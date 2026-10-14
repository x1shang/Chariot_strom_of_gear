#include <Arduino.h>
#include <assert.h>
#include <string.h>
uint32_t simulatedMillis = 1000;
int simulatedPins[40] = {}, simulatedDuty[40] = {};
SimulatedSerial Serial;
#include "../firmware/chassis/PS2SignalCheck/PS2SignalCheck.ino"
void stopped() { assert(outputsDisabled()); }
void frame(uint8_t mode, uint8_t marker, uint16_t pressed) {
  uint16_t released = uint16_t(~pressed);
  uint8_t rx[21] = {0xFF, mode, marker, uint8_t(released), uint8_t(released>>8)};
  acceptFrame(rx, mode == 0x79 ? 21 : 9); stopped();
}
void send(const char *s) { strcpy(line,s); used=strlen(s); overflow=false; command(); stopped(); }
int main() {
  setup(); stopped(); assert(hardwareOK && !fresh());
  frame(0x41,0x5A,0); assert(fresh() && validFrames==1);
  for (uint16_t b : {uint16_t(0x10),uint16_t(0x20),uint16_t(0x40),uint16_t(0x80),uint16_t(0x2000)}) {
    frame(0x41,0x5A,b); assert(buttons==b); stopped();
    frame(0x41,0x5A,0); assert(buttons==0); stopped();
  }
  frame(0x73,0x5A,0xFFFF); assert(valid && buttons==0xFFFF);
  frame(0x79,0x5A,0); assert(valid && rawLength==21);
  frame(0xFF,0xFF,0); assert(!fresh() && invalidFrames==1);
  frame(0x41,0x00,0); assert(!fresh() && invalidFrames==2);
  for (const char *s : {"STATUS","RAW","STOP","RUN","CCW10S","FWD3S","M 1 255"}) send(s);
  overflow=true; used=0; command(); stopped();
  simulatedMillis=0xFFFFFFF0u; frame(0x41,0x5A,0);
  simulatedMillis=lastValidAt+149; assert(fresh());
  simulatedMillis=lastValidAt+150; assert(!fresh());
  simulatedPins[ENA]=HIGH; checkDisabled(); assert(!hardwareOK); stopped();
  setup(); stopped(); assert(!fresh() && validFrames==0);
  puts("PASS: PS2 valid/invalid modes, button decode/release, counters, timeout/wrap; all motor commands refused, all outputs LOW");
}
