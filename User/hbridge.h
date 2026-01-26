#pragma once

#include <stdint.h>

typedef enum
{
  HBRIDGE_OFF = 0,
  HBRIDGE_FORWARD,
  HBRIDGE_REVERSE,
} hbridge_mode_t;

void HBridge_Init(void);
void HBridge_SetMode(hbridge_mode_t mode);
void HBridge_Task(void);
uint8_t HBridge_GetBrightness(void);
void HBridge_SetBrightness(uint8_t pct);
void HBridge_SaveBrightness(void);
hbridge_mode_t HBridge_GetMode(void);
hbridge_mode_t HBridge_GetPreferredMode(void);
void HBridge_TogglePreferredMode(void);
void HBridge_Systick(void);
