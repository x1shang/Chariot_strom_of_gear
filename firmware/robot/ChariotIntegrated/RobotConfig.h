#pragma once
#include <stdint.h>

namespace Cfg {
constexpr char VERSION[] = "ChariotIntegrated-v3.3-JOG";
constexpr char CONTROL_BASELINE[] = "9a11db22ac06d6c2f64c0cb23fb420198a7165ef";
constexpr uint8_t WHEEL_PINS[4][2] = {{16,17},{18,19},{23,32},{15,5}};
constexpr int WHEEL_POLARITY[4] = {1,1,-1,-1};
constexpr uint8_t CHASSIS_STBY=33, PS_CLK=2, PS_CS=4, PS_CMD=12, PS_DAT=13;
constexpr uint8_t FLY_EN=14, SERVO=27, FEED_IN1=25, FEED_IN2=26, FEED_STBY=21;
constexpr bool SERVO_INSTALLED=false; // User has no servo; GPIO27 stays low.
// GPIO22 is disconnected after an isolated LOW readback; use GPIO21 for TB3 STBY. L298 IN1/IN3 are hardwired 3V3; IN2/IN4 are GND.
constexpr uint32_t DRIVE_HZ=20000, FLY_HZ=5000, SERVO_HZ=50;
constexpr uint8_t FLY_CHANNEL=8, SERVO_CHANNEL=9, FEED_CH1=10, FEED_CH2=11;
constexpr uint8_t DRIVE_DUTY=255, FLY_PERCENT=100;
constexpr uint32_t FLY_RAMP_MS=1200, FLY_READY_MS=2500, FLY_MAX_MS=10000;
// READY is an elapsed-time permission, NOT a measured speed.
constexpr uint8_t FEED_PERCENT=30;
constexpr int FEED_POLARITY=1; // R1 docking direction; verify with mechanism detached.
// FEED_JOG_MAX_MS bounds a serial JOG; raised to 3000 so a loaded endurance
// pulse (80% for 3s) can be run without losing the bounded auto-stop.
constexpr uint32_t FEED_PULSE_MS=150, FEED_JOG_MAX_MS=3000;
constexpr uint32_t FEED_COOLDOWN_MS=800, FEED_REVERSE_GUARD_MS=300;
// Initial narrow commissioning range. Not a measured platform angle.
constexpr uint16_t SERVO_MIN_US=1400, SERVO_CENTER_US=1500, SERVO_MAX_US=1600;
constexpr uint16_t SERVO_STEP_US=5;
constexpr int SERVO_DIRECTION=1;
constexpr uint32_t SERVO_TICK_MS=20, SERVO_SETTLE_MS=500;
static_assert(FLY_PERCENT<=100 && FEED_PERCENT>0 && FEED_PERCENT<=100, "Duty range");
static_assert(FLY_READY_MS>=FLY_RAMP_MS && FLY_READY_MS<FLY_MAX_MS, "Pre-spin time");
static_assert(FEED_PULSE_MS>0 && FEED_PULSE_MS<=FEED_JOG_MAX_MS && FEED_JOG_MAX_MS<=3000, "Feed time");
static_assert(SERVO_MIN_US>=500 && SERVO_MIN_US<=SERVO_CENTER_US &&
              SERVO_CENTER_US<=SERVO_MAX_US && SERVO_MAX_US<=2500, "Servo range");
static_assert(FEED_POLARITY==1 || FEED_POLARITY==-1, "Feeder polarity");
static_assert(SERVO_DIRECTION==1 || SERVO_DIRECTION==-1, "Servo polarity");
}
