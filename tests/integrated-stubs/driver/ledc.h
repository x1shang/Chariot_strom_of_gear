#pragma once
#include <Arduino.h>
enum ledc_mode_t { LEDC_HIGH_SPEED_MODE, LEDC_LOW_SPEED_MODE };
enum ledc_timer_t { LEDC_TIMER_0, LEDC_TIMER_1, LEDC_TIMER_2, LEDC_TIMER_3, LEDC_TIMER_MAX };
inline uint32_t ledc_get_freq(ledc_mode_t mode,ledc_timer_t timer) {
  if(failFreq>=0 && frequencyByPin[failFreq] && channelByPin[failFreq]/8==mode && timerByPin[failFreq]==timer)
    return frequencyByPin[failFreq]+1000;
  for(unsigned p=0;p<40;++p)
    if(frequencyByPin[p] && channelByPin[p]/8==mode && timerByPin[p]==timer) return frequencyByPin[p];
  return 0;
}
