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
#define HBRIDGE_FADE_MS        500U   /* reserved for future use */
#define HBRIDGE_FADE_ON_MS    1000U   /* smooth turn-on from OFF */

#define BRIGHT_MIN_PCT    20U
#define BRIGHT_MAX_PCT    100U

#define CONFIG_MAGIC      0xBEEFCAFEUL
#define CONFIG_PAGE_ADDR  0x08004C00UL /* aligned page near end of 20KB flash */

#define PWM_WINDOW_MS     10U          /* 100 Hz software PWM via SysTick */
#define HBRIDGE_FADE_STEPS     32U     /* perceptual steps for power-on fade */

/* Gamma-like curve (gamma ~2.2) normalized to 0..100% for smoother low-end ramp */
static const uint8_t fade_curve_pct[HBRIDGE_FADE_STEPS] =
{
  0, 0, 0, 1, 1, 2, 3, 4,
  5, 7, 8, 10, 12, 15, 17, 20,
  23, 27, 30, 34, 38, 42, 47, 52,
  57, 62, 68, 74, 80, 86, 93, 100
};

static volatile hbridge_mode_t current_mode = HBRIDGE_OFF;
static hbridge_mode_t preferred_mode = HBRIDGE_FORWARD;
static uint8_t brightness_pct = BRIGHT_MAX_PCT; /* target (user) brightness */
static volatile uint8_t pwm_pct = 0;            /* actual PWM value on PA4 */
static uint32_t pwm_window_start = 0;
static struct
{
  volatile uint8_t active;
  uint32_t start_ms;
  uint32_t duration_ms;
  uint8_t from_pct;
  uint8_t to_pct;
} fade = {0};

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
      pwm_pct = brightness_pct;
      return;
    }
  }
  brightness_pct = BRIGHT_MAX_PCT;
  pwm_pct = brightness_pct;
}

static void HBridge_StartFade(uint8_t target_pct, uint32_t duration_ms)
{
  if (target_pct < BRIGHT_MIN_PCT) target_pct = BRIGHT_MIN_PCT;
  if (target_pct > BRIGHT_MAX_PCT) target_pct = BRIGHT_MAX_PCT;
  fade.start_ms = HAL_GetTick();
  fade.duration_ms = duration_ms ? duration_ms : 1;
  fade.from_pct = pwm_pct;
  fade.to_pct = target_pct;

  if (fade.from_pct == fade.to_pct)
  {
    pwm_pct = target_pct;
    fade.active = 0;
    return;
  }

  fade.active = 1;
}

void HBridge_Init(void)
{
  HBridge_GPIO_Init();
  HBridge_LoadBrightness();

  HBridge_UpdatePins(HBRIDGE_OFF);
}

void HBridge_SetMode(hbridge_mode_t mode)
{
  hbridge_mode_t prev = current_mode;
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

  if (prev == HBRIDGE_OFF && mode != HBRIDGE_OFF)
  {
    uint8_t target = brightness_pct; /* preserve user setting before ramp */
    fade.active = 0;
    pwm_pct = 0; /* start dark to avoid visible flash */
    pwm_window_start = HAL_GetTick();

    current_mode = mode;
    HBridge_UpdatePins(current_mode);
    HBridge_StartFade(target, HBRIDGE_FADE_ON_MS);
  }
  else
  {
    current_mode = mode;
    HBridge_UpdatePins(current_mode);

    if (current_mode != HBRIDGE_OFF)
    {
      fade.active = 0; /* keep current brightness, no fade needed */
    }
    else
    {
      fade.active = 0;
      pwm_pct = 0;
    }
  }

  SEGGER_RTT_printf(0, "H-bridge mode: %d\r\n", current_mode);
}

void HBridge_Task(void)
{
  /* no-op: fade handled in SysTick for 1ms resolution */
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
  fade.active = 0; /* explicit set cancels fade */
  if (current_mode != HBRIDGE_OFF)
  {
    pwm_pct = brightness_pct; /* apply immediately when active */
  }
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

  /* update fade with 1 ms resolution */
  if (fade.active)
  {
    uint32_t elapsed = now - fade.start_ms;
    if (elapsed >= fade.duration_ms)
    {
      pwm_pct = fade.to_pct;
      fade.active = 0;
    }
    else
    {
      uint32_t step = (elapsed * (HBRIDGE_FADE_STEPS - 1U)) / fade.duration_ms;
      if (step >= (HBRIDGE_FADE_STEPS - 1U))
      {
        step = HBRIDGE_FADE_STEPS - 1U;
      }
      uint8_t curve = fade_curve_pct[step];
      int16_t delta = (int16_t)fade.to_pct - (int16_t)fade.from_pct;
      int32_t value = (int32_t)fade.from_pct + ((int32_t)delta * (int32_t)curve) / 100;
      if (value < 0) value = 0;
      if (value > 100) value = 100;
      pwm_pct = (uint8_t)value;
    }
  }

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

  uint32_t duty_ms = (pwm_pct * PWM_WINDOW_MS) / 100U;
  if (elapsed < duty_ms)
  {
    HAL_GPIO_WritePin(HBRIDGE_CTRL_PORT, HBRIDGE_CTRL_PIN, GPIO_PIN_SET);
  }
  else
  {
    HAL_GPIO_WritePin(HBRIDGE_CTRL_PORT, HBRIDGE_CTRL_PIN, GPIO_PIN_RESET);
  }
}
