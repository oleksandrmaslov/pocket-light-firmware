/**
  ******************************************************************************
  * @file    main.c
  * @author  MCU Application Team
  * @brief   Main program body
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; Copyright (c) Puya Semiconductor Co.
  * All rights reserved.</center></h2>
  *
  * <h2><center>&copy; Copyright (c) 2016 STMicroelectronics.
  * All rights reserved.</center></h2>
  *
  * This software component is licensed by ST under BSD 3-Clause license,
  * the "License"; You may not use this file except in compliance with the
  * License. You may obtain a copy of the License at:
  *                        opensource.org/licenses/BSD-3-Clause
  *
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "py32f0xx_hal.h"
#include "ws2812_ctrl.h"
#include "hbridge.h"
#include "SEGGER_RTT.h"

int main(void)
{
  HAL_Init();

  /* Default system clock is HSI 8MHz; keep as-is to match F_CPU for ws2812 */
  WS2812_Ctrl_Init();
  HBridge_Init();

  SEGGER_RTT_printf(0, "\r\nPY32F0xx WS2812 + DRV8837 Demo SYSCLK: %lu\r\n", SystemCoreClock);

  while (1)
  {
    WS2812_Ctrl_Task();
    HBridge_ButtonTask();
    HBridge_Task();
    HAL_Delay(20);
  }
}

void APP_ErrorHandler(void)
{
  while (1);
}

#ifdef  USE_FULL_ASSERT

void assert_failed(uint8_t *file, uint32_t line)
{
  while (1)
  {
  }
}
#endif /* USE_FULL_ASSERT */

/************************ (C) COPYRIGHT Puya *****END OF FILE******************/
