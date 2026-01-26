#include "hbridge.h"
#include "py32f0xx_hal.h"
#include "py32f0xx_hal_flash.h"
#include "py32f0xx_hal_flash_ex.h"
#include "SEGGER_RTT.h"

#define HBRIDGE_NSLP_PORT GPIOA
#define HBRIDGE_NSLP_PIN  GPIO_PIN_0
#define HBRIDGE_IN1_PORT  GPIOA
#define HBRIDGE_IN1_PIN   GPIO_PIN_1 /* Red diode */
#define HBRIDGE_IN2_PORT  GPIOA
#define HBRIDGE_IN2_PIN   GPIO_PIN_2 /* White diode */
#define HBRIDGE_CTRL_PORT GPIOA
#define HBRIDGE_CTRL_PIN  GPIO_PIN_4 /* LED driver enable (software PWM) */

#define SWITCH_PAUSE_MS   5U
#define DRIVER_PAUSE_MS   1U
#define HBRIDGE_FADE_MS   500U

#define BRIGHT_MIN_PCT    20U
#define BRIGHT_MAX_PCT    100U

#define CONFIG_MAGIC      0xBEEFCAFEUL
#define CONFIG_PAGE_ADDR  0x08004C00UL /* aligned page near end of 20KB flash */

#define PWM_WINDOW_MS     10U          /* 100 Hz software PWM via SysTick */

static hbridge_mode_t current_mode = HBRIDGE_OFF;
static hbridge_mode_t preferred_mode = HBRIDGE_FORWARD;
static uint8_t brightness_pct = BRIGHT_MAX_PCT;
static uint32_t pwm_window_start = 0;

static void HBridge_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct;

  __HAL_RCC_GPIOA_CLK_ENABLE();

  GPIO_InitStruct.Pin = HBRIDGE_NSLP_PIN | HBRIDGE_IN1_PIN | HBRIDGE_IN2_PIN | HBRIDGE_CTRL_PIN;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(HBRIDGE_NSLP_PORT, &GPIO_InitStruct);
}

static void HBridge_UpdatePins(hbridge_mode_t mode)
{
  /* Driver fully off */
  HAL_GPIO_WritePin(HBRIDGE_CTRL_PORT, HBRIDGE_CTRL_PIN, GPIO_PIN_RESET);

  /* Step 1: put bridge to sleep and tri-state outputs */
  HAL_GPIO_WritePin(HBRIDGE_NSLP_PORT, HBRIDGE_NSLP_PIN, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(HBRIDGE_IN1_PORT, HBRIDGE_IN1_PIN, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(HBRIDGE_IN2_PORT, HBRIDGE_IN2_PIN, GPIO_PIN_RESET);
  HAL_Delay(SWITCH_PAUSE_MS);

  if (mode == HBRIDGE_OFF)
  {
    return;
  }

  /* Step 2: set direction while unpowered */
  if (mode == HBRIDGE_FORWARD)
  {
    HAL_GPIO_WritePin(HBRIDGE_IN1_PORT, HBRIDGE_IN1_PIN, GPIO_PIN_SET);
    HAL_GPIO_WritePin(HBRIDGE_IN2_PORT, HBRIDGE_IN2_PIN, GPIO_PIN_RESET);
  }
  else
  {
    HAL_GPIO_WritePin(HBRIDGE_IN1_PORT, HBRIDGE_IN1_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(HBRIDGE_IN2_PORT, HBRIDGE_IN2_PIN, GPIO_PIN_SET);
  }

  /* Step 3: wake bridge */
  HAL_GPIO_WritePin(HBRIDGE_NSLP_PORT, HBRIDGE_NSLP_PIN, GPIO_PIN_SET);
  HAL_Delay(DRIVER_PAUSE_MS);

  /* Step 4: reassert direction */
  if (mode == HBRIDGE_FORWARD)
  {
    HAL_GPIO_WritePin(HBRIDGE_IN1_PORT, HBRIDGE_IN1_PIN, GPIO_PIN_SET);
    HAL_GPIO_WritePin(HBRIDGE_IN2_PORT, HBRIDGE_IN2_PIN, GPIO_PIN_RESET);
  }
  else
  {
    HAL_GPIO_WritePin(HBRIDGE_IN1_PORT, HBRIDGE_IN1_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(HBRIDGE_IN2_PORT, HBRIDGE_IN2_PIN, GPIO_PIN_SET);
  }
}

static void HBridge_LoadBrightness(void)
{
  uint32_t *p = (uint32_t *)CONFIG_PAGE_ADDR;
  if (p[0] == CONFIG_MAGIC)
  {
    uint32_t val = p[1];
    if (val >= BRIGHT_MIN_PCT && val <= BRIGHT_MAX_PCT)
    {
      brightness_pct = (uint8_t)val;
      return;
    }
  }
  brightness_pct = BRIGHT_MAX_PCT;
}

static void HBridge_FadeTo(uint8_t target_pct, uint16_t duration_ms)
{
  uint8_t start = brightness_pct;
  uint16_t steps = duration_ms / 10U; /* finer steps for smoother fade */
  if (steps == 0) steps = 1;
  int16_t delta = (int16_t)target_pct - (int16_t)start;

  for (uint16_t i = 1; i <= steps; i++)
  {
    uint8_t val = (uint8_t)(start + ((delta * i) / steps));
    brightness_pct = val;
    HAL_Delay(duration_ms / steps);
  }
  brightness_pct = target_pct;
}

void HBridge_Init(void)
{
  HBridge_GPIO_Init();
  HBridge_LoadBrightness();

  HBridge_UpdatePins(HBRIDGE_OFF);
}

void HBridge_SetMode(hbridge_mode_t mode)
{
  if (mode == current_mode)
  {
    return;
  }

  /* If changing direction, pass through OFF to guarantee both legs off */
  if ((current_mode == HBRIDGE_FORWARD && mode == HBRIDGE_REVERSE) ||
      (current_mode == HBRIDGE_REVERSE && mode == HBRIDGE_FORWARD))
  {
    HBridge_UpdatePins(HBRIDGE_OFF);
    HAL_Delay(SWITCH_PAUSE_MS);
  }

  current_mode = mode;
  HBridge_UpdatePins(current_mode);

  if (current_mode != HBRIDGE_OFF)
  {
    HBridge_FadeTo(brightness_pct, HBRIDGE_FADE_MS);
  }

  SEGGER_RTT_printf(0, "H-bridge mode: %d\r\n", current_mode);
}

void HBridge_Task(void)
{
  /* placeholder for periodic safety, currently no-op */
}

hbridge_mode_t HBridge_GetMode(void)
{
  return current_mode;
}

hbridge_mode_t HBridge_GetPreferredMode(void)
{
  return preferred_mode;
}

void HBridge_TogglePreferredMode(void)
{
  preferred_mode = (preferred_mode == HBRIDGE_FORWARD) ? HBRIDGE_REVERSE : HBRIDGE_FORWARD;
  if (current_mode != HBRIDGE_OFF)
  {
    HBridge_SetMode(preferred_mode);
  }
}

uint8_t HBridge_GetBrightness(void)
{
  return brightness_pct;
}

void HBridge_SetBrightness(uint8_t pct)
{
  if (pct < BRIGHT_MIN_PCT) pct = BRIGHT_MIN_PCT;
  if (pct > BRIGHT_MAX_PCT) pct = BRIGHT_MAX_PCT;
  brightness_pct = pct;
}

void HBridge_SaveBrightness(void)
{
  uint32_t page_buf[32]; /* 32 * 4 = 128 bytes */
  for (uint32_t i = 0; i < 32; i++)
  {
    page_buf[i] = 0xFFFFFFFFUL;
  }
  page_buf[0] = CONFIG_MAGIC;
  page_buf[1] = brightness_pct;

  HAL_FLASH_Unlock();

  FLASH_EraseInitTypeDef erase = {0};
  uint32_t page_error = 0;
  erase.TypeErase = FLASH_TYPEERASE_PAGEERASE;
  erase.PageAddress = CONFIG_PAGE_ADDR;
  erase.NbPages = 1;
  HAL_FLASHEx_Erase(&erase, &page_error);

  HAL_FLASH_Program(FLASH_TYPEPROGRAM_PAGE, CONFIG_PAGE_ADDR, page_buf);

  HAL_FLASH_Lock();
}

void HBridge_Systick(void)
{
  uint32_t now = HAL_GetTick();

  if (current_mode == HBRIDGE_OFF)
  {
    HAL_GPIO_WritePin(HBRIDGE_CTRL_PORT, HBRIDGE_CTRL_PIN, GPIO_PIN_RESET);
    pwm_window_start = now;
    return;
  }

  uint32_t elapsed = now - pwm_window_start;
  if (elapsed >= PWM_WINDOW_MS)
  {
    pwm_window_start = now;
    elapsed = 0;
  }

  uint32_t duty_ms = (brightness_pct * PWM_WINDOW_MS) / 100U;
  if (elapsed < duty_ms)
  {
    HAL_GPIO_WritePin(HBRIDGE_CTRL_PORT, HBRIDGE_CTRL_PIN, GPIO_PIN_SET);
  }
  else
  {
    HAL_GPIO_WritePin(HBRIDGE_CTRL_PORT, HBRIDGE_CTRL_PIN, GPIO_PIN_RESET);
  }
}
