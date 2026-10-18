#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdarg.h>
#include <string>
#include <vector>
#define LOW 0
#define HIGH 1
#define OUTPUT 1
#define INPUT_PULLUP 2
extern uint32_t simulatedMillis;
extern int simulatedPins[40], simulatedDuty[40];
inline int failAttach=-1, failWrite=-1, failFreq=-1;
inline bool fullOnReadback=true;
inline bool frequencyApiZero=false;
inline uint8_t bitsByPin[40]={}, channelByPin[40]={}, timerByPin[40]={};
inline uint32_t frequencyByPin[40]={};
inline std::vector<uint8_t> transmitted;
inline uint8_t wireBits=0,wireByte=0;
inline uint32_t millis() { return simulatedMillis; }
inline void pinMode(uint8_t,int) {}
inline void digitalWrite(uint8_t p,int v) {
  simulatedPins[p]=v;
  if(p==4 && v==LOW) { wireBits=0; wireByte=0; }
  if(p==2 && v==HIGH && simulatedPins[4]==LOW) {
    wireByte|=uint8_t(simulatedPins[12]!=0)<<wireBits;
    if(++wireBits==8) { transmitted.push_back(wireByte); wireBits=0; wireByte=0; }
  }
}
inline int digitalRead(uint8_t p) { return simulatedPins[p]; }
inline void delayMicroseconds(unsigned) {}
inline void delay(unsigned ms) { simulatedMillis+=ms; }
inline bool ledcAttachChannel(uint8_t p,uint32_t f,uint8_t b,uint8_t c) {
  if(p==failAttach) return false;
  bool taken[4]={}; int timer=-1;
  for(unsigned i=0;i<40;++i) if(frequencyByPin[i] && channelByPin[i]/8==c/8) {
    taken[timerByPin[i]]=true;
    if(frequencyByPin[i]==f && bitsByPin[i]==b) timer=timerByPin[i];
  }
  if(timer<0) for(int i=0;i<4;++i) if(!taken[i]) { timer=i; break; }
  if(timer<0) return false;
  timerByPin[p]=uint8_t(timer);
  bitsByPin[p]=b; frequencyByPin[p]=f; channelByPin[p]=c; return true;
}
inline bool ledcWrite(uint8_t p,uint32_t d) {
  if(p==failWrite) return false;
  const unsigned max=bitsByPin[p]?(1u<<bitsByPin[p])-1:255;
  simulatedDuty[p]=int(fullOnReadback && bitsByPin[p] && d==max?max+1:d); return true;
}
inline unsigned ledcRead(uint8_t p) { return unsigned(simulatedDuty[p]); }
// Match installed Arduino-ESP32 3.3.12: frequency reads as zero at zero duty.
inline uint32_t ledcReadFreq(uint8_t p) {
  if(frequencyApiZero || !simulatedDuty[p]) return 0;
  return p==failFreq?frequencyByPin[p]+1000:frequencyByPin[p];
}
inline bool ledcDetach(uint8_t p) { frequencyByPin[p]=0; simulatedDuty[p]=0; return true; }
struct SimulatedSerial {
  std::string output,input;
  void begin(unsigned) {}
  void println(const char *s) { output+=s; output+='\n'; }
  void printf(const char *f,...) {
    char b[600]; va_list args; va_start(args,f); vsnprintf(b,sizeof(b),f,args); va_end(args); output+=b;
  }
  int available() { return int(input.size()); }
  int availableForWrite() { return 512; }
  int read() { if(input.empty()) return -1; char c=input[0]; input.erase(0,1); return c; }
};
extern SimulatedSerial Serial;
