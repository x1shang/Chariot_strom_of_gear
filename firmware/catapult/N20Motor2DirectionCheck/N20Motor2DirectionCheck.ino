// Standalone L298N motor 2 direction check; Arduino-ESP32 3.x.
// User confirmed motor 1 on OUT1/2, motor 2 on OUT3/4; EN jumpers removed.
// Boot is idle. FWD3S drives motor 2 once per boot for 3000ms, no reverse.
#include <Arduino.h>
#include <driver/gpio.h>
#include <esp_task_wdt.h>
#include <string.h>

constexpr char VERSION[] = "N20Motor2Fwd3s-v2";
constexpr uint8_t MOTOR1_EN = 14, MOTOR1_IN1 = 25, MOTOR1_IN2 = 26;
constexpr uint8_t MOTOR2_EN = 27, MOTOR2_IN3 = 21, MOTOR2_IN4 = 22;
constexpr uint8_t CHASSIS_STBY = 33;
constexpr uint32_t RUN_MS = 3000;

bool hardwareOK = false, used = false, running = false;
uint32_t startedAt = 0, elapsedMs = 0;
esp_task_wdt_user_handle_t watchdog = nullptr;
char line[24];
size_t lineUsed = 0;
bool overflow = false;

void lowPin(uint8_t pin) {
  gpio_set_level(static_cast<gpio_num_t>(pin), LOW);
  pinMode(pin, OUTPUT);
}
void disableMotor1AndChassis() {
  digitalWrite(MOTOR1_EN, LOW);
  digitalWrite(MOTOR1_IN1, LOW);
  digitalWrite(MOTOR1_IN2, LOW);
  digitalWrite(CHASSIS_STBY, LOW);
}
void stopMotor2() {
  // Disable bridge first; this is coasting, not position holding.
  digitalWrite(MOTOR2_EN, LOW);
  digitalWrite(MOTOR2_IN3, LOW);
  digitalWrite(MOTOR2_IN4, LOW);
  if (running) elapsedMs = uint32_t(millis() - startedAt);
  running = false;
  disableMotor1AndChassis();
}
bool outputsExpected() {
  return digitalRead(MOTOR1_EN) == LOW && digitalRead(MOTOR1_IN1) == LOW &&
    digitalRead(MOTOR1_IN2) == LOW && digitalRead(CHASSIS_STBY) == LOW &&
    digitalRead(MOTOR2_EN) == (running ? HIGH : LOW) &&
    digitalRead(MOTOR2_IN3) == (running ? HIGH : LOW) && digitalRead(MOTOR2_IN4) == LOW;
}
void tickMotor2() {
  if (hardwareOK && !outputsExpected()) {
    hardwareOK = false;
    stopMotor2();
    Serial.println("FAULT GPIO readback; motion refused; check enable jumpers/wiring");
  }
  if (!hardwareOK || (running && uint32_t(millis() - startedAt) >= RUN_MS)) {
    bool finished = running;
    stopMotor2();
    if (finished) Serial.printf("DONE FWD3S elapsed=%lu ENB=0; no automatic repeat\n",
                                static_cast<unsigned long>(elapsedMs));
  }
}
void printStatus() {
  uint32_t elapsed = running ? uint32_t(millis() - startedAt) : elapsedMs;
  Serial.printf("STATUS fw=%s hw=%d used=%d running=%d ENA=%d ENB=%d IN3=%d IN4=%d STBY=%d elapsed=%lu\n",
    VERSION, int(hardwareOK), int(used), int(running), digitalRead(MOTOR1_EN),
    digitalRead(MOTOR2_EN), digitalRead(MOTOR2_IN3), digitalRead(MOTOR2_IN4),
    digitalRead(CHASSIS_STBY), static_cast<unsigned long>(elapsed));
}
void command() {
  tickMotor2();
  line[lineUsed] = 0;
  if (overflow) {
    stopMotor2(); Serial.println("ERR command overflow; stopped");
  } else if (!lineUsed) {
    // Ignore empty lines.
  } else if (!strcmp(line, "STOP")) {
    stopMotor2(); Serial.println("OK STOP; motor 1 disabled");
  } else if (!strcmp(line, "STATUS")) {
    printStatus();
  } else if (!strcmp(line, "FWD3S")) {
    if (!hardwareOK || used || running) {
      Serial.println("ERR FWD3S unavailable; once per boot, no retry");
    } else {
      stopMotor2();
      digitalWrite(MOTOR2_IN4, LOW);
      digitalWrite(MOTOR2_IN3, HIGH);
      used = true;
      startedAt = millis();
      elapsedMs = 0;
      running = true;
      digitalWrite(MOTOR2_EN, HIGH); // 100% enable; electrical forward.
      Serial.println("START FWD3S motor=2 ms=3000 IN3=1 IN4=0 ENB=1 ENA=0");
    }
  } else {
    stopMotor2(); Serial.println("ERR use STATUS, STOP or FWD3S; stopped");
  }
  lineUsed = 0;
  overflow = false;
}
void setup() {
  lowPin(MOTOR1_EN);
  lowPin(MOTOR2_EN);
  lowPin(CHASSIS_STBY);
  lowPin(MOTOR1_IN1); lowPin(MOTOR1_IN2);
  lowPin(MOTOR2_IN3); lowPin(MOTOR2_IN4);
  used = false; running = false; startedAt = elapsedMs = 0;
  lineUsed = 0; overflow = false;
  stopMotor2();
  Serial.begin(115200);
  esp_task_wdt_config_t config = {1000, 0, true};
  esp_err_t result = esp_task_wdt_init(&config);
  if (result == ESP_ERR_INVALID_STATE) result = esp_task_wdt_reconfigure(&config);
  hardwareOK = result == ESP_OK &&
    esp_task_wdt_add_user("motor2-direction-check", &watchdog) == ESP_OK;
  delayMicroseconds(2);
  if (!outputsExpected()) {
    hardwareOK = false;
    Serial.println("FAULT idle GPIO readback; motion refused; check enable jumpers/wiring");
  }
  Serial.printf("BOOT %s; idle; motor1 disabled; FWD3S once per boot\n", VERSION);
  printStatus();
}
void loop() {
  tickMotor2();
  for (uint8_t n = 0; n < 32 && Serial.available(); ++n) {
    char ch = char(Serial.read());
    if (ch == '\n' || ch == '\r') command();
    else if (!overflow) {
      if (lineUsed < sizeof(line) - 1 && ch >= 32 && ch <= 126) line[lineUsed++] = ch;
      else { overflow = true; stopMotor2(); }
    }
    tickMotor2();
  }
  if (watchdog) esp_task_wdt_reset_user(watchdog);
  delay(1);
}
