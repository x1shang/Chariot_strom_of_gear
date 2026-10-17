#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdarg.h>
#include <string>
#define LOW 0
#define HIGH 1
#define OUTPUT 1
#define INPUT_PULLUP 2
extern uint32_t simulatedMillis;
extern int simulatedPins[40], simulatedDuty[40];
inline uint32_t millis() { return simulatedMillis; }
inline void pinMode(uint8_t,int) {}
inline void digitalWrite(uint8_t pin,int value) { simulatedPins[pin]=value; }
inline int digitalRead(uint8_t pin) { return simulatedPins[pin]; }
inline void delayMicroseconds(unsigned) {}
inline void delay(unsigned ms) { simulatedMillis+=ms; }
inline uint8_t simulatedLedcBits[40] = {};
// Opt in when testing the installed Arduino-ESP32 3.x full-on behavior.
inline bool simulatedFullOnReadback = false;
inline bool ledcAttachChannel(uint8_t pin,unsigned,uint8_t bits,uint8_t) {
  simulatedLedcBits[pin] = bits;
  return true;
}
inline bool ledcWrite(uint8_t pin,unsigned duty) {
  // Arduino-ESP32 3.3.12 maps all bits set to the full-on register value.
  const unsigned maxDuty = (1u << simulatedLedcBits[pin]) - 1u;
  if (simulatedFullOnReadback && simulatedLedcBits[pin] && maxDuty != 1 && duty >= maxDuty) duty = maxDuty + 1u;
  simulatedDuty[pin] = int(duty);
  return true;
}
inline unsigned ledcRead(uint8_t pin) { return unsigned(simulatedDuty[pin]); }
struct SimulatedSerial {
  std::string output;
  void begin(unsigned) {}
  void println(const char *text) { output+=text; output+='\n'; }
  void printf(const char *format,...) {
    char buffer[512]; va_list args; va_start(args,format);
    vsnprintf(buffer,sizeof(buffer),format,args); va_end(args); output+=buffer;
  }
  int available() { return 0; }
  int availableForWrite() { return 512; }
  int read() { return -1; }
};
extern SimulatedSerial Serial;
