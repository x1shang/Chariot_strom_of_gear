#pragma once
#include <stdint.h>

constexpr uint16_t PAD_UP = 1u << 4;
constexpr uint16_t PAD_RIGHT = 1u << 5;
constexpr uint16_t PAD_DOWN = 1u << 6;
constexpr uint16_t PAD_LEFT = 1u << 7;
constexpr uint16_t PAD_CIRCLE = 1u << 13;

struct DpadControl {
  bool online = false, armed = false;
  uint8_t neutralFrames = 0, rightX = 128;
  bool analog = false;
  int turn = 0;
  static int turnFromStick(uint8_t raw) {
    const int delta = int(raw) - 128;
    if (delta >= -20 && delta <= 20) return 0;
    return delta > 0 ? (delta - 20) * 255 / 107 : (delta + 20) * 255 / 108;
  }
  uint16_t buttons = 0;
  uint32_t lastGood = 0;

  void lock() { armed = false; neutralFrames = 0; }
  bool fresh(uint32_t now) const {
    return online && uint32_t(now - lastGood) < 150;
  }
  void frame(uint8_t mode, uint8_t marker, uint16_t rawButtons, uint32_t now, uint8_t rx = 128) {
    // A delayed poll must not resume a held direction after a stale link.
    if (!fresh(now)) lock();
    online = marker == 0x5A && (mode == 0x41 || mode == 0x73 || mode == 0x79);
    if (!online) { buttons = 0; turn = 0; rightX = 128; lock(); return; }
    const bool nextAnalog = mode == 0x73 || mode == 0x79;
    if (nextAnalog != analog) lock();
    analog = nextAnalog;
    rightX = analog ? rx : 128;
    turn = analog ? turnFromStick(rightX) : 0;
    lastGood = now;
    buttons = uint16_t(~rawButtons); // PS2 buttons are active low.
    if (buttons & PAD_CIRCLE) { lock(); return; }
    if (!armed) {
      if (buttons == 0 && turn == 0) {
        if (++neutralFrames >= 5) armed = true;
      } else neutralFrames = 0;
    }
  }
  bool axes(uint32_t now, int &y, int &x) const {
    y = x = 0;
    if (!armed || !fresh(now) || (buttons & PAD_CIRCLE)) return false;
    y = int(bool(buttons & PAD_UP)) - int(bool(buttons & PAD_DOWN));
    x = int(bool(buttons & PAD_RIGHT)) - int(bool(buttons & PAD_LEFT));
    return y != 0 || x != 0 || turn != 0;
  }
};
