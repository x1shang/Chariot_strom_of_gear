// ESP32 + L298N: two 130 motors with friction flywheels; soft-start spin test.
// Replaces the retired winch/N20 launcher (torque destroyed the motor).
// Boot is idle. Both flywheels are driven together, once per boot, duty-capped.
// EN pins are now PWM (5 kHz) instead of static enable; IN pins stay static.
#include <Arduino.h>
#include <driver/gpio.h>
#include <esp_task_wdt.h>
#include <stdlib.h>
#include <string.h>

constexpr char VERSION[] = "FlywheelSpin-v3-100";
constexpr uint8_t FW1_EN = 14, FW1_IN1 = 25, FW1_IN2 = 26;  // OUT1/OUT2 -> left flywheel
constexpr uint8_t FW2_EN = 27, FW2_IN3 = 21, FW2_IN4 = 22;  // OUT3/OUT4 -> right flywheel
constexpr uint8_t CHASSIS_STBY = 33;                        // chassis stays disabled here

// L298 is a slow bipolar bridge: 5 kHz keeps switching loss low. 8 bit matches the
// chassis firmware. Channels 0/1 are free here because the chassis is disabled; a
// future combined firmware must re-allocate the chassis 0-7 channels and timers.
constexpr uint32_t PWM_FREQ = 5000;
constexpr uint8_t PWM_BITS = 8, PWM_CH1 = 0, PWM_CH2 = 1;
constexpr uint32_t PWM_FULL = (1u << PWM_BITS) - 1u;  // 255

// User-requested 100% ceiling for staged diagnosis; default stays at 30%.
// PWM duty is not a motor-voltage limit: verify the VS supply before a run.
constexpr uint8_t DUTY_MIN_PERCENT = 1, DUTY_MAX_PERCENT = 100, DUTY_DEFAULT_PERCENT = 30;
constexpr uint32_t RUN_MS_MAX = 10000, RUN_MS_DEFAULT = 3000;
constexpr uint32_t RAMP_MS = 1200;  // soft start: limits flywheel inrush current

enum class Phase : uint8_t { IDLE, RAMP, HOLD, DONE, STOPPED, FAULT };
Phase phase = Phase::IDLE;
bool hardwareOK = false, used = false, directionsSet = false;
int direction1 = 1, direction2 = 1;
uint8_t targetDuty = 0, appliedDuty = 0;  // raw 8-bit values
uint8_t targetPercent = DUTY_DEFAULT_PERCENT;
uint32_t targetMs = RUN_MS_DEFAULT;
uint32_t startedAt = 0, elapsedMs = 0;
esp_task_wdt_user_handle_t watchdog = nullptr;
char line[32];
size_t lineUsed = 0;
bool overflow = false;

const char *phaseName() {
  switch (phase) {
    case Phase::RAMP: return "RAMP";
    case Phase::HOLD: return "HOLD";
    case Phase::DONE: return "DONE";
    case Phase::STOPPED: return "STOPPED";
    case Phase::FAULT: return "FAULT";
    default: return "IDLE";
  }
}
bool active() { return phase == Phase::RAMP || phase == Phase::HOLD; }
uint8_t dutyForPercent(uint8_t percent) {
  return uint8_t((uint32_t(percent) * PWM_FULL + 50u) / 100u);
}
void lowPin(uint8_t pin) {
  gpio_set_level(static_cast<gpio_num_t>(pin), LOW);
  pinMode(pin, OUTPUT);
}
void clearOutputs() {
  // Disable both bridges before touching direction; never leave an enable high.
  ledcWrite(FW1_EN, 0);
  ledcWrite(FW2_EN, 0);
  appliedDuty = 0;
  digitalWrite(CHASSIS_STBY, LOW);
  digitalWrite(FW1_IN1, LOW); digitalWrite(FW1_IN2, LOW);
  digitalWrite(FW2_IN3, LOW); digitalWrite(FW2_IN4, LOW);
}
void stopSpin(Phase target) {
  if (active()) elapsedMs = uint32_t(millis() - startedAt);
  clearOutputs();
  phase = target;
}
bool enReadback(uint8_t pin, uint8_t expected) {
  // Arduino-ESP32 3.3.12 maps an 8-bit write of 255 to full-on register value 256.
  const uint32_t expectedReadback = expected == PWM_FULL ? PWM_FULL + 1u : expected;
  if (ledcRead(pin) != expectedReadback) return false;
  // Duty 0 parks the pad low, so an external pull-up / left-in enable jumper is
  // still visible at the pad. Readback under PWM is a peripheral (commanded duty)
  // value only and cannot prove the pad level.
  return expected != 0 || digitalRead(pin) == LOW;
}
bool outputsExpected() {
  const bool m1 = active() && direction1 > 0, m2 = active() && direction2 > 0;
  const bool n1 = active() && direction1 < 0, n2 = active() && direction2 < 0;
  return digitalRead(CHASSIS_STBY) == LOW &&
    digitalRead(FW1_IN1) == (m1 ? HIGH : LOW) && digitalRead(FW1_IN2) == (n1 ? HIGH : LOW) &&
    digitalRead(FW2_IN3) == (m2 ? HIGH : LOW) && digitalRead(FW2_IN4) == (n2 ? HIGH : LOW) &&
    enReadback(FW1_EN, appliedDuty) && enReadback(FW2_EN, appliedDuty);
}
void fault(const char *reason) {
  hardwareOK = false;
  stopSpin(Phase::FAULT);
  Serial.printf("FAULT %s; outputs disabled; check enable jumpers/wiring\n", reason);
}
void applyDuty(uint8_t duty) {
  ledcWrite(FW1_EN, duty);
  ledcWrite(FW2_EN, duty);
  appliedDuty = duty;
}
void tickSpin() {
  if (hardwareOK && !outputsExpected()) { fault("GPIO readback"); return; }
  if (!hardwareOK) { stopSpin(Phase::FAULT); return; }
  if (!active()) return;
  const uint32_t elapsed = uint32_t(millis() - startedAt);
  if (phase == Phase::RAMP) {
    if (elapsed >= RAMP_MS) {
      applyDuty(targetDuty);
      phase = Phase::HOLD;
      Serial.printf("HOLD duty=%u%% raw=%u elapsed=%lu; ramp done\n",
                    unsigned(targetPercent), unsigned(targetDuty),
                    static_cast<unsigned long>(elapsed));
    } else {
      // Linear ramp from 0: the average applied voltage starts low, so the initial
      // current is roughly duty x stall current instead of full stall current.
      applyDuty(uint8_t((uint32_t(targetDuty) * elapsed) / RAMP_MS));
    }
  }
  if (phase == Phase::HOLD && elapsed >= targetMs) {
    stopSpin(Phase::DONE);
    Serial.printf("DONE SPIN elapsed=%lu DUTY1=0 DUTY2=0; flywheel COASTS now, not stopped\n",
                  static_cast<unsigned long>(elapsedMs));
  }
}
void printStatus() {
  const uint32_t elapsed = active() ? uint32_t(millis() - startedAt) : elapsedMs;
  Serial.printf("STATUS fw=%s hw=%d used=%d dirs=%d phase=%s d1=%d d2=%d pct=%u ms=%lu "
                "DUTY1=%lu DUTY2=%lu IN1=%d IN2=%d IN3=%d IN4=%d STBY=%d elapsed=%lu\n",
    VERSION, int(hardwareOK), int(used), int(directionsSet), phaseName(), direction1, direction2,
    unsigned(targetPercent), static_cast<unsigned long>(targetMs),
    static_cast<unsigned long>(ledcRead(FW1_EN)), static_cast<unsigned long>(ledcRead(FW2_EN)),
    digitalRead(FW1_IN1), digitalRead(FW1_IN2), digitalRead(FW2_IN3), digitalRead(FW2_IN4),
    digitalRead(CHASSIS_STBY), static_cast<unsigned long>(elapsed));
}
// Parses up to maxCount integers, rejecting trailing garbage. Returns 255 on error.
uint8_t parseLongs(const char *text, long *values, uint8_t maxCount) {
  uint8_t count = 0;
  while (*text) {
    while (*text == ' ') ++text;
    if (!*text) break;
    if (count >= maxCount) return 255;
    char *end = nullptr;
    const long value = strtol(text, &end, 10);
    if (end == text) return 255;
    values[count++] = value;
    text = end;
  }
  return count;
}
void startSpin(uint8_t percent, uint32_t ms) {
  used = true;
  targetPercent = percent;
  targetMs = ms;
  targetDuty = dutyForPercent(percent);
  elapsedMs = 0;
  clearOutputs();
  digitalWrite(FW1_IN1, direction1 > 0 ? HIGH : LOW);
  digitalWrite(FW1_IN2, direction1 < 0 ? HIGH : LOW);
  digitalWrite(FW2_IN3, direction2 > 0 ? HIGH : LOW);
  digitalWrite(FW2_IN4, direction2 < 0 ? HIGH : LOW);
  phase = Phase::RAMP;
  startedAt = millis();
  Serial.printf("START SPIN duty=%u%% raw=%u ms=%lu ramp=%lums dirs=%d,%d\n",
                unsigned(percent), unsigned(targetDuty), static_cast<unsigned long>(ms),
                static_cast<unsigned long>(RAMP_MS), direction1, direction2);
}
void commandSpin() {
  long values[3] = {0, 0, 0};
  const uint8_t count = parseLongs(line + 4, values, 3);
  if (count > 2) { stopSpin(Phase::STOPPED); Serial.println("ERR use SPIN [duty] [ms]; stopped"); return; }
  const long percent = count >= 1 ? values[0] : DUTY_DEFAULT_PERCENT;
  const long ms = count >= 2 ? values[1] : RUN_MS_DEFAULT;
  if (!hardwareOK || !directionsSet || used || active()) {
    if (active()) stopSpin(Phase::STOPPED);
    Serial.println("ERR SPIN requires hw=1 dirs=1 used=0, idle; no retry");
    return;
  }
  if (percent < DUTY_MIN_PERCENT || percent > DUTY_MAX_PERCENT) {
    Serial.printf("ERR duty must be %u..%u%%; no motion\n", unsigned(DUTY_MIN_PERCENT),
                  unsigned(DUTY_MAX_PERCENT));
    return;
  }
  if (ms < 1 || uint32_t(ms) > RUN_MS_MAX) {
    Serial.printf("ERR ms must be 1..%lu; no motion\n", static_cast<unsigned long>(RUN_MS_MAX));
    return;
  }
  startSpin(uint8_t(percent), uint32_t(ms));
}
void command() {
  // STOP and invalid input take priority over any ramp/hold step.
  line[lineUsed] = 0;
  if (overflow) {
    stopSpin(hardwareOK ? Phase::STOPPED : Phase::FAULT);
    Serial.println("ERR command overflow; stopped");
  } else if (!lineUsed) {
    // CRLF / empty lines change nothing.
  } else if (!strcmp(line, "STOP")) {
    stopSpin(hardwareOK ? Phase::STOPPED : Phase::FAULT);
    Serial.println("OK STOP; both flywheel enables 0; flywheel COASTS, not stopped");
  } else if (!strcmp(line, "STATUS")) {
    tickSpin(); printStatus();
  } else if (!strcmp(line, "DIR 1 1") || !strcmp(line, "DIR 1 -1") ||
             !strcmp(line, "DIR -1 1") || !strcmp(line, "DIR -1 -1")) {
    if (!hardwareOK || used || active()) {
      if (active()) stopSpin(Phase::STOPPED);
      Serial.println("ERR DIR unavailable after SPIN; stopped");
    } else {
      clearOutputs();
      direction1 = line[4] == '-' ? -1 : 1;
      direction2 = line[strlen(line) - 2] == '-' ? -1 : 1;
      directionsSet = true;
      Serial.printf("OK DIR d1=%d d2=%d; no motion; counter-rotation not yet verified\n",
                    direction1, direction2);
    }
  } else if (!strncmp(line, "SPIN", 4)) {
    tickSpin(); commandSpin();
  } else {
    stopSpin(hardwareOK ? Phase::STOPPED : Phase::FAULT);
    Serial.println("ERR use STATUS, STOP, DIR <d1> <d2> or SPIN [duty] [ms]; stopped");
  }
  lineUsed = 0; overflow = false;
}
void setup() {
  lowPin(FW1_EN); lowPin(FW2_EN); lowPin(CHASSIS_STBY);
  lowPin(FW1_IN1); lowPin(FW1_IN2); lowPin(FW2_IN3); lowPin(FW2_IN4);
  phase = Phase::IDLE; used = false; directionsSet = false;
  direction1 = direction2 = 1;
  targetPercent = DUTY_DEFAULT_PERCENT; targetMs = RUN_MS_DEFAULT;
  targetDuty = appliedDuty = 0;
  startedAt = elapsedMs = 0;
  lineUsed = 0; overflow = false;
  Serial.begin(115200);
  esp_task_wdt_config_t config = {1000, 0, true};
  esp_err_t result = esp_task_wdt_init(&config);
  if (result == ESP_ERR_INVALID_STATE) result = esp_task_wdt_reconfigure(&config);
  hardwareOK = result == ESP_OK && esp_task_wdt_add_user("flywheel-spin", &watchdog) == ESP_OK;
  // Pad-level idle check BEFORE the LEDC takes over, so a left-in enable jumper or
  // an external pull-up still shows up as a fault.
  delayMicroseconds(2);
  if (!hardwareOK) {
    Serial.println("FAULT watchdog init");
  } else if (digitalRead(FW1_EN) || digitalRead(FW2_EN) || digitalRead(CHASSIS_STBY) ||
             digitalRead(FW1_IN1) || digitalRead(FW1_IN2) || digitalRead(FW2_IN3) ||
             digitalRead(FW2_IN4)) {
    fault("idle GPIO readback before PWM attach");
  } else if (!ledcAttachChannel(FW1_EN, PWM_FREQ, PWM_BITS, PWM_CH1) ||
             !ledcAttachChannel(FW2_EN, PWM_FREQ, PWM_BITS, PWM_CH2)) {
    fault("ledcAttachChannel failed");
  } else {
    clearOutputs();
    if (!outputsExpected()) fault("idle GPIO readback after PWM attach");
  }
  if (hardwareOK) {
    Serial.printf("BOOT %s; idle; duty cap %u%%; set polarity with DIR then SPIN once\n",
                  VERSION, unsigned(DUTY_MAX_PERCENT));
  }
  printStatus();
}
void loop() {
  // Read queued STOP before a ramp step can raise the duty further.
  for (uint8_t n = 0; n < 64 && Serial.available(); ++n) {
    char ch = char(Serial.read());
    if (ch == '\n' || ch == '\r') command();
    else if (!overflow) {
      if (lineUsed < sizeof(line) - 1 && ch >= 32 && ch <= 126) line[lineUsed++] = ch;
      else { overflow = true; stopSpin(hardwareOK ? Phase::STOPPED : Phase::FAULT); }
    }
  }
  tickSpin();
  if (watchdog) esp_task_wdt_reset_user(watchdog);
  delay(1);
}
