#include <Arduino.h>
#include <stdlib.h>
#include <string.h>
uint32_t simulatedMillis=1000;
int simulatedPins[40]={}, simulatedDuty[40]={};
SimulatedSerial Serial;
#include "../firmware/robot/ChariotIntegrated/ChariotIntegrated.ino"
int checks=0;
#define CHECK(x) do { ++checks; if(!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); exit(1); } } while(0)
void frame(uint16_t keys=0,unsigned dt=20,uint8_t mode=0x41,uint8_t marker=0x5A) {
  simulatedMillis+=dt; receiveFrame(mode,marker,uint16_t(~keys)); tickRobot();
}
void neutral() { for(int i=0;i<5;++i) frame(); CHECK(pad.armed); }
void cmd(const char *s) { strcpy(line,s); used=strlen(s); overflow=false; command(); }
void advance(unsigned ms,uint16_t keys=0) { for(unsigned i=0;i<ms/10;++i) frame(keys,10); }
void stick(uint8_t rx,uint16_t keys=0,uint8_t mode=0x73) {
  simulatedMillis+=20; receiveFrame(mode,0x5A,uint16_t(~keys),rx); tickRobot();
}
void center() { for(int i=0;i<5;++i) stick(128); CHECK(pad.armed); }
void stickHold(uint8_t rx,uint16_t keys=0) { for(int i=0;i<150;++i) stick(rx,keys); }
void wheelValues(int a,int b,int c,int d) {
  CHECK(wheels[0].value==a && wheels[1].value==b && wheels[2].value==c && wheels[3].value==d);
}
void motorIdle() {
  CHECK(!flyActive && !feedActive && flyDuty==0);
  CHECK(simulatedPins[Cfg::CHASSIS_STBY]==0 && simulatedPins[Cfg::FEED_STBY]==0);
  CHECK(simulatedDuty[14]==0 && simulatedDuty[25]==0 && simulatedDuty[26]==0);
  for(int i=0;i<4;++i) CHECK(wheels[i].value==0);
}
void arm() { neutral(); cmd("ARM"); CHECK(launcherArmed); }
void flyReadyHold() { frame(BTN_R2); CHECK(flyActive); advance(Cfg::FLY_READY_MS,BTN_R2); CHECK(flyReady()); }
int main() {
  setup(); CHECK(hardwareOK); CHECK(!servoEnabled && frequencyByPin[27]==0); motorIdle();
  CHECK(ledcReadFreq(14)==0 && ledcReadFreq(25)==0 && ledcReadFreq(16)==0);
  CHECK(pwmStopped(14,true,5000) && pwmStopped(25,true,20000)); // No nonzero boot pulse.
  CHECK(pwmFrequency(16)==20000 && pwmFrequency(14)==5000 && pwmFrequency(25)==20000);
  frequencyApiZero=true; // Duty-dependent convenience API cannot gate timer health.
  const uint8_t expectedConfig[]={1,0x43,0,1,0,1,0x44,0,1,3,0,0,0,0,1,0x43,0,0,0x5A,0x5A,0x5A,0x5A,0x5A};
  CHECK(transmitted.size()==sizeof(expectedConfig));
  for(size_t i=0;i<sizeof(expectedConfig);++i) CHECK(transmitted[i]==expectedConfig[i]);
  // Boot-held movement/actions cannot arm. Neutral must survive normal ticks.
  advance(400,PAD_UP|BTN_R2); CHECK(!pad.armed); motorIdle(); neutral(); CHECK(launcherArmed);
  frame(BTN_SQUARE); CHECK(!flyActive); frame();
  frame(BTN_START); CHECK(launcherArmed && !flyActive); frame();
  // Existing chassis polarity, release stop and reversal guard.
  advance(1000,PAD_UP); CHECK(wheels[0].value==255 && wheels[2].value==-255);
  frame(); motorIdle(); advance(1500,PAD_RIGHT); CHECK(wheels[0].value==255 && wheels[1].value==-255);
  frame(); motorIdle();
  const uint16_t directionKeys[]={PAD_UP,PAD_DOWN,PAD_LEFT,PAD_RIGHT,uint16_t(PAD_UP|PAD_RIGHT),uint16_t(PAD_DOWN|PAD_LEFT)};
  const int directionOutputs[][4]={{255,255,-255,-255},{-255,-255,255,255},{-255,255,-255,255},{255,-255,255,-255},{255,0,0,-255},{-255,0,0,255}};
  for(int i=0;i<6;++i) {
    advance(1800,directionKeys[i]); const int *v=directionOutputs[i]; wheelValues(v[0],v[1],v[2],v[3]);
    frame(); motorIdle();
  }
  CHECK(hardwareOK && !strcmp(faultReason,"none")); frequencyApiZero=false;
  WheelRamp r; for(unsigned i=0;i<85;++i) r.step(255,i*10);
  uint32_t rt=850; while(r.value) { CHECK(r.step(-255,rt)>=0); rt+=10; }
  CHECK(r.step(-255,r.zeroAt+299)==0); CHECK(r.step(-255,r.zeroAt+300)==-3);
  // Ramp is retained; too-early R1 is rejected and never queued.
  cmd("ARM"); frame(BTN_R2); CHECK(flyActive && flyDuty==0);
  advance(600,BTN_R2); CHECK(flyDuty>100 && flyDuty<150);
  frame(BTN_R2|BTN_R1); CHECK(!feedActive);
  advance(2000,BTN_R2|BTN_R1); CHECK(flyReady() && !feedActive);
  CHECK(simulatedDuty[14]==256); // Installed 3.3.12 full-on representation.
  frame(BTN_R2); frame(BTN_R2|BTN_R1);
  CHECK(feedActive && simulatedPins[Cfg::FEED_STBY]==1 && simulatedDuty[25]>0 && simulatedDuty[26]==0);
  advance(160,BTN_R2|BTN_R1); CHECK(!feedActive && simulatedPins[Cfg::FEED_STBY]==0);
  advance(1000,BTN_R2|BTN_R1); CHECK(!feedActive); // Hold does not repeat.
  frame(BTN_R2); frame(BTN_R2|BTN_R1); CHECK(feedActive);
  frame(); motorIdle(); // Releasing fly deadman cancels a partial feed.
  // Re-press after timeout is required, even with R2 still held.
  frame(BTN_R2); CHECK(flyActive); advance(Cfg::FLY_MAX_MS+100,BTN_R2);
  CHECK(!flyActive && flyTimedOut); advance(1000,BTN_R2); CHECK(!flyActive);
  frame(); frame(BTN_R2); CHECK(flyActive); frame();
  // Repository parity: driving and flywheel pre-spin can run together.
  flyReadyHold(); advance(1000,BTN_R2|PAD_UP);
  CHECK(flyActive && wheels[0].value==255);
  frame(BTN_R2|PAD_UP|BTN_R1); CHECK(!feedActive && flyActive);
  frame(BTN_R2); CHECK(wheelsIdle()); frame(BTN_R2|BTN_R1); CHECK(feedActive);
  frame(BTN_R2|PAD_UP); motorIdle(); CHECK(!launcherArmed && !pad.armed);
  advance(200,PAD_UP); motorIdle(); arm();
  // Invalid/stale communication clears all motor output and requires neutral.
  flyReadyHold(); frame(BTN_R2,20,0x41,0); motorIdle(); CHECK(!pad.armed);
  arm(); flyReadyHold(); simulatedMillis+=150; tickRobot(); motorIdle(); CHECK(!pad.armed);
  arm(); frame(BTN_R2); simulatedMillis+=150;
  receiveFrame(0x41,0x5A,uint16_t(~BTN_R2)); tickRobot(); motorIdle(); CHECK(!pad.armed);
  arm(); flyReadyHold(); frame(PAD_CIRCLE|BTN_R2); motorIdle(); CHECK(!launcherArmed);
  arm(); flyReadyHold(); cmd("STOP"); motorIdle(); CHECK(!pad.armed);
  // Explicit, time-bounded direction tests; no flywheel needed.
  arm(); cmd("JOG 1 100"); CHECK(feedActive && simulatedPins[Cfg::FEED_STBY]==1);
  advance(110); CHECK(!feedActive && simulatedPins[Cfg::FEED_STBY]==0);
  cmd("JOG -1 100"); CHECK(!feedActive); advance(Cfg::FEED_COOLDOWN_MS);
  cmd("JOG -1 100"); CHECK(feedActive && simulatedDuty[26]>0 && simulatedDuty[25]==0);
  advance(110); cmd("JOG 1 501"); CHECK(!feedActive); cmd("JOG 1 0"); CHECK(!feedActive);
  // All serial test runs remain limited and respond to fresh keys/STOP.
  advance(Cfg::FEED_COOLDOWN_MS); cmd("FLY 30 2000"); CHECK(flyActive && flySerial);
  advance(1500); CHECK(flyDuty==percentDuty(30)); frame(PAD_UP); motorIdle();
  arm(); cmd("FLY 30 100"); advance(110); motorIdle();
  cmd("FLY 101 1000"); CHECK(!flyActive); cmd("FLY 30 10001"); CHECK(!flyActive);
  cmd("FLY 30 1x"); CHECK(!flyActive); cmd("FLY 999999999999999999999 1"); CHECK(!flyActive);
  cmd("JOG 1 100 extra"); CHECK(!feedActive);
  // No servo is installed; no button or serial command may enable GPIO27 PWM.
  frame(); cmd("SERVO ON"); CHECK(!servoEnabled && frequencyByPin[27]==0);
  CHECK(channelByPin[14]==8 && channelByPin[25]==10 && channelByPin[26]==11);
  CHECK(frequencyByPin[14]==5000 && frequencyByPin[25]==20000);
  cmd("SERVO 1600"); CHECK(servoCurrent==1500 && servoTarget==1500);
  frame(BTN_SELECT); advance(100,BTN_SELECT); CHECK(!servoEnabled); frame();
  advance(1200,BTN_L1); CHECK(servoCurrent==1500 && frequencyByPin[27]==0); frame();
  frame(BTN_CROSS); CHECK(!feedActive); frame(); // Old CROSS control is removed.
  // R1 is a single timed pulse with R2 released, not a deadman or a queued move.
  advance(Cfg::FEED_COOLDOWN_MS); frame(BTN_R1);
  CHECK(feedActive && !flyActive && !feedNeedsFly && !feedJog && simulatedPins[Cfg::FEED_STBY]==1);
  frame(); CHECK(feedActive); // A tap continues for its fixed duration after release.
  advance(120); CHECK(feedActive); frame(0,10); CHECK(!feedActive); motorIdle();
  frame(BTN_R1); CHECK(!feedActive); // Cooldown rejects the edge.
  advance(1000,BTN_R1); CHECK(!feedActive); // Does not start later while held.
  frame(); frame(BTN_R1); CHECK(feedActive);
  advance(160,BTN_R1); CHECK(!feedActive); advance(1000,BTN_R1); CHECK(!feedActive);
  frame(); CHECK(!servoEnabled && frequencyByPin[27]==0);
  // Driving requests cannot move N20; stopping with R1 still held cannot queue it.
  advance(1000,PAD_UP); frame(PAD_UP|BTN_R1); CHECK(!feedActive && !wheelsIdle());
  frame(BTN_R1); CHECK(!feedActive && wheelsIdle()); advance(1000,BTN_R1); CHECK(!feedActive);
  frame(); frame(BTN_R1); CHECK(feedActive); frame(PAD_UP); motorIdle(); CHECK(!pad.armed);
  // Independent R1 pulses still obey STOP, CIRCLE and missing/invalid frames.
  for(int event=0;event<4;++event) {
    arm(); advance(Cfg::FEED_COOLDOWN_MS); frame(BTN_R1); CHECK(feedActive);
    if(event==0) cmd("STOP");
    if(event==1) frame(PAD_CIRCLE);
    if(event==2) { simulatedMillis+=150; tickRobot(); }
    if(event==3) frame(0,20,0x41,0);
    motorIdle(); CHECK(!pad.armed);
    advance(200,BTN_R1); motorIdle(); CHECK(!pad.armed);
  }
  arm(); advance(Cfg::FEED_COOLDOWN_MS);
  // Error paths and input buffer overflow cannot leave energized outputs.
  arm(); cmd("JOG 1 100"); cmd("UNKNOWN"); motorIdle(); CHECK(!pad.armed);
  arm(); flyReadyHold(); overflow=true; used=0; command(); motorIdle();
  // Idle-only analog negotiation retains the repository's configuration bytes.
  neutral(); lastConfigAttempt=millis()-2000; size_t sentBefore=transmitted.size();
  maybeConfigureAnalog(); CHECK(transmitted.size()==sentBefore+23 && !pad.armed);
  neutral(); frame(BTN_R2); lastConfigAttempt=millis()-2000; sentBefore=transmitted.size();
  maybeConfigureAnalog(); CHECK(transmitted.size()==sentBefore && flyActive); frame();
  // Analog mode changes and deflected sticks cannot synthesize neutral/rearm.
  for(int i=0;i<10;++i) stick(255,BTN_R2); CHECK(!pad.armed && !flyActive); motorIdle();
  center(); CHECK(launcherArmed && pad.analog); stickHold(255); wheelValues(255,255,255,255);
  stick(128); motorIdle(); stickHold(0); wheelValues(-255,-255,-255,-255); stick(128); motorIdle();
  for(int rx=108;rx<=148;++rx) { stick(uint8_t(rx)); CHECK(pad.turn==0 && wheelsIdle()); }
  stickHold(202); wheelValues(128,128,128,128); stick(128); motorIdle();
  stickHold(255,PAD_UP|BTN_R2); wheelValues(255,255,0,0); CHECK(flyActive);
  stick(128,BTN_R2); CHECK(wheelsIdle() && flyActive); stick(128); motorIdle();
  stick(255,PAD_CIRCLE|BTN_R2); motorIdle(); CHECK(!pad.armed);
  for(int i=0;i<10;++i) stick(255); CHECK(!pad.armed); center();
  stick(255); cmd("STOP"); motorIdle(); stick(255); CHECK(!pad.armed); center();
  stick(255); simulatedMillis+=150; tickRobot(); motorIdle(); stick(255); CHECK(!pad.armed); center();
  stick(255); stick(255,0,0x41); motorIdle(); CHECK(!pad.armed && pad.turn==0 && !pad.analog);
  for(int i=0;i<5;++i) stick(0,0,0x41); CHECK(pad.armed && pad.rightX==128 && pad.turn==0);
  for(int i=0;i<5;++i) stick(128,0,0x79); CHECK(pad.armed && pad.analog);
  stick(255,0,0x79); CHECK(pad.turn==255); stick(128,0,0x79); CHECK(wheelsIdle());
  // New actuator commands must treat a non-centered stick as non-neutral.
  stick(255); cmd("ARM"); CHECK(Serial.output.find("ERR ARM")!=std::string::npos);
  cmd("JOG 1 100"); CHECK(!feedActive); const bool servoBefore=servoEnabled;
  cmd("SERVO ON"); CHECK(servoEnabled==servoBefore && Serial.output.find("ERR servo gate")!=std::string::npos);
  center(); cmd("FLY 30 3000"); CHECK(flySerial); stick(255); motorIdle(); CHECK(!pad.armed);
  neutral(); // Explicit return to digital mode for the remaining original tests.
  // Unsigned clock arithmetic remains valid over wraparound.
  simulatedMillis=0xFFFFFF00u; pad=DpadControl{}; previousButtons=pendingEdges=0; arm();
  frame(BTN_R1); CHECK(feedActive); frame(); advance(150); motorIdle();
  advance(Cfg::FEED_COOLDOWN_MS); frame(BTN_R2); advance(2600,BTN_R2); CHECK(flyReady()); frame(); motorIdle();
  // Runtime write failure gates STBY before a fault is latched.
  arm(); failWrite=25; cmd("JOG 1 100"); CHECK(!hardwareOK && !feedActive && simulatedPins[Cfg::FEED_STBY]==0);
  failWrite=-1; motorIdle();
  setup(); neutral(); cmd("ARM"); frame(BTN_R2); advance(500,BTN_R2);
  failWrite=14; cmd("STOP"); CHECK(!hardwareOK && !flyAttached && frequencyByPin[14]==0 && simulatedPins[14]==0);
  failWrite=-1; motorIdle();
  // Startup attach failure cannot be cleared by an ARM command.
  setup(); neutral(); advance(Cfg::FEED_COOLDOWN_MS); failFreq=25; frame(BTN_R1);
  CHECK(!hardwareOK && !feedActive && simulatedPins[Cfg::FEED_STBY]==0); failFreq=-1; motorIdle();
  setup(); neutral(); failFreq=14; frame(BTN_R2); advance(50,BTN_R2);
  CHECK(!hardwareOK && !flyActive); failFreq=-1; motorIdle();
  setup(); neutral(); failFreq=16; frame(PAD_UP);
  CHECK(!hardwareOK && simulatedPins[33]==0); failFreq=-1; motorIdle();
  failAttach=14; setup(); CHECK(!hardwareOK); for(int i=0;i<5;++i) frame();
  cmd("ARM"); CHECK(!launcherArmed); motorIdle();
  CHECK(Cfg::FEED_STBY==21);
  failAttach=-1; setup(); neutral(); failHighReadPin=Cfg::FEED_STBY; frame(BTN_R1);
  CHECK(!hardwareOK && !feedActive && !strcmp(faultReason,"feed_enable")); motorIdle();
  failHighReadPin=-1;
  printf("PASS: %d integrated firmware checks; no serial hardware or motors accessed\n",checks);
}
