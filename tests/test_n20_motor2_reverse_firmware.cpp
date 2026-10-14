#include <Arduino.h>
#include <assert.h>
#include <string.h>
uint32_t simulatedMillis = 1000;
int simulatedPins[40] = {}, simulatedDuty[40] = {};
SimulatedSerial Serial;
#include "../firmware/catapult/N20Motor2Reverse10s/N20Motor2Reverse10s.ino"

void noRelease() {
  assert(simulatedPins[MOTOR1_EN] == LOW);
  assert(simulatedPins[MOTOR1_IN1] == LOW && simulatedPins[MOTOR1_IN2] == LOW);
  assert(simulatedPins[CHASSIS_STBY] == LOW);
}
void idle() {
  assert(!running);
  assert(simulatedPins[MOTOR2_EN] == LOW);
  assert(simulatedPins[MOTOR2_IN3] == LOW && simulatedPins[MOTOR2_IN4] == LOW);
  noRelease();
}
void send(const char *text) {
  strcpy(line, text); lineUsed = strlen(text); overflow = false; command();
  noRelease();
}
int main() {
  setup(); assert(hardwareOK && !used); idle();
  send("STOP"); assert(!used); idle();
  send("STATUS"); assert(!used); idle();
  send("CCW10S"); assert(used && running);
  assert(simulatedPins[MOTOR2_EN] == HIGH);
  assert(simulatedPins[MOTOR2_IN3] == LOW && simulatedPins[MOTOR2_IN4] == HIGH);
  uint32_t start = startedAt;
  simulatedMillis = start + 9999; tickMotor2(); assert(running); noRelease();
  simulatedMillis = start + 10000; tickMotor2(); idle(); assert(elapsedMs == 10000);
  send("CCW10S"); idle(); // No accidental duplicate can restart.
  setup(); send("CCW10S"); simulatedMillis += 123; send("STOP"); idle();
  assert(used && elapsedMs == 123); send("CCW10S"); idle();
  setup(); hardwareOK = false; send("CCW10S"); assert(!used); idle();
  setup(); send("CCW10S"); send("REVERSE"); idle();
  setup(); send("CCW10S"); overflow = true; lineUsed = 0; command(); idle();
  setup(); send("CCW10S"); hardwareOK = false; tickMotor2(); idle();
  setup(); send("CCW10S"); simulatedPins[MOTOR1_EN] = HIGH;
  tickMotor2(); idle(); assert(!hardwareOK); send("CCW10S"); idle();
  setup(); simulatedPins[MOTOR2_EN] = HIGH;
  tickMotor2(); idle(); assert(!hardwareOK && !used); send("CCW10S"); idle();
  simulatedMillis = 0xFFFFFF80u; setup(); send("CCW10S");
  simulatedMillis = startedAt + 9999; tickMotor2(); assert(running); noRelease();
  simulatedMillis = startedAt + 10000; tickMotor2(); idle(); assert(elapsedMs == 10000);
  setup(); idle(); assert(!used); // Reboot never starts a pulse by itself.
  puts("PASS: motor2 counterclockwise pins, exact 10s expiry/wrap, STOP, duplicate rejection, idle boot, guards; motor1/chassis always disabled");
}
