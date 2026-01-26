#include "button_ctrl.h"
#include "py32f0xx_hal.h"
#include "hbridge.h"

#define BTN1_PORT GPIOA
#define BTN1_PIN  GPIO_PIN_6 /* SW1 */
#define BTN2_PORT GPIOA
#define BTN2_PIN  GPIO_PIN_5 /* SW2 (safety) */

#define BTN_DEBOUNCE_MS   30U
#define DOUBLE_CLICK_MS   400U
#define LONG_PRESS_MS     1000U
#define BR_STEP_MS        75U   /* brightness step interval during long hold (smoothed +50%) */

typedef struct
{
  uint8_t stable;        /* debounced logical level */
  uint8_t last_raw;
  uint32_t last_change;
  uint8_t long_fired;
  uint32_t press_start;
  uint8_t click_count;
  uint32_t last_release;
} btn_t;

static btn_t btn1 = { .stable = 1, .last_raw = 1 };
static btn_t btn2 = { .stable = 1, .last_raw = 1 };
static uint8_t guard_block = 0;
static uint8_t ramp_dir_up = 1;      /* toggles each long press */
static uint32_t last_br_step = 0;
static uint8_t ramp_active = 0;
static uint8_t double_armed = 0;

static void Button_Debounce(btn_t *b, uint8_t raw, uint32_t now)
{
  if (raw != b->last_raw)
  {
    b->last_raw = raw;
    b->last_change = now;
  }

  if ((now - b->last_change) > BTN_DEBOUNCE_MS && raw != b->stable)
  {
    b->stable = raw;
    if (raw == GPIO_PIN_RESET)
    {
      b->press_start = now;
      b->long_fired = 0;
    }
    else
    {
      b->last_release = now;
    }
  }
}

static uint8_t Button_BothPressed(void)
{
  return (btn1.stable == GPIO_PIN_RESET) && (btn2.stable == GPIO_PIN_RESET);
}

static void Handle_BrightnessRamp(uint32_t now)
{
  if (!ramp_active)
  {
    return;
  }

  if ((now - last_br_step) < BR_STEP_MS)
  {
    return;
  }
  last_br_step = now;

  uint8_t br = HBridge_GetBrightness();
  if (ramp_dir_up)
  {
    if (br < 100U)
    {
      br++;
      HBridge_SetBrightness(br);
    }
  }
  else
  {
    if (br > 20U)
    {
      br--;
      HBridge_SetBrightness(br);
    }
  }
}

static void Handle_Btn1_LongStart(void)
{
  ramp_dir_up = !ramp_dir_up; /* toggle direction each long press */
  ramp_active = 1;
  last_br_step = HAL_GetTick();
}

static void Handle_Btn1_LongEnd(void)
{
  ramp_active = 0;
  HBridge_SaveBrightness(); /* persist last value */
}

static void Handle_Btn1_Single(void)
{
  hbridge_mode_t mode = HBridge_GetMode();
  if (mode == HBRIDGE_OFF)
  {
    HBridge_SetMode(HBridge_GetPreferredMode());
  }
  else
  {
    HBridge_SetMode(HBRIDGE_OFF);
  }
}

static void Handle_Btn1_Double(void)
{
  HBridge_TogglePreferredMode();
}

void ButtonCtrl_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct;

  __HAL_RCC_GPIOA_CLK_ENABLE();

  GPIO_InitStruct.Pin = BTN1_PIN | BTN2_PIN;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(BTN1_PORT, &GPIO_InitStruct);
}

void ButtonCtrl_Task(void)
{
  uint32_t now = HAL_GetTick();

  uint8_t raw1 = HAL_GPIO_ReadPin(BTN1_PORT, BTN1_PIN);
  uint8_t raw2 = HAL_GPIO_ReadPin(BTN2_PORT, BTN2_PIN);

  Button_Debounce(&btn1, raw1, now);
  Button_Debounce(&btn2, raw2, now);

  if (Button_BothPressed())
  {
    guard_block = 1;
    btn1.click_count = 0;
    ramp_active = 0;
    return;
  }

  if (guard_block)
  {
    if (btn1.stable == GPIO_PIN_SET && btn2.stable == GPIO_PIN_SET)
    {
      guard_block = 0;
    }
    else
    {
      return;
    }
  }

  /* Long press detection */
  if (btn1.stable == GPIO_PIN_RESET && !btn1.long_fired)
  {
    if ((now - btn1.press_start) >= LONG_PRESS_MS)
    {
      btn1.long_fired = 1;
      btn1.click_count = 0;
      Handle_Btn1_LongStart();
    }
  }

  if (btn1.stable == GPIO_PIN_SET && btn1.long_fired && ramp_active)
  {
    Handle_Btn1_LongEnd();
  }

  Handle_BrightnessRamp(now);

  /* Click / double-click handling */
  if (btn1.stable == GPIO_PIN_SET && !btn1.long_fired)
  {
    /* released */
    if (btn1.click_count == 0)
    {
      btn1.click_count = 1;
      btn1.last_release = now;
      double_armed = 1;
    }
    else if (btn1.click_count == 1)
    {
      /* second release inside window -> double */
      if (double_armed && (now - btn1.last_release) <= DOUBLE_CLICK_MS)
      {
        Handle_Btn1_Double();
        btn1.click_count = 0;
        double_armed = 0;
        btn1.last_release = 0;
      }
    }
  }

  /* Single click timeout */
  if (btn1.click_count == 1 && (now - btn1.last_release) > DOUBLE_CLICK_MS)
  {
    Handle_Btn1_Single();
    btn1.click_count = 0;
    double_armed = 0;
  }
}
