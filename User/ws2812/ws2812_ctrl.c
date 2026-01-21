#include "ws2812_ctrl.h"
#include "light_ws2812_cortex.h"
#include "ws2812_config.h"
#include "py32f0xx_hal.h"

typedef struct
{
  uint8_t g;
  uint8_t r;
  uint8_t b;
} cRGB;

static cRGB led;
static uint8_t hue = 0;

static void WS_ColorWheel(uint8_t pos, uint8_t *r, uint8_t *g, uint8_t *b)
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

void WS2812_Ctrl_Init(void)
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

void WS2812_Ctrl_Task(void)
{
  WS_ColorWheel(hue, &led.r, &led.g, &led.b);
  ws2812_sendarray((uint8_t *)&led, sizeof(led));
  hue++;
}
