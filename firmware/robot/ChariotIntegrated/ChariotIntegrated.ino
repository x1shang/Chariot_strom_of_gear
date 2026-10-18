// Classic ESP32, Arduino-ESP32 3.3.12. ONLY for the NEW wiring plan.
// Controls/analog negotiation adapted from repository MecanumPS2Flywheel-v2
// at 9a11db22ac06d6c2f64c0cb23fb420198a7165ef; GPIO remap and ramp retained.
#include <Arduino.h>
#include <driver/gpio.h>
#include <driver/ledc.h>
#include <esp32-hal-periman.h>
#include <soc/soc_caps.h>
#include <esp_task_wdt.h>
#include <string.h>
#include <stdlib.h>
#include "RobotConfig.h"
#include "DpadControl.h"
#include "DriveMath.h"

constexpr uint16_t BTN_SELECT=1u<<0, BTN_START=1u<<3, BTN_L1=1u<<10;
constexpr uint16_t BTN_R1=1u<<11, BTN_CROSS=1u<<14, BTN_SQUARE=1u<<15;
constexpr uint16_t BTN_TRIANGLE=1u<<12; // Right cluster; unused before v3.6.
constexpr uint16_t BTN_R2=1u<<9;
constexpr uint16_t DIRECTIONS=PAD_UP|PAD_DOWN|PAD_LEFT|PAD_RIGHT;

DpadControl pad;
WheelRamp wheels[4];
bool wheelAttached[4][2]={}, flyAttached=false, feedAttached[2]={};
bool hardwareOK=false, launcherArmed=false, servoEnabled=false;
bool flyActive=false, flySerial=false, flyTimedOut=false, feedActive=false, feedJog=false;
bool feedHasStopped=false, feedNeedsFly=false;
uint8_t psMode=0, flyTarget=0, flyDuty=0;
int feedDirection=0, feedLastDirection=0;
// R1 alternates: each ACCEPTED tap runs one direction for a direction-specific
// duration and arms the next tap for the opposite direction. Rejected taps do
// not flip it. The two directions deliberately differ in length.
int feedNextDirection=1;
// Nonzero while a hold-to-run mechanism run owns the N20 (value = requested
// direction). Cleared by stopFeed. feedHoldTimeout latches after the hold cap.
int feedHoldRequest=0;
bool feedHoldTimeout=false;
uint32_t feedPulseMs(int direction) {
  return direction>0?Cfg::FEED_PULSE_MS_FORWARD:Cfg::FEED_PULSE_MS_REVERSE;
}
uint8_t feedRunPercent=Cfg::FEED_PERCENT;
uint16_t previousButtons=0, pendingEdges=0;
uint16_t servoCurrent=Cfg::SERVO_CENTER_US, servoTarget=Cfg::SERVO_CENTER_US;
uint32_t flyStarted=0, flyDuration=0, feedStarted=0, feedDuration=0, feedStoppedAt=0;
uint32_t servoChangedAt=0, servoTick=0, lastPoll=0, lastTick=0, lastPrint=0;
uint32_t lastConfigAttempt=0;
esp_task_wdt_user_handle_t watchdog=nullptr;
char line[64]; size_t used=0; bool overflow=false;
char faultReason[40]="none";

uint8_t percentDuty(unsigned percent) { return uint8_t((percent*255u+50u)/100u); }
void lowPin(uint8_t pin) { gpio_set_level(static_cast<gpio_num_t>(pin),LOW); pinMode(pin,OUTPUT); }
uint32_t pwmFrequency(uint8_t pin) {
  // Pinned to classic ESP32 / Arduino-ESP32 3.3.12. That core publishes the
  // LEDC channel as bus_num and its dynamically allocated timer as bus_channel.
  // Query the timer directly: ledcReadFreq(pin) is conditional on duty readback.
  if(perimanGetPinBusType(pin)!=ESP32_BUS_TYPE_LEDC) return 0;
  const int channel=perimanGetPinBusNum(pin), timer=perimanGetPinBusChannel(pin);
  if(channel<0 || channel>=2*SOC_LEDC_CHANNEL_NUM || timer<0 || timer>=LEDC_TIMER_MAX) return 0;
  return ledc_get_freq(static_cast<ledc_mode_t>(channel/SOC_LEDC_CHANNEL_NUM),static_cast<ledc_timer_t>(timer));
}
void zeroOutput(uint8_t pin,bool &attached) {
  if(attached && !ledcWrite(pin,0)) {
    // Stop is still attempted through GPIO if the PWM peripheral rejects a write.
    ledcDetach(pin); attached=false; lowPin(pin); hardwareOK=false;
    if(!strcmp(faultReason,"none")) strcpy(faultReason,"stop_write");
  }
}
bool wheelsIdle() {
  for (auto &w:wheels) if(w.value) return false;
  return digitalRead(Cfg::CHASSIS_STBY)==LOW;
}
void stopDrive() {
  digitalWrite(Cfg::CHASSIS_STBY,LOW);
  for(int i=0;i<4;++i) {
    for(int j=0;j<2;++j) zeroOutput(Cfg::WHEEL_PINS[i][j],wheelAttached[i][j]);
    wheels[i].stop(millis());
  }
}
void stopFeed() {
  digitalWrite(Cfg::FEED_STBY,LOW); // Gate the bridge before clearing inputs.
  zeroOutput(Cfg::FEED_IN1,feedAttached[0]);
  zeroOutput(Cfg::FEED_IN2,feedAttached[1]);
  if(feedActive) { feedStoppedAt=millis(); feedHasStopped=true; }
  feedActive=false; feedJog=false; feedNeedsFly=false; feedDirection=0;
  feedHoldRequest=0;
}
void stopFly() {
  stopFeed();
  zeroOutput(Cfg::FLY_EN,flyAttached);
  flyDuty=0; flyActive=false; flySerial=false;
}
void inhibitOutputs() {
  stopDrive(); stopFly(); launcherArmed=false; pendingEdges=0;
  servoTarget=servoCurrent; // Hold last commanded position; do not drop the platform.
}
void lockRobot() { inhibitOutputs(); pad.lock(); }
void fault(const char *why) {
  strncpy(faultReason,why,sizeof(faultReason)-1); faultReason[sizeof(faultReason)-1]=0;
  for(char *p=faultReason;*p;++p) if(*p==' ') *p='_';
  hardwareOK=false; lockRobot();
  // A failed PWM peripheral is not trusted to hold a platform. Use a physical support.
  if(servoEnabled) { ledcDetach(Cfg::SERVO); servoEnabled=false; lowPin(Cfg::SERVO); }
  Serial.printf("FAULT %s; motor enables OFF; reset required\n",why);
}
bool safeNeutral() { return hardwareOK && pad.armed && pad.fresh(millis()) && pad.buttons==0 && pad.turn==0 && wheelsIdle(); }
bool servoSettled() {
  return !servoEnabled || (servoCurrent==servoTarget && uint32_t(millis()-servoChangedAt)>=Cfg::SERVO_SETTLE_MS);
}
bool flyReady() { return flyActive && uint32_t(millis()-flyStarted)>=Cfg::FLY_READY_MS; }
bool pwmStopped(uint8_t pin,bool attached,uint32_t expectedHz) {
  // Core 3.3.12 ledcReadFreq() deliberately returns 0 when duty is 0.
  // Validate attachment, write and duty readback without pulsing a motor at boot.
  const bool ok=attached && ledcWrite(pin,0) && ledcRead(pin)==0 && pwmFrequency(pin)==expectedHz;
  if(!ok) { strcpy(faultReason,"init_pwm"); Serial.printf("INIT FAIL PWM pin=%u attached=%d hz=%lu\n",unsigned(pin),int(attached),static_cast<unsigned long>(pwmFrequency(pin))); }
  return ok;
}
bool setServo(bool on) {
  if(!Cfg::SERVO_INSTALLED) return !on;
  if(!safeNeutral() || flyActive || feedActive) return false;
  if(on==servoEnabled) return true;
  if(!on) {
    if(!ledcDetach(Cfg::SERVO)) { fault("servo detach"); return false; }
    servoEnabled=false; lowPin(Cfg::SERVO); return true;
  }
  if(!ledcAttachChannel(Cfg::SERVO,Cfg::SERVO_HZ,16,Cfg::SERVO_CHANNEL)) {
    fault("servo attach"); return false;
  }
  servoEnabled=true; servoCurrent=servoTarget=Cfg::SERVO_CENTER_US;
  servoChangedAt=servoTick=millis();
  const uint32_t duty=(uint32_t(servoCurrent)*65536u+10000u)/20000u;
  if(!ledcWrite(Cfg::SERVO,duty) || pwmFrequency(Cfg::SERVO)!=Cfg::SERVO_HZ) {
    fault("servo PWM"); return false;
  }
  return true;
}
bool requestServo(uint16_t us) {
  if(!Cfg::SERVO_INSTALLED || !safeNeutral() || !servoEnabled || flyActive || feedActive ||
     us<Cfg::SERVO_MIN_US || us>Cfg::SERVO_MAX_US) return false;
  servoTarget=us; return true;
}
void tickServo() {
  if(!servoEnabled || uint32_t(millis()-servoTick)<Cfg::SERVO_TICK_MS) return;
  servoTick=millis();
  if(servoCurrent<servoTarget) servoCurrent+=uint16_t(servoTarget-servoCurrent>Cfg::SERVO_STEP_US?Cfg::SERVO_STEP_US:servoTarget-servoCurrent);
  else if(servoCurrent>servoTarget) servoCurrent-=uint16_t(servoCurrent-servoTarget>Cfg::SERVO_STEP_US?Cfg::SERVO_STEP_US:servoCurrent-servoTarget);
  else return;
  servoChangedAt=millis();
  if(!ledcWrite(Cfg::SERVO,(uint32_t(servoCurrent)*65536u+10000u)/20000u)) fault("servo write");
}
bool startFly(uint8_t percent,uint32_t duration,bool serialRun) {
  if(!hardwareOK || !launcherArmed || !pad.armed || !pad.fresh(millis()) ||
     feedActive || flyActive || !servoSettled() ||
     percent<1 || percent>100 || duration<1 || duration>Cfg::FLY_MAX_MS) return false;
  flyTarget=percentDuty(percent); flyDuration=duration; flyStarted=millis();
  flyDuty=0; flyActive=true; flySerial=serialRun; return true;
}
bool startFeed(int direction,uint32_t duration,bool jog,uint8_t percent=Cfg::FEED_PERCENT) {
  const int actual=direction*Cfg::FEED_POLARITY;
  if(!hardwareOK || !launcherArmed || !pad.armed || !pad.fresh(millis()) ||
     !wheelsIdle() || (pad.buttons&DIRECTIONS) || pad.turn!=0 || !servoSettled() || feedActive ||
     (direction!=1 && direction!=-1) || duration<1 || duration>Cfg::FEED_JOG_MAX_MS || percent<1 || percent>100) return false;
  // R1 is independent of R2. During a running flywheel ramp, reject rather
  // than queue the request; a pulse begun with ready flywheels follows R2 stop.
  if(jog ? flyActive : (flyActive && !flyReady())) return false;
  if(feedHasStopped && (uint32_t(millis()-feedStoppedAt)<Cfg::FEED_COOLDOWN_MS ||
     (actual!=feedLastDirection && uint32_t(millis()-feedStoppedAt)<Cfg::FEED_REVERSE_GUARD_MS))) return false;
  if(!ledcWrite(Cfg::FEED_IN1,0) || !ledcWrite(Cfg::FEED_IN2,0)) { fault("feed clear"); return false; }
  if(!ledcWrite(actual>0?Cfg::FEED_IN1:Cfg::FEED_IN2,percentDuty(percent))) {
    fault("feed write"); return false;
  }
  if(pwmFrequency(actual>0?Cfg::FEED_IN1:Cfg::FEED_IN2)!=Cfg::DRIVE_HZ) {
    fault("feed frequency"); return false;
  }
  feedDirection=actual; feedLastDirection=actual; feedDuration=duration; feedStarted=millis(); feedRunPercent=percent;
  feedActive=true; feedJog=jog; feedNeedsFly=!jog && flyActive;
  digitalWrite(Cfg::FEED_STBY,HIGH); delayMicroseconds(2);
  if(digitalRead(Cfg::FEED_STBY)!=HIGH) { fault("feed enable"); return false; }
  return true;
}
void receiveFrame(uint8_t mode,uint8_t marker,uint16_t rawButtons,uint8_t rightX=128) {
  psMode=mode; const bool wasFresh=pad.fresh(millis()), wasArmed=pad.armed;
  pad.frame(mode,marker,rawButtons,millis(),rightX);
  if(!wasFresh || !pad.fresh(millis()) || !pad.armed) {
    // frame() owns the neutral counter: do not reset it on each unarmed frame.
    inhibitOutputs(); previousButtons=pad.buttons; return;
  }
  if(!wasArmed) launcherArmed=true; // Repository behavior: no START/L1 required.
  pendingEdges|=uint16_t(pad.buttons & ~previousButtons); previousButtons=pad.buttons;
}
uint8_t exchangeByte(uint8_t tx) {
  uint8_t rx=0;
  for(uint8_t bit=0;bit<8;++bit) {
    digitalWrite(Cfg::PS_CMD,(tx>>bit)&1); digitalWrite(Cfg::PS_CLK,LOW); delayMicroseconds(4);
    if(digitalRead(Cfg::PS_DAT)) rx|=1u<<bit;
    digitalWrite(Cfg::PS_CLK,HIGH); delayMicroseconds(4);
  }
  digitalWrite(Cfg::PS_CMD,HIGH); delayMicroseconds(20); return rx;
}
void configureAnalog() {
  lockRobot();
  const uint8_t commands[3][9]={
    {1,0x43,0,1,0,0,0,0,0},
    {1,0x44,0,1,3,0,0,0,0},
    {1,0x43,0,0,0x5A,0x5A,0x5A,0x5A,0x5A}
  };
  for(int c=0;c<3;++c) {
    digitalWrite(Cfg::PS_CS,LOW); delayMicroseconds(20);
    for(int i=0;i<(c==0?5:9);++i) exchangeByte(commands[c][i]);
    digitalWrite(Cfg::PS_CS,HIGH); delay(10);
  }
}
void maybeConfigureAnalog() {
  if(hardwareOK && pad.online && !pad.analog && pad.buttons==0 && pad.turn==0 &&
     wheelsIdle() && !flyActive && !feedActive && servoCurrent==servoTarget &&
     uint32_t(millis()-lastConfigAttempt)>=2000) {
    lastConfigAttempt=millis(); configureAnalog();
  }
}
void pollPS() {
  uint8_t rx[21]={}, length=9;
  digitalWrite(Cfg::PS_CS,LOW); delayMicroseconds(20);
  for(uint8_t i=0;i<length;++i) {
    rx[i]=exchangeByte(i==0?1:(i==1?0x42:0));
    if(i==1 && rx[1]==0x79) length=21;
  }
  digitalWrite(Cfg::PS_CS,HIGH);
  receiveFrame(rx[1],rx[2],uint16_t(rx[3])|(uint16_t(rx[4])<<8),rx[5]);
  maybeConfigureAnalog();
}
void tickDrive() {
  int y,x,targets[4];
  if(!pad.axes(millis(),y,x)) { stopDrive(); return; }
  mixDrive(y,x,pad.turn,Cfg::DRIVE_DUTY,targets); bool moving=false;
  for(int i=0;i<4;++i) {
    const int duty=wheels[i].step(targets[i]*Cfg::WHEEL_POLARITY[i],millis()); moving|=duty!=0;
    const uint8_t active=Cfg::WHEEL_PINS[i][duty>=0?0:1], inactive=Cfg::WHEEL_PINS[i][duty>=0?1:0];
    if(!ledcWrite(inactive,0) || !ledcWrite(active,unsigned(duty>=0?duty:-duty))) { fault("drive write"); return; }
    if(duty && pwmFrequency(active)!=Cfg::DRIVE_HZ) { fault("drive frequency"); return; }
  }
  digitalWrite(Cfg::CHASSIS_STBY,moving?HIGH:LOW);
}
void tickRobot() {
  if(!hardwareOK || !pad.fresh(millis()) || (pad.buttons&PAD_CIRCLE)) { lockRobot(); return; }
  if(!pad.armed) { inhibitOutputs(); return; }
  const uint16_t keys=pad.buttons, edges=pendingEdges; pendingEdges=0;
  const bool driveIntent=(keys&DIRECTIONS) || pad.turn!=0;
  if(driveIntent && (feedActive || servoCurrent!=servoTarget)) {
    lockRobot(); Serial.println("LOCK conflicting drive/action; release all"); return;
  }
  if(!driveIntent) stopDrive();
  if(flySerial && (keys || pad.turn!=0)) { lockRobot(); return; }
  if(feedJog && (keys || pad.turn!=0)) { lockRobot(); return; }
  // No hand-servo controls: R1 belongs to the N20 docking mechanism.
  tickServo();
  if(!hardwareOK) return;
  if(!(keys&BTN_R2)) {
    flyTimedOut=false;
    if(flyActive && !flySerial) stopFly();
  } else if((edges&BTN_R2) && !flyTimedOut && !flyActive && launcherArmed) {
    startFly(Cfg::FLY_PERCENT,Cfg::FLY_MAX_MS,false);
  }
  if(flyActive) {
    const uint32_t elapsed=uint32_t(millis()-flyStarted);
    if(elapsed>=flyDuration) {
      flyTimedOut=true; stopFly(); Serial.println("FLY TIMEOUT; release R2; rotors coast");
    } else {
      flyDuty=elapsed>=Cfg::FLY_RAMP_MS?flyTarget:uint8_t(uint32_t(flyTarget)*elapsed/Cfg::FLY_RAMP_MS);
      if(!ledcWrite(Cfg::FLY_EN,flyDuty)) { fault("fly write"); return; }
      if(flyDuty && pwmFrequency(Cfg::FLY_EN)!=Cfg::FLY_HZ) { fault("fly frequency"); return; }
    }
  }
  if((edges&BTN_R1) && !flySerial && launcherArmed) {
    const int direction=feedNextDirection;
    if(startFeed(direction,feedPulseMs(direction),false)) feedNextDirection=-direction;
    else Serial.println("N20 R1 rejected: park/ramp/settle/cooldown");
  }
  // Hold-to-run mechanism: TRIANGLE raises while held, CROSS lowers while held.
  // Releasing the button stops the pulse immediately; reaching the hold cap stops
  // it and latches until both buttons are released. If both are held, TRIANGLE
  // wins. Hold runs never touch the R1 alternation slot.
  const int holdWant=(keys&BTN_TRIANGLE)?Cfg::FEED_UP_DIRECTION:((keys&BTN_CROSS)?-Cfg::FEED_UP_DIRECTION:0);
  if(!holdWant) feedHoldTimeout=false;
  if(feedHoldRequest && holdWant!=feedHoldRequest) stopFeed();
  if(!feedActive && !feedHoldRequest && !feedHoldTimeout && holdWant && launcherArmed && !flySerial &&
     (edges&(BTN_TRIANGLE|BTN_CROSS))) {
    if(startFeed(holdWant,Cfg::FEED_HOLD_MAX_MS,false)) feedHoldRequest=holdWant;
    else Serial.println("N20 hold rejected: park/ramp/settle/cooldown");
  }
  if(feedActive) {
    const bool expired=uint32_t(millis()-feedStarted)>=feedDuration;
    const bool flyLost=feedNeedsFly && !flyReady();
    if(expired || flyLost) {
      const bool wasHold=feedHoldRequest!=0;
      stopFeed();
      if(wasHold && expired) { feedHoldTimeout=true; Serial.println("N20 hold CAP; release both buttons"); }
    }
  }
  if(!feedActive && servoCurrent==servoTarget) tickDrive(); else stopDrive();
}
void printStatus() {
  Serial.printf("STATUS fw=%s hw=%d ps=%d mode=%02X ready=%d armed=%d buttons=%04X "
    "chassis=%d fly=%d duty=%u pretime=%d feed=%d TB3=%d servo=%d us=%u target=%u RX=%u turn=%d analog=%d "
    "N20ms=%lu N20pct=%u N20next=%d N20last=%d N20hold=%d drive=%d,%d,%d,%d fault=%s pwmhz=%lu,%lu,%lu N20pwm=%lu,%lu\n",
    Cfg::VERSION,int(hardwareOK),int(pad.fresh(millis())),psMode,int(pad.armed),int(launcherArmed),pad.buttons,
    digitalRead(Cfg::CHASSIS_STBY),int(flyActive),unsigned(flyDuty),int(flyReady()),int(feedActive),
    digitalRead(Cfg::FEED_STBY),int(servoEnabled),unsigned(servoCurrent),unsigned(servoTarget),
    unsigned(pad.rightX),pad.turn,int(pad.analog),
    static_cast<unsigned long>(feedActive?feedDuration:feedPulseMs(feedNextDirection)),
    unsigned(feedActive?feedRunPercent:Cfg::FEED_PERCENT),feedNextDirection,feedLastDirection,feedHoldRequest,
    wheels[0].value,wheels[1].value,wheels[2].value,wheels[3].value,faultReason,
    static_cast<unsigned long>(pwmFrequency(16)),static_cast<unsigned long>(pwmFrequency(Cfg::FLY_EN)),
    static_cast<unsigned long>(pwmFrequency(Cfg::FEED_IN1)),
    static_cast<unsigned long>(ledcRead(Cfg::FEED_IN1)),static_cast<unsigned long>(ledcRead(Cfg::FEED_IN2)));
}
uint8_t parseNumbers(const char *s,long *values,uint8_t maxCount) {
  uint8_t count=0;
  while(*s) {
    while(*s==' ') ++s; if(!*s) break;
    if(count>=maxCount) return 255;
    char *end=nullptr; const long v=strtol(s,&end,10);
    if(end==s || (*end && *end!=' ') || v<-100000 || v>100000) return 255;
    values[count++]=v; s=end;
  }
  return count;
}
void command() {
  line[used]=0; bool ok=false; long args[3]={};
  if(overflow) { lockRobot(); Serial.println("ERR overflow; locked"); }
  else if(!used) { }
  else if(!strcmp(line,"STATUS")) printStatus();
  else if(!strcmp(line,"STOP") || !strcmp(line,"DISARM")) { lockRobot(); Serial.println("OK motors OFF; servo holds"); }
  else if(!strcmp(line,"ARM")) {
    ok=safeNeutral() && !flyActive && !feedActive;
    if(ok) launcherArmed=true; Serial.println(ok?"OK armed; no motion":"ERR ARM requires fresh neutral controller");
  } else if(!strcmp(line,"SERVO ON") || !strcmp(line,"SERVO OFF")) {
    ok=setServo(!strcmp(line,"SERVO ON")); Serial.println(ok?"OK servo":"ERR servo gate");
  } else if(!strncmp(line,"SERVO ",6)) {
    const uint8_t n=parseNumbers(line+6,args,1);
    ok=n==1 && args[0]>=Cfg::SERVO_MIN_US && args[0]<=Cfg::SERVO_MAX_US && requestServo(uint16_t(args[0]));
    Serial.println(ok?"OK servo target (not measured angle)":"ERR servo range/gate");
  } else if(!strncmp(line,"FLY ",4)) {
    const uint8_t n=parseNumbers(line+4,args,2);
    ok=n==2 && args[0]>=1 && args[0]<=100 && args[1]>=1 && args[1]<=long(Cfg::FLY_MAX_MS) &&
       safeNeutral() && startFly(uint8_t(args[0]),uint32_t(args[1]),true);
    Serial.println(ok?"OK timed flywheel test":"ERR FLY percent/ms/gate");
  } else if(!strncmp(line,"JOG ",4)) {
    const uint8_t n=parseNumbers(line+4,args,3);
    const long percent=n==2?Cfg::FEED_PERCENT:args[2];
    ok=(n==2 || n==3) && (args[0]==1 || args[0]==-1) && args[1]>=1 && args[1]<=long(Cfg::FEED_JOG_MAX_MS) &&
       percent>=1 && percent<=100 && safeNeutral() && startFeed(int(args[0]),uint32_t(args[1]),true,uint8_t(percent));
    Serial.println(ok?"OK bounded N20 jog":"ERR JOG direction/ms/percent/gate");
  } else { lockRobot(); Serial.println("ERR unknown command; locked"); }
  used=0; overflow=false;
}
void setup() {
  lowPin(Cfg::CHASSIS_STBY); lowPin(Cfg::FEED_STBY); lowPin(Cfg::FLY_EN);
  lowPin(Cfg::FEED_IN1); lowPin(Cfg::FEED_IN2); lowPin(Cfg::SERVO);
  Serial.begin(115200); hardwareOK=true; strcpy(faultReason,"none");
  for(int i=0;i<4;++i) for(int j=0;j<2;++j) {
    const uint8_t pin=Cfg::WHEEL_PINS[i][j]; lowPin(pin);
    wheelAttached[i][j]=ledcAttachChannel(pin,Cfg::DRIVE_HZ,8,uint8_t(i*2+j));
    if(!pwmStopped(pin,wheelAttached[i][j],Cfg::DRIVE_HZ)) hardwareOK=false;
  }
  flyAttached=ledcAttachChannel(Cfg::FLY_EN,Cfg::FLY_HZ,8,Cfg::FLY_CHANNEL);
  feedAttached[0]=ledcAttachChannel(Cfg::FEED_IN1,Cfg::DRIVE_HZ,8,Cfg::FEED_CH1);
  feedAttached[1]=ledcAttachChannel(Cfg::FEED_IN2,Cfg::DRIVE_HZ,8,Cfg::FEED_CH2);
  if(!pwmStopped(Cfg::FLY_EN,flyAttached,Cfg::FLY_HZ)) hardwareOK=false;
  if(!pwmStopped(Cfg::FEED_IN1,feedAttached[0],Cfg::DRIVE_HZ)) hardwareOK=false;
  if(!pwmStopped(Cfg::FEED_IN2,feedAttached[1],Cfg::DRIVE_HZ)) hardwareOK=false;
  digitalWrite(Cfg::PS_CS,HIGH); pinMode(Cfg::PS_CS,OUTPUT);
  digitalWrite(Cfg::PS_CLK,HIGH); pinMode(Cfg::PS_CLK,OUTPUT);
  digitalWrite(Cfg::PS_CMD,HIGH); pinMode(Cfg::PS_CMD,OUTPUT); pinMode(Cfg::PS_DAT,INPUT_PULLUP);
  lockRobot();
  lastConfigAttempt=millis(); configureAnalog();
  esp_task_wdt_config_t config={1000,0,true}; esp_err_t result=esp_task_wdt_init(&config);
  if(result==ESP_ERR_INVALID_STATE) result=esp_task_wdt_reconfigure(&config);
  if(result!=ESP_OK || esp_task_wdt_add_user("integrated-loop",&watchdog)!=ESP_OK) { hardwareOK=false; strcpy(faultReason,"init_watchdog"); }
  Serial.printf("BOOT %s; NEW WIRING ONLY; release buttons and center right stick\n",Cfg::VERSION);
  Serial.println("D-pad drive + right stick turn; hold R2 fly; hold TRIANGLE rise / CROSS lower N20 (release stops); tap R1 alternates 3000/1750ms; servo absent; CIRCLE stop");
  printStatus();
}
void loop() {
  uint32_t now=millis();
  if(uint32_t(now-lastPoll)>=20) { lastPoll=now; pollPS(); }
  if(!pad.fresh(millis())) lockRobot();
  for(uint8_t n=0;n<64 && Serial.available();++n) {
    const char ch=char(Serial.read());
    if(ch=='\n' || ch=='\r') command();
    else if(!overflow) {
      if(used<sizeof(line)-1 && ch>=32 && ch<=126) line[used++]=ch;
      else { overflow=true; lockRobot(); }
    }
  }
  now=millis();
  if(uint32_t(now-lastTick)>=10) { lastTick=now; tickRobot(); }
  if(uint32_t(now-lastPrint)>=500 && Serial.availableForWrite()>230) { lastPrint=now; printStatus(); }
  if(watchdog) esp_task_wdt_reset_user(watchdog);
  delay(1);
}
