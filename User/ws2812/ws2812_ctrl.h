#pragma once

#include <stdint.h>

void WS2812_Ctrl_Init(void);
void WS2812_Ctrl_Task(void);
void WS2812_Ctrl_SetEnabled(uint8_t enable);
void WS2812_Ctrl_RequestBatteryIndication(uint32_t duration_ms);
uint8_t WS2812_Ctrl_IsActive(void);
