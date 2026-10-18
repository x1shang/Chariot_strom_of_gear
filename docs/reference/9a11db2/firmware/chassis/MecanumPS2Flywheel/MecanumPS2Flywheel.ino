// ESP32-WROOM-32E + two TB6612 boards + the existing PS2 receiver.
// Arduino-ESP32 3.x. PWMA/PWMB are tied to 3.3V (two-input PWM wiring).
#include <Arduino.h>
#include <driver/gpio.h>
#include <esp_task_wdt.h>
#include <string.h>
#include "DpadControl.h"
#include "DriveMath.h"

// Full duty is constant HIGH: no extra LEDC channel/timer is needed.
constexpr uint8_t FW1_EN = 14, FW1_IN1 = 25, FW1_IN2 = 26;
constexpr uint8_t FW2_EN = 27, FW2_IN3 = 21, FW2_IN4 = 22;
constexpr uint16_t PAD_R2 = 1u << 9;
// Preserve the latest test polarity; physical counter-rotation requires observation.
constexpr int FW1_DIRECTION = 1, FW2_DIRECTION = 1;
bool firing = false;
constexpr uint8_t STBY = 33;
constexpr uint8_t PS_CLK = 2, PS_CS = 4, PS_CMD = 12, PS_DAT = 13;
constexpr uint8_t PINS[4][2] = {{16,17}, {18,19}, {23,32}, {15,5}};
// FL, RL, FR, RR: calibrated in this project's 2026-10-07 wheel tests.
constexpr int POLARITY[4] = {1,1,-1,-1};
constexpr int DRIVE_DUTY = 255; // 100%; duty is not measured speed/current.
static_assert(DRIVE_DUTY > 0 && DRIVE_DUTY <= 255, "DRIVE_DUTY must be 1..255");

bool attached[4][2] = {}, hardwareOK = false;
uint8_t psMode = 0;
uint32_t lastPoll = 0, lastTick = 0, lastPrint = 0;
DpadControl pad;
WheelRamp wheels[4];
esp_task_wdt_user_handle_t watchdog = nullptr;
char line[40];
size_t used = 0;
bool overflow = false;

void lowPin(uint8_t pin) {
  gpio_set_level(static_cast<gpio_num_t>(pin), 0);
  pinMode(pin, OUTPUT);
}
void stopDrive() {
  digitalWrite(STBY, LOW); // Disable both drivers before clearing PWM.
  for (int i = 0; i < 4; ++i) {
    for (int j = 0; j < 2; ++j)
      if (attached[i][j]) ledcWrite(PINS[i][j], 0);
    wheels[i].stop(millis());
  }
}
void stopFlywheels() {
  digitalWrite(FW1_EN, LOW); digitalWrite(FW2_EN, LOW);
  digitalWrite(FW1_IN1, LOW); digitalWrite(FW1_IN2, LOW);
  digitalWrite(FW2_IN3, LOW); digitalWrite(FW2_IN4, LOW);
  firing = false;
}
void lockDrive() { stopDrive(); stopFlywheels(); pad.lock(); }
void tickFlywheels() {
  if (!hardwareOK || !pad.fresh(millis()) || !pad.armed ||
      (pad.buttons & PAD_CIRCLE) || !(pad.buttons & PAD_R2)) {
    stopFlywheels(); return;
  }
  digitalWrite(FW1_IN1, FW1_DIRECTION > 0 ? HIGH : LOW);
  digitalWrite(FW1_IN2, FW1_DIRECTION < 0 ? HIGH : LOW);
  digitalWrite(FW2_IN3, FW2_DIRECTION > 0 ? HIGH : LOW);
  digitalWrite(FW2_IN4, FW2_DIRECTION < 0 ? HIGH : LOW);
  digitalWrite(FW1_EN, HIGH); digitalWrite(FW2_EN, HIGH);
  firing = true;
}

uint8_t exchangeByte(uint8_t tx) {
  uint8_t rx = 0;
  for (uint8_t bit = 0; bit < 8; ++bit) {
    digitalWrite(PS_CMD, (tx >> bit) & 1);
    digitalWrite(PS_CLK, LOW); delayMicroseconds(4);
    if (digitalRead(PS_DAT)) rx |= 1u << bit;
    digitalWrite(PS_CLK, HIGH); delayMicroseconds(4);
  }
  digitalWrite(PS_CMD, HIGH); delayMicroseconds(20);
  return rx;
}
// PS2X-compatible analog-mode negotiation, only while outputs are idle.
void configureAnalog() {
  lockDrive();
  const uint8_t commands[3][9] = {
    {1,0x43,0,1,0,0,0,0,0},
    {1,0x44,0,1,3,0,0,0,0},
    {1,0x43,0,0,0x5A,0x5A,0x5A,0x5A,0x5A}
  };
  for (int c = 0; c < 3; ++c) {
    digitalWrite(PS_CS, LOW); delayMicroseconds(20);
    for (int i = 0; i < (c == 0 ? 5 : 9); ++i) exchangeByte(commands[c][i]);
    digitalWrite(PS_CS, HIGH); delay(10);
  }
}
void pollPS() {
  uint8_t rx[21] = {}, length = 9;
  digitalWrite(PS_CS, LOW); delayMicroseconds(20);
  for (uint8_t i = 0; i < length; ++i) {
    rx[i] = exchangeByte(i == 0 ? 1 : (i == 1 ? 0x42 : 0));
    if (i == 1 && rx[1] == 0x79) length = 21;
  }
  digitalWrite(PS_CS, HIGH);
  psMode = rx[1];
  pad.frame(psMode, rx[2], uint16_t(rx[3]) | (uint16_t(rx[4]) << 8), millis(), rx[5]);
  tickFlywheels();
  int y, x;
  // Release/invalid/emergency input bypasses deceleration: power is removed.
  if (!pad.axes(millis(), y, x)) stopDrive();
  static uint32_t lastConfig = 0;
  if (pad.online && !pad.analog && !pad.buttons && !firing &&
      digitalRead(STBY) == LOW && uint32_t(millis() - lastConfig) >= 2000) {
    lastConfig = millis(); configureAnalog();
  }
}
void tickDrive() {
  tickFlywheels();
  int y, x, targets[4];
  if (!hardwareOK || !pad.fresh(millis())) { lockDrive(); return; }
  if (!pad.axes(millis(), y, x)) { stopDrive(); return; }
  mixDrive(y, x, pad.turn, DRIVE_DUTY, targets);
  bool moving = false, ok = true;
  for (int i = 0; i < 4; ++i) {
    int duty = wheels[i].step(targets[i] * POLARITY[i], millis());
    moving |= duty != 0;
    // Zero the inactive input before energizing the active input.
    if (duty >= 0) {
      ok = ledcWrite(PINS[i][1], 0) && ok;
      ok = ledcWrite(PINS[i][0], duty) && ok;
    } else {
      ok = ledcWrite(PINS[i][0], 0) && ok;
      ok = ledcWrite(PINS[i][1], -duty) && ok;
    }
  }
  if (!ok) {
    hardwareOK = false; lockDrive(); Serial.println("FAULT PWM; disabled");
    return;
  }
  digitalWrite(STBY, moving ? HIGH : LOW);
}
void printStatus() {
  Serial.printf("STATUS fw=MecanumPS2Flywheel-v2 hw=%d ps=%d mode=%02X buttons=%04X ready=%d STBY=%d duty=%d,%d,%d,%d fire=%d FW=%d,%d RX=%u turn=%d analog=%d\n",
    int(hardwareOK), int(pad.fresh(millis())), psMode, pad.buttons, int(pad.armed),
    digitalRead(STBY), wheels[0].value, wheels[1].value, wheels[2].value, wheels[3].value, int(firing),
    digitalRead(FW1_EN) * 100, digitalRead(FW2_EN) * 100, unsigned(pad.rightX), pad.turn, int(pad.analog));
}
void command() {
  line[used] = 0;
  if (overflow) { lockDrive(); Serial.println("ERR overflow; release all buttons"); }
  else if (!used) { }
  else if (!strcmp(line, "STATUS")) printStatus();
  else if (!strcmp(line, "STOP")) {
    lockDrive(); Serial.println("OK STOP; release all buttons before driving");
  } else { lockDrive(); Serial.println("ERR use STATUS or STOP; release all buttons"); }
  used = 0; overflow = false;
}
void setup() {
  lowPin(STBY);
  lowPin(FW1_EN); lowPin(FW2_EN);
  lowPin(FW1_IN1); lowPin(FW1_IN2); lowPin(FW2_IN3); lowPin(FW2_IN4);
  Serial.begin(115200);
  hardwareOK = !digitalRead(FW1_EN) && !digitalRead(FW2_EN) &&
    !digitalRead(FW1_IN1) && !digitalRead(FW1_IN2) &&
    !digitalRead(FW2_IN3) && !digitalRead(FW2_IN4);
  for (int i = 0; i < 4; ++i) for (int j = 0; j < 2; ++j) {
    lowPin(PINS[i][j]);
    attached[i][j] = ledcAttachChannel(PINS[i][j], 20000, 8, i * 2 + j);
    if (!attached[i][j] || !ledcWrite(PINS[i][j], 0)) hardwareOK = false;
  }
  digitalWrite(PS_CS, HIGH); pinMode(PS_CS, OUTPUT);
  digitalWrite(PS_CLK, HIGH); pinMode(PS_CLK, OUTPUT);
  digitalWrite(PS_CMD, HIGH); pinMode(PS_CMD, OUTPUT);
  pinMode(PS_DAT, INPUT_PULLUP);
  lockDrive();
  configureAnalog();
  esp_task_wdt_config_t config = {1000, 0, true};
  esp_err_t result = esp_task_wdt_init(&config);
  if (result == ESP_ERR_INVALID_STATE) result = esp_task_wdt_reconfigure(&config);
  if (result != ESP_OK || esp_task_wdt_add_user("dpad-loop", &watchdog) != ESP_OK)
    hardwareOK = false;
  Serial.println("BOOT MecanumPS2Flywheel-v2; release all buttons until ready=1");
  Serial.println("D-pad: forward/back/strafe/diagonal; right stick X: turn; hold R2: both flywheels 100%; CIRCLE stops ALL");
  printStatus();
}
void loop() {
  uint32_t now = millis();
  if (uint32_t(now - lastPoll) >= 20) { lastPoll = now; pollPS(); }
  if (!pad.fresh(millis())) lockDrive();
  for (uint8_t n = 0; n < 64 && Serial.available(); ++n) {
    char ch = char(Serial.read());
    if (ch == '\n' || ch == '\r') command();
    else if (!overflow) {
      if (used < sizeof(line) - 1 && ch >= 32 && ch <= 126) line[used++] = ch;
      else { overflow = true; lockDrive(); }
    }
  }
  now = millis();
  if (uint32_t(now - lastTick) >= 10) { lastTick = now; tickDrive(); }
  if (uint32_t(now - lastPrint) >= 500 && Serial.availableForWrite() > 120) {
    lastPrint = now; printStatus();
  }
  if (watchdog) esp_task_wdt_reset_user(watchdog);
  delay(1);
}
