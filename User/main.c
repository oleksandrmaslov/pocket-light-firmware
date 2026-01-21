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
#include "py32f0xx_bsp_printf.h"
#include "ws2812_config.h"
#include "light_ws2812_cortex.h"

typedef struct
{
  uint8_t g;
  uint8_t r;
  uint8_t b;
} cRGB;

/* Private function prototypes -----------------------------------------------*/
static void APP_LedConfig(void);
static void APP_WS2812_Init(void);
static void APP_ColorWheel(uint8_t pos, uint8_t *r, uint8_t *g, uint8_t *b);

int main(void)
{
  HAL_Init();

  /* Default system clock is HSI 8MHz; keep as-is to match F_CPU for ws2812 */
  APP_LedConfig();
  APP_WS2812_Init();
  BSP_USART_Config();

  printf("\r\nPY32F0xx WS2812 Demo (light_ws2812) SYSCLK: %lu\r\n", SystemCoreClock);

  uint8_t hue = 0;
  cRGB leds[1];

  while (1)
  {
    APP_ColorWheel(hue, &leds[0].r, &leds[0].g, &leds[0].b);
    ws2812_sendarray((uint8_t *)leds, sizeof(leds));
    hue++;
    HAL_Delay(20);
  }
}

static void APP_LedConfig(void)
{
  GPIO_InitTypeDef GPIO_InitStruct;

  __HAL_RCC_GPIOB_CLK_ENABLE();

  GPIO_InitStruct.Pin = GPIO_PIN_1;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;

  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  __HAL_RCC_GPIOF_CLK_ENABLE();

  GPIO_InitStruct.Pin = GPIO_PIN_0;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;

  HAL_GPIO_Init(GPIOF, &GPIO_InitStruct);
}

static void APP_WS2812_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct;

  __HAL_RCC_GPIOB_CLK_ENABLE();

  GPIO_InitStruct.Pin = LIGHT_WS2812_GPIO_PIN;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(LIGHT_WS2812_GPIO_PORT, &GPIO_InitStruct);

  HAL_GPIO_WritePin(LIGHT_WS2812_GPIO_PORT, LIGHT_WS2812_GPIO_PIN, GPIO_PIN_RESET);
}

static void APP_ColorWheel(uint8_t pos, uint8_t *r, uint8_t *g, uint8_t *b)
{
  uint8_t region = pos / 85;
  uint8_t step = pos % 85;

  switch (region)
  {
  case 0:
    *r = 255 - step * 3;
    *g = step * 3;
    *b = 0;
    break;
  case 1:
    *r = 0;
    *g = 255 - step * 3;
    *b = step * 3;
    break;
  default:
    *r = step * 3;
    *g = 0;
    *b = 255 - step * 3;
    break;
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
