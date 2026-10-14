#include <Arduino.h>
#include <assert.h>
#include <string.h>
uint32_t simulatedMillis = 1000;
int simulatedPins[40] = {}, simulatedDuty[40] = {};
SimulatedSerial Serial;
#include "../firmware/catapult/N20SequenceTest/N20SequenceTest.ino"

void check() {
  assert(outputsExpected());
  assert(simulatedPins[CHASSIS_STBY] == LOW);
  assert(!(simulatedPins[MOTOR1_EN] && simulatedPins[MOTOR2_EN]));
}
void idle() { assert(!active()); check(); }
void send(const char *s) {
  strcpy(line, s); lineUsed = strlen(s); overflow = false; command(); check();
}
void start(const char *dir = "DIR 1 1") { setup(); idle(); send(dir); send("RUN"); assert(phase == Phase::MOTOR2 && used); }
int main() {
  setup(); idle(); assert(hardwareOK && !used && !directionsSet);
  send("RUN"); idle(); assert(!used);
  send("STOP"); send("STATUS"); assert(!used);
  for (const char *dir : {"DIR 1 1", "DIR 1 -1", "DIR -1 1", "DIR -1 -1"}) {
    start(dir);
    assert(direction1 == (dir[4] == '-' ? -1 : 1));
    assert(direction2 == (dir[strlen(dir)-2] == '-' ? -1 : 1));
    uint32_t m2start = startedAt;
    simulatedMillis = m2start + 2999; tickSequence(); assert(phase == Phase::MOTOR2); check();
    simulatedMillis = m2start + 3000; tickSequence(); assert(phase == Phase::MOTOR1); check();
    assert(motor2Elapsed == 3000 && simulatedPins[MOTOR2_EN] == LOW);
    uint32_t m1start = startedAt;
    simulatedMillis = m1start + 499; tickSequence(); assert(phase == Phase::MOTOR1); check();
    simulatedMillis = m1start + 500; tickSequence(); assert(phase == Phase::DONE); idle();
    assert(motor1Elapsed == 500);
    send("RUN"); idle(); assert(used);
  }
  start(); simulatedMillis += 3000; send("STOP");
  idle(); assert(phase == Phase::STOPPED && motor1Elapsed == 0); // No release on boundary STOP.
  send("RUN"); idle();
  start(); simulatedMillis += 3000; tickSequence(); assert(phase == Phase::MOTOR1);
  simulatedMillis += 123; send("STOP"); idle(); assert(motor1Elapsed == 123);
  start(); send("DIR -1 1"); idle(); // Reconfiguration cannot leave a running output.
  start(); send("INVALID"); idle();
  start(); overflow = true; lineUsed = 0; command(); idle();
  start(); simulatedPins[MOTOR1_EN] = HIGH; tickSequence(); idle(); assert(!hardwareOK);
  start(); simulatedPins[MOTOR2_IN3] = LOW; tickSequence(); idle(); assert(!hardwareOK);
  setup(); simulatedPins[MOTOR2_EN] = HIGH; tickSequence(); idle(); assert(!hardwareOK);
  start(); hardwareOK = false; tickSequence(); idle();
  simulatedMillis = 0xFFFFFF00u; start();
  simulatedMillis = startedAt + 3000; tickSequence(); assert(phase == Phase::MOTOR1); check();
  simulatedMillis = startedAt + 500; tickSequence(); idle(); assert(phase == Phase::DONE);
  start(); simulatedMillis += 3100; tickSequence(); assert(phase == Phase::MOTOR1);
  assert(motor2Elapsed == 3100); simulatedMillis += 499; tickSequence(); assert(active());
  simulatedMillis += 1; tickSequence(); idle(); assert(motor1Elapsed == 500);
  setup(); idle(); assert(!used && !directionsSet); // No boot motion, no persistent retrigger.
  puts("PASS: 3s motor2 -> 500ms motor1; GPIO polarities, no overlap, STOP before release, faults, once-only, idle boot, timer wrap");
}
