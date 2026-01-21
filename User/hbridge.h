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
void HBridge_ButtonTask(void);
