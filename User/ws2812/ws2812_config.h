#pragma once

#include "py32f0xx_hal.h"

/* CPU and pin configuration for light_ws2812 */
#define F_CPU 8000000UL
#define LIGHT_WS2812_UC_PY32
#define LIGHT_WS2812_GPIO_PORT GPIOB
#define LIGHT_WS2812_GPIO_PIN  GPIO_PIN_0
