// ESP32 + L298N: motor 2 for 3000ms, then motor 1 for 500ms, once.
// Boot is idle. Set the observed clockwise polarities with DIR before RUN.
#include <Arduino.h>
#include <driver/gpio.h>
#include <esp_task_wdt.h>
#include <string.h>

constexpr char VERSION[] = "N20Sequence-3s-500ms-v1";
constexpr uint8_t MOTOR1_EN = 14, MOTOR1_IN1 = 25, MOTOR1_IN2 = 26;
constexpr uint8_t MOTOR2_EN = 27, MOTOR2_IN3 = 21, MOTOR2_IN4 = 22;
constexpr uint8_t CHASSIS_STBY = 33;
constexpr uint32_t MOTOR2_MS = 3000, MOTOR1_MS = 500;
enum class Phase : uint8_t { IDLE, MOTOR2, MOTOR1, DONE, STOPPED, FAULT };
Phase phase = Phase::IDLE;
bool hardwareOK = false, used = false, directionsSet = false;
int direction1 = 1, direction2 = 1;
uint32_t startedAt = 0, motor2Elapsed = 0, motor1Elapsed = 0;
esp_task_wdt_user_handle_t watchdog = nullptr;
char line[32];
size_t lineUsed = 0;
bool overflow = false;

const char *phaseName() {
  switch (phase) {
    case Phase::MOTOR2: return "MOTOR2";
    case Phase::MOTOR1: return "MOTOR1";
    case Phase::DONE: return "DONE";
    case Phase::STOPPED: return "STOPPED";
    case Phase::FAULT: return "FAULT";
    default: return "IDLE";
  }
}
bool active() { return phase == Phase::MOTOR2 || phase == Phase::MOTOR1; }
void lowPin(uint8_t pin) {
  gpio_set_level(static_cast<gpio_num_t>(pin), LOW);
  pinMode(pin, OUTPUT);
}
void clearOutputs() {
  // Disable both bridges before changing any direction; never overlap enables.
  digitalWrite(MOTOR2_EN, LOW);
  digitalWrite(MOTOR1_EN, LOW);
  digitalWrite(CHASSIS_STBY, LOW);
  digitalWrite(MOTOR1_IN1, LOW); digitalWrite(MOTOR1_IN2, LOW);
  digitalWrite(MOTOR2_IN3, LOW); digitalWrite(MOTOR2_IN4, LOW);
}
void stopSequence(Phase target) {
  if (phase == Phase::MOTOR2) motor2Elapsed = uint32_t(millis() - startedAt);
  if (phase == Phase::MOTOR1) motor1Elapsed = uint32_t(millis() - startedAt);
  clearOutputs();
  phase = target;
}
bool outputsExpected() {
  const bool m1 = phase == Phase::MOTOR1, m2 = phase == Phase::MOTOR2;
  return digitalRead(CHASSIS_STBY) == LOW &&
    digitalRead(MOTOR1_EN) == (m1 ? HIGH : LOW) &&
    digitalRead(MOTOR2_EN) == (m2 ? HIGH : LOW) &&
    digitalRead(MOTOR1_IN1) == (m1 && direction1 > 0 ? HIGH : LOW) &&
    digitalRead(MOTOR1_IN2) == (m1 && direction1 < 0 ? HIGH : LOW) &&
    digitalRead(MOTOR2_IN3) == (m2 && direction2 > 0 ? HIGH : LOW) &&
    digitalRead(MOTOR2_IN4) == (m2 && direction2 < 0 ? HIGH : LOW);
}
void fault() {
  hardwareOK = false;
  stopSequence(Phase::FAULT);
  Serial.println("FAULT GPIO readback; sequence aborted; check jumpers/wiring");
}
void enablePhase(Phase target) {
  clearOutputs();
  phase = Phase::IDLE;
  if (!hardwareOK || !outputsExpected()) { fault(); return; }
  if (target == Phase::MOTOR2) {
    digitalWrite(MOTOR2_IN3, direction2 > 0 ? HIGH : LOW);
    digitalWrite(MOTOR2_IN4, direction2 < 0 ? HIGH : LOW);
    phase = target;
    startedAt = millis();
    digitalWrite(MOTOR2_EN, HIGH);
  } else {
    digitalWrite(MOTOR1_IN1, direction1 > 0 ? HIGH : LOW);
    digitalWrite(MOTOR1_IN2, direction1 < 0 ? HIGH : LOW);
    phase = target;
    startedAt = millis();
    digitalWrite(MOTOR1_EN, HIGH);
  }
}
void tickSequence() {
  if (hardwareOK && !outputsExpected()) { fault(); return; }
  if (!hardwareOK) { stopSequence(Phase::FAULT); return; }
  if (phase == Phase::MOTOR2 && uint32_t(millis() - startedAt) >= MOTOR2_MS) {
    motor2Elapsed = uint32_t(millis() - startedAt);
    enablePhase(Phase::MOTOR1);
    if (phase == Phase::MOTOR1) {
      Serial.printf("TRANSITION motor2_stopped=1 m2ms=%lu motor1_started=1 ms=500\n",
                    static_cast<unsigned long>(motor2Elapsed));
    }
  } else if (phase == Phase::MOTOR1 && uint32_t(millis() - startedAt) >= MOTOR1_MS) {
    stopSequence(Phase::DONE);
    Serial.printf("DONE SEQUENCE m2ms=%lu m1ms=%lu ENA=0 ENB=0 STBY=0\n",
                  static_cast<unsigned long>(motor2Elapsed),
                  static_cast<unsigned long>(motor1Elapsed));
  }
}
void printStatus() {
  Serial.printf("STATUS fw=%s hw=%d used=%d dirs=%d phase=%s d1=%d d2=%d ENA=%d ENB=%d IN1=%d IN2=%d IN3=%d IN4=%d STBY=%d m2ms=%lu m1ms=%lu\n",
    VERSION, int(hardwareOK), int(used), int(directionsSet), phaseName(), direction1, direction2,
    digitalRead(MOTOR1_EN), digitalRead(MOTOR2_EN), digitalRead(MOTOR1_IN1),
    digitalRead(MOTOR1_IN2), digitalRead(MOTOR2_IN3), digitalRead(MOTOR2_IN4),
    digitalRead(CHASSIS_STBY), static_cast<unsigned long>(motor2Elapsed),
    static_cast<unsigned long>(motor1Elapsed));
}
void command() {
  // STOP and invalid input take priority over the motor2 -> motor1 transition.
  line[lineUsed] = 0;
  if (overflow) {
    stopSequence(hardwareOK ? Phase::STOPPED : Phase::FAULT);
    Serial.println("ERR command overflow; stopped");
  } else if (!lineUsed) {
    // CRLF / empty lines do not change the sequence.
  } else if (!strcmp(line, "STOP")) {
    stopSequence(hardwareOK ? Phase::STOPPED : Phase::FAULT);
    Serial.println("OK STOP; both motors disabled");
  } else if (!strcmp(line, "STATUS")) {
    tickSequence(); printStatus();
  } else if (!strcmp(line, "DIR 1 1") || !strcmp(line, "DIR 1 -1") ||
             !strcmp(line, "DIR -1 1") || !strcmp(line, "DIR -1 -1")) {
    if (!hardwareOK || used || active()) {
      if (active()) stopSequence(Phase::STOPPED);
      Serial.println("ERR DIR unavailable after RUN; stopped");
    } else {
      clearOutputs();
      direction1 = line[4] == '-' ? -1 : 1;
      direction2 = line[strlen(line)-2] == '-' ? -1 : 1;
      directionsSet = true;
      Serial.printf("OK DIR d1=%d d2=%d; no motion\n", direction1, direction2);
    }
  } else if (!strcmp(line, "RUN")) {
    tickSequence();
    if (!hardwareOK || !directionsSet || used || active()) {
      Serial.println("ERR RUN requires hw=1 dirs=1 used=0; no retry");
    } else {
      used = true;
      motor2Elapsed = motor1Elapsed = 0;
      enablePhase(Phase::MOTOR2);
      if (phase == Phase::MOTOR2)
        Serial.println("START SEQUENCE motor=2 ms=3000 duty=100%; then motor=1 ms=500");
    }
  } else {
    stopSequence(hardwareOK ? Phase::STOPPED : Phase::FAULT);
    Serial.println("ERR use STATUS, STOP, DIR <d1> <d2> or RUN; stopped");
  }
  lineUsed = 0; overflow = false;
}
void setup() {
  lowPin(MOTOR1_EN); lowPin(MOTOR2_EN); lowPin(CHASSIS_STBY);
  lowPin(MOTOR1_IN1); lowPin(MOTOR1_IN2);
  lowPin(MOTOR2_IN3); lowPin(MOTOR2_IN4);
  phase = Phase::IDLE; used = false; directionsSet = false;
  direction1 = direction2 = 1;
  startedAt = motor2Elapsed = motor1Elapsed = 0;
  lineUsed = 0; overflow = false;
  clearOutputs();
  Serial.begin(115200);
  esp_task_wdt_config_t config = {1000, 0, true};
  esp_err_t result = esp_task_wdt_init(&config);
  if (result == ESP_ERR_INVALID_STATE) result = esp_task_wdt_reconfigure(&config);
  hardwareOK = result == ESP_OK && esp_task_wdt_add_user("n20-sequence", &watchdog) == ESP_OK;
  delayMicroseconds(2);
  if (!hardwareOK || !outputsExpected()) fault();
  Serial.printf("BOOT %s; idle; set clockwise polarities using DIR, then RUN once\n", VERSION);
  printStatus();
}
void loop() {
  // Read queued STOP before a transition can enable the release motor.
  for (uint8_t n = 0; n < 64 && Serial.available(); ++n) {
    char ch = char(Serial.read());
    if (ch == '\n' || ch == '\r') command();
    else if (!overflow) {
      if (lineUsed < sizeof(line) - 1 && ch >= 32 && ch <= 126) line[lineUsed++] = ch;
      else { overflow = true; stopSequence(hardwareOK ? Phase::STOPPED : Phase::FAULT); }
    }
  }
  tickSequence();
  if (watchdog) esp_task_wdt_reset_user(watchdog);
  delay(1);
}
