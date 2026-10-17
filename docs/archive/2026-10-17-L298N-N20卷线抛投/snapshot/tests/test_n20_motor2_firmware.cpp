#include <Arduino.h>
#include <assert.h>
#include <string.h>
uint32_t simulatedMillis = 1000;
int simulatedPins[40] = {}, simulatedDuty[40] = {};
SimulatedSerial Serial;
#include "../firmware/catapult/N20Motor2DirectionCheck/N20Motor2DirectionCheck.ino"

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
  send("FWD3S"); assert(used && running);
  assert(simulatedPins[MOTOR2_EN] == HIGH);
  assert(simulatedPins[MOTOR2_IN3] == HIGH && simulatedPins[MOTOR2_IN4] == LOW);
  uint32_t start = startedAt;
  simulatedMillis = start + 2999; tickMotor2(); assert(running); noRelease();
  simulatedMillis = start + 3000; tickMotor2(); idle(); assert(elapsedMs == 3000);
  send("FWD3S"); idle(); // No accidental duplicate can restart.
  setup(); send("FWD3S"); simulatedMillis += 123; send("STOP"); idle();
  assert(used && elapsedMs == 123); send("FWD3S"); idle();
  setup(); hardwareOK = false; send("FWD3S"); assert(!used); idle();
  setup(); send("FWD3S"); send("REVERSE"); idle();
  setup(); send("FWD3S"); overflow = true; lineUsed = 0; command(); idle();
  setup(); send("FWD3S"); hardwareOK = false; tickMotor2(); idle();
  setup(); send("FWD3S"); simulatedPins[MOTOR1_EN] = HIGH;
  tickMotor2(); idle(); assert(!hardwareOK); send("FWD3S"); idle();
  setup(); simulatedPins[MOTOR2_EN] = HIGH;
  tickMotor2(); idle(); assert(!hardwareOK && !used); send("FWD3S"); idle();
  simulatedMillis = 0xFFFFFF80u; setup(); send("FWD3S");
  simulatedMillis = startedAt + 2999; tickMotor2(); assert(running); noRelease();
  simulatedMillis = startedAt + 3000; tickMotor2(); idle(); assert(elapsedMs == 3000);
  setup(); idle(); assert(!used); // Reboot never starts a pulse by itself.
  puts("PASS: motor2 forward pins, exact 3s expiry/wrap, STOP, duplicate rejection, idle boot, guards; motor1/chassis always disabled");
}
