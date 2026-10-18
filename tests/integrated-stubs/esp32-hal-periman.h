#pragma once
#include <Arduino.h>
enum peripheral_bus_type_t { ESP32_BUS_TYPE_INIT, ESP32_BUS_TYPE_LEDC };
inline peripheral_bus_type_t perimanGetPinBusType(uint8_t p) {
  return frequencyByPin[p]?ESP32_BUS_TYPE_LEDC:ESP32_BUS_TYPE_INIT;
}
inline int8_t perimanGetPinBusNum(uint8_t p) { return frequencyByPin[p]?int8_t(channelByPin[p]):-1; }
inline int8_t perimanGetPinBusChannel(uint8_t p) { return frequencyByPin[p]?int8_t(timerByPin[p]):-1; }
