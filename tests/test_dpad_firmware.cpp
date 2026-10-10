#include <Arduino.h>
#include <assert.h>
#include <string.h>
uint32_t simulatedMillis = 1000;
int simulatedPins[40] = {}, simulatedDuty[40] = {};
SimulatedSerial Serial;
#include "../firmware/chassis/MecanumPS2Dpad/MecanumPS2Dpad.ino"

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
int main() {
  setup(); assert(hardwareOK); idle();
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
  puts("PASS: D-pad mapping, GPIO polarity, release, neutral rearm, emergency/serial stops, invalid/stale link, reversal and timer wraparound");
}
