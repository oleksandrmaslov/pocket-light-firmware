#include "hbridge.h"
#include "py32f0xx_hal.h"

#define HBRIDGE_NSLP_PORT GPIOA
#define HBRIDGE_NSLP_PIN  GPIO_PIN_0
#define HBRIDGE_IN1_PORT  GPIOA
#define HBRIDGE_IN1_PIN   GPIO_PIN_1 /* Red diode */
#define HBRIDGE_IN2_PORT  GPIOA
#define HBRIDGE_IN2_PIN   GPIO_PIN_2 /* White diode */
#define HBRIDGE_BTN_PORT  GPIOA
#define HBRIDGE_BTN_PIN   GPIO_PIN_6

#define BTN_DEBOUNCE_MS   30U
#define SWITCH_PAUSE_MS   2U

static hbridge_mode_t current_mode = HBRIDGE_OFF;
static uint32_t last_btn_tick = 0;
static uint8_t last_btn_state = 1;

static void HBridge_UpdatePins(hbridge_mode_t mode)
{
  /* Disable bridge before toggling */
  HAL_GPIO_WritePin(HBRIDGE_NSLP_PORT, HBRIDGE_NSLP_PIN, GPIO_PIN_RESET);
  HAL_Delay(SWITCH_PAUSE_MS);

  switch (mode)
  {
  case HBRIDGE_FORWARD:
    HAL_GPIO_WritePin(HBRIDGE_IN1_PORT, HBRIDGE_IN1_PIN, GPIO_PIN_SET);
    HAL_GPIO_WritePin(HBRIDGE_IN2_PORT, HBRIDGE_IN2_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(HBRIDGE_NSLP_PORT, HBRIDGE_NSLP_PIN, GPIO_PIN_SET);
    break;
  case HBRIDGE_REVERSE:
    HAL_GPIO_WritePin(HBRIDGE_IN1_PORT, HBRIDGE_IN1_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(HBRIDGE_IN2_PORT, HBRIDGE_IN2_PIN, GPIO_PIN_SET);
    HAL_GPIO_WritePin(HBRIDGE_NSLP_PORT, HBRIDGE_NSLP_PIN, GPIO_PIN_SET);
    break;
  case HBRIDGE_OFF:
  default:
    HAL_GPIO_WritePin(HBRIDGE_IN1_PORT, HBRIDGE_IN1_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(HBRIDGE_IN2_PORT, HBRIDGE_IN2_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(HBRIDGE_NSLP_PORT, HBRIDGE_NSLP_PIN, GPIO_PIN_RESET);
    break;
  }
}

void HBridge_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct;

  __HAL_RCC_GPIOA_CLK_ENABLE();

  /* nsleep + outputs */
  GPIO_InitStruct.Pin = HBRIDGE_NSLP_PIN | HBRIDGE_IN1_PIN | HBRIDGE_IN2_PIN;
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
}

void HBridge_SetMode(hbridge_mode_t mode)
{
  current_mode = mode;
  HBridge_UpdatePins(current_mode);
}

void HBridge_Task(void)
{
  /* placeholder for periodic safety, currently no-op */
}

void HBridge_ButtonTask(void)
{
  uint8_t state = HAL_GPIO_ReadPin(HBRIDGE_BTN_PORT, HBRIDGE_BTN_PIN);
  uint32_t now = HAL_GetTick();

  if (state != last_btn_state)
  {
    last_btn_tick = now;
    last_btn_state = state;
    return;
  }

  if (state == GPIO_PIN_RESET && (now - last_btn_tick) > BTN_DEBOUNCE_MS)
  {
    last_btn_tick = now + 300; /* simple lockout */
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
