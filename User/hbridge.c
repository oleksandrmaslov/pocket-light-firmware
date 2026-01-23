#include "hbridge.h"
#include "py32f0xx_hal.h"
#include "SEGGER_RTT.h"

#define HBRIDGE_NSLP_PORT GPIOA
#define HBRIDGE_NSLP_PIN  GPIO_PIN_0
#define HBRIDGE_IN1_PORT  GPIOA
#define HBRIDGE_IN1_PIN   GPIO_PIN_1 /* Red diode */
#define HBRIDGE_IN2_PORT  GPIOA
#define HBRIDGE_IN2_PIN   GPIO_PIN_2 /* White diode */
#define HBRIDGE_CTRL_PORT GPIOA
#define HBRIDGE_CTRL_PIN  GPIO_PIN_4 /* Enable / CTRL for LED driver (high = on) */
#define HBRIDGE_BTN_PORT  GPIOA
#define HBRIDGE_BTN_PIN   GPIO_PIN_6

#define BTN_DEBOUNCE_MS   30U
#define SWITCH_PAUSE_MS   5U
#define DRIVER_PAUSE_MS   1U

static hbridge_mode_t current_mode = HBRIDGE_OFF;
static uint32_t last_btn_tick = 0;
static uint8_t last_btn_state = 1;
static uint8_t press_armed = 1;

static void HBridge_UpdatePins(hbridge_mode_t mode)
{
  /* Step 0: shut down LED driver first so load is disconnected */
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

  /* Step 2: set direction lines while still unpowered */
  if (mode == HBRIDGE_FORWARD)
  {
    HAL_GPIO_WritePin(HBRIDGE_IN1_PORT, HBRIDGE_IN1_PIN, GPIO_PIN_SET);
    HAL_GPIO_WritePin(HBRIDGE_IN2_PORT, HBRIDGE_IN2_PIN, GPIO_PIN_RESET);
  }
  else /* REVERSE */
  {
    HAL_GPIO_WritePin(HBRIDGE_IN1_PORT, HBRIDGE_IN1_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(HBRIDGE_IN2_PORT, HBRIDGE_IN2_PIN, GPIO_PIN_SET);
  }

  /* Step 3: wake bridge */
  HAL_GPIO_WritePin(HBRIDGE_NSLP_PORT, HBRIDGE_NSLP_PIN, GPIO_PIN_SET);
  HAL_Delay(DRIVER_PAUSE_MS);

  /* Step 4: reassert direction to avoid latch-up during wake */
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

  /* Step 5: power LED driver once bridge state is stable */
  HAL_Delay(DRIVER_PAUSE_MS);
  HAL_GPIO_WritePin(HBRIDGE_CTRL_PORT, HBRIDGE_CTRL_PIN, GPIO_PIN_SET);
}

void HBridge_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct;

  __HAL_RCC_GPIOA_CLK_ENABLE();

  /* nsleep + outputs + ctrl */
  GPIO_InitStruct.Pin = HBRIDGE_NSLP_PIN | HBRIDGE_IN1_PIN | HBRIDGE_IN2_PIN | HBRIDGE_CTRL_PIN;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(HBRIDGE_NSLP_PORT, &GPIO_InitStruct);

  /* button */
  GPIO_InitStruct.Pin = HBRIDGE_BTN_PIN;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(HBRIDGE_BTN_PORT, &GPIO_InitStruct);

  HBridge_UpdatePins(HBRIDGE_OFF);

  /* Quick self-test: forward 0.5s, reverse 0.5s, off */
  // HBridge_SetMode(HBRIDGE_FORWARD);
  // HAL_Delay(500);
  // HBridge_SetMode(HBRIDGE_REVERSE);
  // HAL_Delay(500);
  // HBridge_SetMode(HBRIDGE_OFF);
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
  SEGGER_RTT_printf(0, "H-bridge mode: %d\r\n", current_mode);
}

void HBridge_Task(void)
{
  /* placeholder for periodic safety, currently no-op */
}

void HBridge_ButtonTask(void)
{
  uint8_t state = HAL_GPIO_ReadPin(HBRIDGE_BTN_PORT, HBRIDGE_BTN_PIN);
  uint32_t now = HAL_GetTick();

  /* debounce edge detection */
  if (state != last_btn_state)
  {
    last_btn_tick = now;
    last_btn_state = state;
  }

  /* re-arm only after stable release */
  if (!press_armed && state == GPIO_PIN_SET && (now - last_btn_tick) > BTN_DEBOUNCE_MS)
  {
    press_armed = 1;
  }

  if (press_armed && state == GPIO_PIN_RESET && (now - last_btn_tick) > BTN_DEBOUNCE_MS)
  {
    press_armed = 0; /* consume press until next stable release */
    /* Cycle modes: OFF -> FWD -> REV -> OFF ... */
    switch (current_mode)
    {
    case HBRIDGE_OFF:
      HBridge_SetMode(HBRIDGE_FORWARD);
      break;
    case HBRIDGE_FORWARD:
      HBridge_SetMode(HBRIDGE_REVERSE);
      break;
    default:
      HBridge_SetMode(HBRIDGE_OFF);
      break;
    }
  }
}
