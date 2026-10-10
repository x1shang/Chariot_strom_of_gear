#pragma once
#include <stdint.h>

// Wheel order: FL, RL, FR, RR; positive y=forward, x=right.
inline void mixTranslation(int y, int x, int cap, int out[4]) {
  out[0] = y + x; out[1] = y - x;
  out[2] = y - x; out[3] = y + x;
  int peak = 1;
  for (int i = 0; i < 4; ++i) {
    int magnitude = out[i] < 0 ? -out[i] : out[i];
    if (magnitude > peak) peak = magnitude;
  }
  for (int i = 0; i < 4; ++i) out[i] = out[i] * cap / peak;
}

// Retains the existing project's ramp and 300ms reversal dead time.
struct WheelRamp {
  int value = 0, lastSign = 0;
  uint32_t zeroAt = 0;
  void stop(uint32_t now) {
    if (value) zeroAt = now;
    value = 0;
  }
  int step(int target, uint32_t now) {
    int sign = target > 0 ? 1 : (target < 0 ? -1 : 0);
    bool reversing = sign && lastSign && sign != lastSign;
    if (reversing && (value || uint32_t(now - zeroAt) < 300)) target = 0;
    int next = value;
    if (next < target) next += (target - next > 3 ? 3 : target - next);
    if (next > target) next -= (next - target > 3 ? 3 : next - target);
    if (value && !next) zeroAt = now;
    value = next;
    if (value) lastSign = value > 0 ? 1 : -1;
    return value;
  }
};
