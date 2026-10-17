// Read-only PS2 receiver check. All motor enables stay LOW for the whole run.
// Poll timing and pin assignment are taken from the existing MecanumPS2Dpad.
#include <Arduino.h>
#include <driver/gpio.h>
#include <esp_task_wdt.h>
#include <string.h>

constexpr char VERSION[] = "PS2SignalCheck-v1";
constexpr uint8_t PS_CLK = 2, PS_CS = 4, PS_CMD = 12, PS_DAT = 13;
constexpr uint8_t STBY = 33, ENA = 14, ENB = 27;
constexpr uint8_t MOTOR_PINS[] = {33,14,27,25,26,21,22,16,17,18,19,23,32,15,5};
bool hardwareOK = false, valid = false;
uint8_t mode = 0, marker = 0, raw[21] = {}, rawLength = 9;
uint16_t buttons = 0;
uint32_t totalFrames = 0, validFrames = 0, invalidFrames = 0;
uint32_t lastValidAt = 0, lastPoll = 0, lastPrint = 0, changes = 0;
esp_task_wdt_user_handle_t watchdog = nullptr;
char line[24]; size_t used = 0; bool overflow = false;

void lowPin(uint8_t pin) {
  gpio_set_level(static_cast<gpio_num_t>(pin), LOW);
  pinMode(pin, OUTPUT);
}
void stopAll() { for (uint8_t pin : MOTOR_PINS) digitalWrite(pin, LOW); }
bool outputsDisabled() {
  for (uint8_t pin : MOTOR_PINS) if (digitalRead(pin) != LOW) return false;
  return true;
}
bool fresh() { return valid && validFrames > 0 && uint32_t(millis()-lastValidAt) < 150; }
void checkDisabled() {
  if (hardwareOK && !outputsDisabled()) {
    hardwareOK = false;
    stopAll();
    Serial.println("FAULT motor GPIO readback; all motor commands unavailable");
  }
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
void acceptFrame(const uint8_t *rx, uint8_t length) {
  ++totalFrames;
  mode = rx[1]; marker = rx[2]; rawLength = length;
  memcpy(raw, rx, length);
  valid = marker == 0x5A && (mode == 0x41 || mode == 0x73 || mode == 0x79);
  if (!valid) { ++invalidFrames; buttons = 0; return; }
  ++validFrames; lastValidAt = millis();
  uint16_t next = uint16_t(~(uint16_t(rx[3]) | uint16_t(rx[4]) << 8));
  if (next != buttons) {
    ++changes;
    Serial.printf("BUTTONS previous=%04X current=%04X time=%lu\n", buttons, next,
                  static_cast<unsigned long>(millis()));
  }
  buttons = next;
}
void pollPS() {
  uint8_t rx[21] = {}, length = 9;
  digitalWrite(PS_CS, LOW); delayMicroseconds(20);
  for (uint8_t i = 0; i < length; ++i) {
    rx[i] = exchangeByte(i == 0 ? 1 : (i == 1 ? 0x42 : 0));
    if (i == 1 && rx[1] == 0x79) length = 21;
  }
  digitalWrite(PS_CS, HIGH);
  acceptFrame(rx, length);
}
void printStatus() {
  checkDisabled();
  Serial.printf("STATUS fw=%s hw=%d ps=%d mode=%02X marker=%02X buttons=%04X frames=%lu good=%lu bad=%lu changes=%lu STBY=%d ENA=%d ENB=%d\n",
    VERSION, int(hardwareOK), int(fresh()), mode, marker, buttons,
    static_cast<unsigned long>(totalFrames), static_cast<unsigned long>(validFrames),
    static_cast<unsigned long>(invalidFrames), static_cast<unsigned long>(changes),
    digitalRead(STBY), digitalRead(ENA), digitalRead(ENB));
}
void command() {
  line[used] = 0;
  if (overflow) { stopAll(); Serial.println("ERR overflow; motors disabled"); }
  else if (!used) { }
  else if (!strcmp(line, "STATUS")) printStatus();
  else if (!strcmp(line, "STOP")) { stopAll(); Serial.println("OK STOP; all motors disabled"); }
  else if (!strcmp(line, "RAW")) {
    Serial.printf("RAW len=%u bytes=", rawLength);
    for (uint8_t i = 0; i < rawLength; ++i) Serial.printf("%02X%s", raw[i], i+1 == rawLength ? "\n" : ",");
  } else { stopAll(); Serial.println("ERR read-only: STATUS, RAW or STOP"); }
  used = 0; overflow = false;
}
void setup() {
  for (uint8_t pin : MOTOR_PINS) lowPin(pin);
  stopAll();
  valid = false; mode = marker = 0; buttons = 0;
  totalFrames = validFrames = invalidFrames = changes = 0;
  lastValidAt = lastPoll = lastPrint = 0;
  used = 0; overflow = false;
  digitalWrite(PS_CS, HIGH); pinMode(PS_CS, OUTPUT);
  digitalWrite(PS_CLK, HIGH); pinMode(PS_CLK, OUTPUT);
  digitalWrite(PS_CMD, HIGH); pinMode(PS_CMD, OUTPUT);
  pinMode(PS_DAT, INPUT_PULLUP);
  Serial.begin(115200);
  esp_task_wdt_config_t config = {1000, 0, true};
  esp_err_t result = esp_task_wdt_init(&config);
  if (result == ESP_ERR_INVALID_STATE) result = esp_task_wdt_reconfigure(&config);
  hardwareOK = result == ESP_OK && esp_task_wdt_add_user("ps2-signal-check", &watchdog) == ESP_OK;
  delayMicroseconds(2);
  if (!outputsDisabled()) hardwareOK = false;
  Serial.printf("BOOT %s; receiver input only; all motor outputs disabled\n", VERSION);
  printStatus();
}
void loop() {
  stopAll(); checkDisabled();
  if (uint32_t(millis()-lastPoll) >= 20) { lastPoll = millis(); pollPS(); }
  for (uint8_t n = 0; n < 64 && Serial.available(); ++n) {
    char ch = char(Serial.read());
    if (ch == '\n' || ch == '\r') command();
    else if (!overflow) {
      if (used < sizeof(line)-1 && ch >= 32 && ch <= 126) line[used++] = ch;
      else { overflow = true; stopAll(); }
    }
  }
  if (uint32_t(millis()-lastPrint) >= 500 && Serial.availableForWrite() > 220) {
    lastPrint = millis(); printStatus();
  }
  if (watchdog) esp_task_wdt_reset_user(watchdog);
  delay(1);
}
