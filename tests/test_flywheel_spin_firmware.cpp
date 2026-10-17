// Host-side simulation test for the dual-130 friction-flywheel spin firmware.
// Compile: g++ -std=c++17 -Wall -Wextra -Itests/firmware-stubs tests/test_flywheel_spin_firmware.cpp -o build/test_flywheel_spin_firmware.exe
#include <Arduino.h>
#include <assert.h>
#include <string.h>
uint32_t simulatedMillis = 1000;
int simulatedPins[40] = {}, simulatedDuty[40] = {};
SimulatedSerial Serial;
#include "../firmware/catapult/FlywheelSpinTest/FlywheelSpinTest.ino"

const uint8_t PC30 = 77, PC60 = 153;  // raw 8-bit duty for 30% and 60%
int checkCalls = 0;

void check() {
  ++checkCalls;
  if (!outputsExpected()) {
    printf("FAIL check #%d phase=%s active=%d hw=%d used=%d dirs=%d d1=%d d2=%d applied=%u "
           "DUTY1=%u DUTY2=%u IN1=%d IN2=%d IN3=%d IN4=%d STBY=%d\n",
           checkCalls, phaseName(), int(active()), int(hardwareOK), int(used), int(directionsSet),
           direction1, direction2, unsigned(appliedDuty), unsigned(simulatedDuty[FW1_EN]),
           unsigned(simulatedDuty[FW2_EN]), simulatedPins[FW1_IN1], simulatedPins[FW1_IN2],
           simulatedPins[FW2_IN3], simulatedPins[FW2_IN4], simulatedPins[CHASSIS_STBY]);
    exit(1);
  }
  assert(simulatedPins[CHASSIS_STBY] == LOW);
  assert(simulatedDuty[FW1_EN] == simulatedDuty[FW2_EN]);  // both flywheels share one duty
}
void idle() { assert(!active()); check(); assert(simulatedDuty[FW1_EN] == 0); }
void send(const char *s) {
  strcpy(line, s); lineUsed = strlen(s); overflow = false; command(); check();
}
// A rejected command must never move: no spin started, nothing left enabled.
void rejected(const char *s) {
  const bool wasUsed = used;
  strcpy(line, s); lineUsed = strlen(s); overflow = false; command();
  assert(used == wasUsed && !active()); check(); assert(simulatedDuty[FW1_EN] == 0);
}
void armed() {  // booted, polarity chosen, still fully idle: SPIN is allowed
  setup(); idle(); assert(hardwareOK && !used && !directionsSet);
  send("DIR 1 1");
  assert(directionsSet && !used && phase == Phase::IDLE);
}
void start(const char *dir = "DIR 1 1", const char *spin = "SPIN 30 3000") {
  armed(); send(dir); send(spin);
  assert(phase == Phase::RAMP && used && active());
}
int main() {
  // Boot: idle, no motion, no polarity assumed, both bridges disabled at the pad.
  setup(); idle();
  assert(hardwareOK && !used && !directionsSet && direction1 == 1 && direction2 == 1);
  assert(simulatedPins[FW1_IN1] == LOW && simulatedPins[FW1_IN2] == LOW);
  assert(simulatedPins[FW2_IN3] == LOW && simulatedPins[FW2_IN4] == LOW);
  assert(targetPercent == 30 && targetMs == 3000);

  // Nothing moves without an explicit polarity and an explicit SPIN.
  rejected("SPIN");
  rejected("SPIN 30 3000");
  rejected("DIR 5 1");
  rejected("DIR 1");
  send("STOP"); send("STATUS"); idle(); assert(!used);
  armed(); assert(!used);

  // Duty and duration are hard caps; a refused command costs nothing and moves nothing.
  armed(); rejected("SPIN 0 3000");
  armed(); rejected("SPIN 61 3000");
  armed(); rejected("SPIN 100 3000");
  armed(); rejected("SPIN -1 3000");
  armed(); rejected("SPIN 30 0");
  armed(); rejected("SPIN 30 10001");
  armed(); rejected("SPIN 30 3000 x");
  armed(); rejected("SPIN 30 3000 5");

  // All four polarity combinations parse; the spin then ramps up to the target duty.
  for (const char *dir : {"DIR 1 1", "DIR 1 -1", "DIR -1 1", "DIR -1 -1"}) {
    start(dir);
    assert(direction1 == (dir[4] == '-' ? -1 : 1));
    assert(direction2 == (dir[strlen(dir) - 2] == '-' ? -1 : 1));
    assert(simulatedPins[FW1_IN1] == (direction1 > 0 ? HIGH : LOW));
    assert(simulatedPins[FW1_IN2] == (direction1 < 0 ? HIGH : LOW));
    assert(simulatedPins[FW2_IN3] == (direction2 > 0 ? HIGH : LOW));
    assert(simulatedPins[FW2_IN4] == (direction2 < 0 ? HIGH : LOW));
    const uint32_t t0 = startedAt;
    // Soft start: duty climbs linearly from 0 and never jumps straight to the target.
    simulatedMillis = t0; tickSpin(); assert(phase == Phase::RAMP && simulatedDuty[FW1_EN] == 0);
    simulatedMillis = t0 + 300; tickSpin(); assert(simulatedDuty[FW1_EN] == PC30 * 300 / 1200);
    simulatedMillis = t0 + 600; tickSpin(); assert(simulatedDuty[FW1_EN] == PC30 * 600 / 1200);
    simulatedMillis = t0 + 1199; tickSpin(); assert(simulatedDuty[FW1_EN] < PC30);
    simulatedMillis = t0 + 1200; tickSpin();
    assert(phase == Phase::HOLD && simulatedDuty[FW1_EN] == PC30);  // both at the target duty
    check();
    simulatedMillis = t0 + 2999; tickSpin(); assert(phase == Phase::HOLD && active());
    simulatedMillis = t0 + 3000; tickSpin(); assert(phase == Phase::DONE && !active());
    assert(elapsedMs == 3000);
    idle();
    send("SPIN 30 3000"); idle(); assert(used);  // once per boot, no retry
  }

  // 60% / 10s are accepted as the extremes; the same run keeps the target duty.
  start("DIR 1 1", "SPIN 60 10000");
  assert(targetDuty == PC60);
  simulatedMillis = startedAt + 1200; tickSpin();
  assert(phase == Phase::HOLD && simulatedDuty[FW1_EN] == PC60);

  // STOP wins during the ramp and during the hold; the flywheel then coasts.
  start(); simulatedMillis += 500; send("STOP");
  assert(phase == Phase::STOPPED && elapsedMs == 500); idle();
  start(); simulatedMillis = startedAt + 1500; tickSpin(); assert(phase == Phase::HOLD);
  simulatedMillis += 123; send("STOP"); idle(); assert(phase == Phase::STOPPED);
  send("SPIN 30 3000"); idle(); assert(used);

  // Reconfiguration, invalid input and a second SPIN during a spin stop it instead
  // of letting it run on.
  start(); send("DIR -1 -1"); idle(); assert(phase == Phase::STOPPED);
  start(); send("INVALID"); idle(); assert(phase == Phase::STOPPED);
  start(); send("SPIN 60 1000"); idle(); assert(phase == Phase::STOPPED);
  start(); overflow = true; lineUsed = 0; command(); idle(); assert(phase == Phase::STOPPED);

  // Interlocks: any mismatch between commanded and read-back state kills the outputs.
  start(); simulatedMillis = startedAt + 1200; tickSpin();
  assert(phase == Phase::HOLD && appliedDuty == PC30);
  simulatedDuty[FW1_EN] = 0; tickSpin(); idle(); assert(!hardwareOK);  // PWM readback
  start(); simulatedDuty[FW2_EN] = 200; tickSpin(); idle(); assert(!hardwareOK);  // one-sided
  start(); simulatedPins[FW2_IN3] = LOW; tickSpin(); idle(); assert(!hardwareOK);  // direction pin
  start(); simulatedPins[CHASSIS_STBY] = HIGH; tickSpin(); idle(); assert(!hardwareOK);
  setup(); simulatedPins[FW1_IN2] = HIGH; tickSpin(); idle(); assert(!hardwareOK);
  start(); hardwareOK = false; tickSpin(); idle();

  // An idle enable pin held high (left-in jumper / external pull-up) is refused, not
  // driven: the firmware faults and commands duty 0 instead of reporting a spin it
  // cannot verify. The pad itself stays high because the external source wins, so the
  // pad-level check only clears once that external source is removed.
  setup(); simulatedPins[FW1_EN] = HIGH; tickSpin();
  assert(!hardwareOK && phase == Phase::FAULT);
  assert(simulatedDuty[FW1_EN] == 0 && simulatedDuty[FW2_EN] == 0);
  simulatedPins[FW1_EN] = LOW;  // operator removes the jumper
  idle();
  setup(); idle(); assert(hardwareOK);

  // Millisecond counter wrap: the ramp, the hold and the deadline still land correctly.
  simulatedMillis = 0xFFFFFF00u; start();
  simulatedMillis = startedAt + 600; tickSpin();
  assert(phase == Phase::RAMP && simulatedDuty[FW1_EN] == PC30 * 600 / 1200);
  simulatedMillis = startedAt + 1200; tickSpin(); assert(phase == Phase::HOLD);
  simulatedMillis = startedAt + 3000; tickSpin(); idle();
  assert(phase == Phase::DONE && elapsedMs == 3000);

  // A requested duration shorter than the soft-start ramp still ends when the ramp
  // completes, and a wrap during the run neither shortens nor extends it.
  simulatedMillis = 0xFFFFFFF0u; start("DIR 1 1", "SPIN 30 1000");
  simulatedMillis = startedAt + 900; tickSpin();
  assert(phase == Phase::RAMP && elapsedMs == 0);
  simulatedMillis = startedAt + 1200; tickSpin(); idle();
  assert(phase == Phase::DONE && elapsedMs == 1200);

  // A fresh boot forgets everything: no motion, no remembered polarity, limits restored.
  setup(); idle();
  assert(!used && !directionsSet && targetPercent == 30 && targetMs == 3000 && elapsedMs == 0);
  printf("PASS: dual 130 flywheel soft start (linear 0->target duty ramp, 5kHz PWM), duty/ms hard "
         "caps, both flywheels on one duty, once per boot, STOP priority in ramp and hold, "
         "PWM/pin readback interlocks, idle enable-jumper refusal, timer wrap, idle boot (%d checks)\n",
         checkCalls);
}
