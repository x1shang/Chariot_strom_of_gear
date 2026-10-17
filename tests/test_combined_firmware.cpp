#include <Arduino.h>
#include <assert.h>
#include <string.h>
uint32_t simulatedMillis = 1000;
int simulatedPins[40] = {}, simulatedDuty[40] = {};
SimulatedSerial Serial;
#include "../firmware/chassis/MecanumPS2Flywheel/MecanumPS2Flywheel.ino"

void receive(uint16_t buttons, uint8_t mode = 0x41, uint8_t marker = 0x5A) {
  simulatedMillis += 20;
  pad.frame(mode, marker, uint16_t(~buttons), millis());
  tickDrive();
}
void neutral() { for (int i = 0; i < 5; ++i) receive(0); assert(pad.armed); }
void idle() {
  assert(simulatedPins[STBY] == LOW);
  for (int i = 0; i < 4; ++i) {
    assert(wheels[i].value == 0);
    assert(simulatedDuty[PINS[i][0]] == 0 && simulatedDuty[PINS[i][1]] == 0);
  }
}
void outputs(const int expected[4]) {
  assert(simulatedPins[STBY] == HIGH);
  for (int i = 0; i < 4; ++i) {
    assert(wheels[i].value == expected[i]);
    assert(simulatedDuty[PINS[i][0]] == (expected[i] > 0 ? expected[i] : 0));
    assert(simulatedDuty[PINS[i][1]] == (expected[i] < 0 ? -expected[i] : 0));
  }
}
void hold(uint16_t buttons) { for (int i = 0; i < 150; ++i) receive(buttons); }
void send(const char *request) {
  strcpy(line, request); used = strlen(request); overflow = false; command();
}
void stick(uint8_t rx, uint16_t buttons = 0, uint8_t mode = 0x73) {
  simulatedMillis += 20;
  pad.frame(mode, 0x5A, uint16_t(~buttons), millis(), rx); tickDrive();
}
void center() { for (int i=0; i<5; ++i) stick(128); assert(pad.armed); }
int main() {
  setup(); assert(hardwareOK); idle();
  hold(PAD_R2); assert(!firing); assert(!pad.armed);
  neutral(); receive(PAD_R2);
  assert(firing && simulatedPins[FW1_EN] == HIGH && simulatedPins[FW2_EN] == HIGH);
  assert(simulatedPins[FW1_IN1] == HIGH && simulatedPins[FW1_IN2] == LOW);
  assert(simulatedPins[FW2_IN3] == HIGH && simulatedPins[FW2_IN4] == LOW);
  idle(); // R2 alone never moves the chassis.
  hold(PAD_R2 | PAD_UP); assert(firing && wheels[0].value == 255);
  receive(PAD_UP); assert(!firing && wheels[0].value == 255);
  assert(simulatedPins[FW1_EN] == LOW && simulatedPins[FW2_EN] == LOW);
  receive(PAD_R2); idle(); assert(firing);
  receive(PAD_CIRCLE | PAD_R2); assert(!firing); idle();
  hold(PAD_R2); assert(!firing); neutral(); receive(PAD_R2);
  send("STOP"); assert(!firing); hold(PAD_R2); assert(!firing);
  neutral(); receive(PAD_R2); receive(PAD_R2, 0xFF); assert(!firing);
  neutral(); receive(PAD_R2); simulatedMillis += 150; tickDrive(); assert(!firing);
  hold(PAD_R2); assert(!firing); neutral(); receive(PAD_R2);
  hardwareOK = false; tickDrive(); assert(!firing); hardwareOK = true;
  neutral(); receive(PAD_R2); send("INVALID"); assert(!firing);
  neutral(); receive(PAD_R2); overflow=true; used=0; command(); assert(!firing);
  // Held keys at boot cannot start motion, including after partial neutral.
  hold(PAD_UP); assert(!pad.armed); idle();
  for (int i = 0; i < 4; ++i) receive(0);
  assert(!pad.armed); receive(PAD_UP); idle(); neutral();
  // Check actual output GPIO duty and the calibrated right-side inversion.
  const uint16_t keys[] = {PAD_UP, PAD_DOWN, PAD_LEFT, PAD_RIGHT,
    uint16_t(PAD_UP | PAD_RIGHT), uint16_t(PAD_DOWN | PAD_LEFT)};
  const int expected[][4] = {{255,255,-255,-255}, {-255,-255,255,255},
    {-255,255,-255,255}, {255,-255,255,-255}, {255,0,0,-255}, {-255,0,0,255}};
  for (int n = 0; n < 6; ++n) {
    hold(keys[n]); outputs(expected[n]); receive(0); idle();
  }
  hold(PAD_UP); receive(PAD_UP | PAD_DOWN); idle();
  receive(PAD_LEFT | PAD_RIGHT); idle();
  receive(1u << 10); idle(); // L1 alone cannot cause motion.
  // Emergency stop and serial STOP require a full neutral handshake.
  hold(PAD_UP); receive(PAD_CIRCLE | PAD_UP); idle(); assert(!pad.armed);
  hold(PAD_UP); idle(); neutral(); hold(PAD_UP);
  send("STOP"); idle(); hold(PAD_UP); idle(); neutral();
  // Invalid marker/mode and a stale link disable output and prevent restart.
  hold(PAD_UP); receive(PAD_UP, 0x41, 0); idle(); assert(!pad.armed);
  hold(PAD_UP); idle(); neutral(); hold(PAD_UP);
  receive(PAD_UP, 0xFF); idle(); neutral(); hold(PAD_UP);
  simulatedMillis = pad.lastGood + 150; tickDrive(); idle(); assert(!pad.armed);
  hold(PAD_UP); idle(); neutral(); hold(PAD_UP);
  // Even if a new valid frame arrives before tickDrive, a 150ms gap locks.
  simulatedMillis += 150;
  pad.frame(0x41, 0x5A, uint16_t(~PAD_UP), millis()); tickDrive(); idle();
  neutral(); hold(PAD_UP); hardwareOK = false; tickDrive(); idle(); hardwareOK = true;
  neutral(); hold(PAD_UP); send("INVALID"); idle(); assert(!pad.armed);
  neutral(); hold(PAD_UP); overflow = true; used = 0; command(); idle();
  // Digital, analog and pressure modes share the same direction bit mapping.
  for (uint8_t mode : {uint8_t(0x41), uint8_t(0x73), uint8_t(0x79)}) {
    for (int i = 0; i < 5; ++i) receive(0, mode);
    assert(pad.armed); receive(PAD_UP, mode); assert(wheels[0].value > 0);
    receive(0, mode); idle();
  }
  // Reversal never crosses sign without zero and a 300ms wait.
  WheelRamp ramp;
  for (int i = 0; i < 85; ++i) ramp.step(255, 1000 + i * 10);
  assert(ramp.value == 255);
  uint32_t now = 1850;
  while (ramp.value) { assert(ramp.step(-255, now) >= 0); now += 10; }
  uint32_t zero = ramp.zeroAt;
  assert(ramp.step(-255, zero + 299) == 0);
  assert(ramp.step(-255, zero + 300) == -3);
  ramp.stop(zero + 301);
  assert(ramp.step(255, zero + 600) == 0);
  assert(ramp.step(255, zero + 601) == 3);
  // Unsigned elapsed checks work over millis() wraparound.
  pad = DpadControl{}; simulatedMillis = 0xFFFFFF80u;
  neutral(); hold(PAD_UP); outputs(expected[0]); receive(0); idle();
  simulatedMillis = pad.lastGood + 150; tickDrive(); idle(); assert(!pad.armed);
  // Analog transition/held stick cannot auto-start; full lock includes R2.
  for (int i=0; i<10; ++i) stick(255, PAD_R2);
  assert(!pad.armed && !firing); idle(); center();
  for (int i=0; i<150; ++i) stick(255);
  const int clockwise[4] = {255,255,255,255}; outputs(clockwise);
  stick(128); idle();
  for (int i=0; i<150; ++i) stick(0);
  const int counterclockwise[4] = {-255,-255,-255,-255}; outputs(counterclockwise);
  stick(128); idle();
  for (int rx=108; rx<=148; ++rx) { stick(uint8_t(rx)); idle(); }
  for (int i=0; i<150; ++i) stick(202);
  const int partial[4] = {128,128,128,128}; outputs(partial);
  stick(128); idle();
  for (int i=0; i<150; ++i) stick(255, PAD_UP | PAD_R2);
  const int curve[4] = {255,255,0,0}; outputs(curve); assert(firing);
  stick(128, PAD_R2); idle(); assert(firing);
  stick(255, PAD_CIRCLE | PAD_R2); idle(); assert(!firing && !pad.armed);
  for (int i=0; i<10; ++i) stick(255); idle(); assert(!pad.armed);
  center(); stick(255); send("STOP"); idle();
  stick(255); idle(); center(); stick(255);
  simulatedMillis += 150; tickDrive(); idle(); stick(255); idle();
  center(); stick(255); receive(0, 0xFF); idle(); assert(!firing);
  center(); stick(255); stick(255, 0, 0x41); idle(); assert(!pad.armed && pad.turn == 0);
  // Digital frames must ignore padding bytes; pressure mode accepts RX.
  for (int i=0; i<5; ++i) stick(0, 0, 0x41);
  assert(pad.armed && pad.turn == 0); idle();
  for (int i=0; i<5; ++i) stick(128, 0, 0x79);
  stick(255, 0, 0x79); assert(pad.turn == 255); stick(128, 0, 0x79); idle();
  puts("PASS: analog steering, dead zone, proportional/combined motion, centered rearm, mode transitions; original regression below");
  puts("PASS: D-pad mapping, GPIO polarity, release, neutral rearm, emergency/serial stops, invalid/stale link, reversal and timer wraparound");
}
