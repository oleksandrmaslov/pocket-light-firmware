#include "button_ctrl.h"
#include "py32f0xx_hal.h"
#include "hbridge.h"
#include "ws2812_ctrl.h"

#define BTN1_PORT GPIOA
#define BTN1_PIN  GPIO_PIN_6 /* SW1 */
#define BTN2_PORT GPIOA
#define BTN2_PIN  GPIO_PIN_5 /* SW2 (safety) */

#define BTN_DEBOUNCE_MS       25U
#define DOUBLE_CLICK_MS       400U
#define LONG_PRESS_MS_SW1     1000U
#define LONG_PRESS_MS_SW2     800U
#define BR_STEP_MS        75U   /* brightness step interval during long hold (smoothed +50%) */
#define BATT_INDICATE_MS  5000U

typedef struct
{
  uint8_t stable;        /* debounced logical level */
  uint8_t last_raw;
  uint32_t last_change;
  uint8_t long_fired;
  uint32_t press_start;
  uint32_t last_release;
  uint8_t fell_evt;
  uint8_t rise_evt;
} btn_t;

static btn_t btn1 = { .stable = 1, .last_raw = 1 };
static btn_t btn2 = { .stable = 1, .last_raw = 1 };
static uint8_t ramp_dir_up = 1;      /* toggles each long press */
static uint32_t last_br_step = 0;
static uint8_t ramp_active = 0;
static uint8_t btn1_single_pending = 0;
static uint32_t btn1_single_deadline = 0;

static void Button_Debounce(btn_t *b, uint8_t raw, uint32_t now)
{
  b->fell_evt = 0;
  b->rise_evt = 0;

  if (raw != b->last_raw)
  {
    b->last_raw = raw;
    b->last_change = now;
  }

  if ((now - b->last_change) >= BTN_DEBOUNCE_MS && raw != b->stable)
  {
    b->stable = raw;
    if (raw == GPIO_PIN_RESET)
    {
      b->press_start = now;
      b->long_fired = 0;
      b->fell_evt = 1;
    }
    else
    {
      b->last_release = now;
      b->rise_evt = 1;
    }
  }
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

static void Handle_Btn2_Short(void)
{
  ramp_active = 0;
  HBridge_SetMode(HBRIDGE_OFF);
  WS2812_Ctrl_SetEnabled(0);
}

static void Handle_Btn2_Long(void)
{
  WS2812_Ctrl_RequestBatteryIndication(BATT_INDICATE_MS);
}

/* SW2 short is executed immediately on press to guarantee instant shutdown */
static void Handle_Btn2_ShortImmediate(void)
{
  ramp_active = 0;
  HBridge_SetMode(HBRIDGE_OFF);
  WS2812_Ctrl_SetEnabled(0);
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

  /* Long press detection */
  if (btn1.stable == GPIO_PIN_RESET && !btn1.long_fired)
  {
    if ((now - btn1.press_start) >= LONG_PRESS_MS_SW1)
    {
      btn1.long_fired = 1;
      btn1_single_pending = 0;
      Handle_Btn1_LongStart();
    }
  }

  if (btn1.rise_evt)
  {
    if (btn1.long_fired)
    {
      Handle_Btn1_LongEnd();
      btn1.long_fired = 0;
    }
    else
    {
      /* release of short press -> start or complete click combo */
      if (btn1_single_pending && (int32_t)(now - btn1_single_deadline) <= 0)
      {
        Handle_Btn1_Double();
        btn1_single_pending = 0;
      }
      else
      {
        btn1_single_pending = 1;
        btn1_single_deadline = now + DOUBLE_CLICK_MS;
      }
    }
  }

  Handle_BrightnessRamp(now);

  if (btn1_single_pending && (int32_t)(now - btn1_single_deadline) >= 0)
  {
    Handle_Btn1_Single();
    btn1_single_pending = 0;
  }

  /* SW2 handling */
  if (btn2.fell_evt)
  {
    /* instant power-off on any SW2 press */
    Handle_Btn2_ShortImmediate();
    btn2.long_fired = 0;
    btn2.press_start = now;
  }

  if (btn2.stable == GPIO_PIN_RESET && !btn2.long_fired)
  {
    if ((now - btn2.press_start) >= LONG_PRESS_MS_SW2)
    {
      btn2.long_fired = 1;
      Handle_Btn2_Long();
    }
  }

  if (btn2.rise_evt)
  {
    if (!btn2.long_fired)
    {
      /* already powered off on press, keep behavior consistent */
      Handle_Btn2_Short();
    }
    btn2.long_fired = 0;
  }

  /* clear edge latches after processing */
  btn1.fell_evt = btn1.rise_evt = 0;
  btn2.fell_evt = btn2.rise_evt = 0;
}
