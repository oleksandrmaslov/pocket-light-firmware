#include "ws2812_ctrl.h"
#include "light_ws2812_cortex.h"
#include "ws2812_config.h"
#include "py32f0xx_hal.h"

/* How often to refresh battery measurement (ms) */
#define VBAT_SAMPLE_PERIOD_MS   10U

/* Nominal internal reference voltage in mV (from LL ADC header) */
#define VREFINT_NOMINAL_MV      1200U

/* Simple IIR filter factor for voltage smoothing (1/4 new, 3/4 old) */
#define VBAT_FILTER_SHIFT       2U

typedef struct
{
  uint8_t g;
  uint8_t r;
  uint8_t b;
} cRGB;

static ADC_HandleTypeDef hadc;
static cRGB led;
static uint32_t vdd_mv = 3300U;
static uint8_t soc_pct = 0;
static uint32_t last_sample = 0xFFFFFFFFUL - VBAT_SAMPLE_PERIOD_MS; /* force immediate first sample */
static uint8_t ws_enabled = 1;

static void WS_SendOff(void)
{
  led.r = led.g = led.b = 0;
  ws2812_sendarray((uint8_t *)&led, sizeof(led));
}

static void VBat_AdcInit(void)
{
  __HAL_RCC_ADC_FORCE_RESET();
  __HAL_RCC_ADC_RELEASE_RESET();
  __HAL_RCC_ADC_CLK_ENABLE();

  hadc.Instance = ADC1;
  if (HAL_ADCEx_Calibration_Start(&hadc) != HAL_OK)
  {
    return;
  }

  hadc.Init.ClockPrescaler        = ADC_CLOCK_SYNC_PCLK_DIV1;
  hadc.Init.Resolution            = ADC_RESOLUTION_12B;
  hadc.Init.DataAlign             = ADC_DATAALIGN_RIGHT;
  hadc.Init.ScanConvMode          = ADC_SCAN_DIRECTION_BACKWARD;
  hadc.Init.EOCSelection          = ADC_EOC_SINGLE_CONV;
  hadc.Init.LowPowerAutoWait      = DISABLE;
  hadc.Init.ContinuousConvMode    = DISABLE;
  hadc.Init.DiscontinuousConvMode = DISABLE;
  hadc.Init.ExternalTrigConv      = ADC_SOFTWARE_START;
  hadc.Init.ExternalTrigConvEdge  = ADC_EXTERNALTRIGCONVEDGE_NONE;
  hadc.Init.Overrun               = ADC_OVR_DATA_PRESERVED;
  hadc.Init.SamplingTimeCommon    = ADC_SAMPLETIME_239CYCLES_5; /* long sample for internal ref */
  HAL_ADC_Init(&hadc);

  ADC_ChannelConfTypeDef sConfig = {0};
  sConfig.Rank    = ADC_RANK_CHANNEL_NUMBER;
  sConfig.Channel = ADC_CHANNEL_VREFINT;
  HAL_ADC_ConfigChannel(&hadc, &sConfig);

  /* Allow Vrefint path to settle */
  HAL_Delay(1);
}

static uint16_t VBat_ReadVrefRaw(void)
{
  HAL_ADC_Start(&hadc);
  HAL_ADC_PollForConversion(&hadc, 10);
  uint16_t raw = HAL_ADC_GetValue(&hadc);
  HAL_ADC_Stop(&hadc);
  return raw;
}

static uint32_t VBat_ComputeVddMv(uint16_t vref_raw)
{
  if (vref_raw == 0)
    return vdd_mv;
  /* Vdd = Vref_nominal * fullscale / vref_raw */
  uint32_t new_mv = (uint32_t)VREFINT_NOMINAL_MV * 4095U / vref_raw;
  /* IIR filter to smooth noise */
  return (vdd_mv * ((1U << VBAT_FILTER_SHIFT) - 1U) + new_mv) >> VBAT_FILTER_SHIFT;
}

static uint8_t VBat_VoltageToPercent(uint32_t mv)
{
  if (mv >= 4200U)
    return 100;
  if (mv <= 3000U)
    return 0;

  if (mv > 3700U)
  {
    /* 3.7V..4.2V : 60..100% */
    return 60U + (uint8_t)((mv - 3700U) * 40U / 500U);
  }
  else if (mv > 3500U)
  {
    /* 3.5V..3.7V : 30..60% */
    return 30U + (uint8_t)((mv - 3500U) * 30U / 200U);
  }
  else if (mv > 3300U)
  {
    /* 3.3V..3.5V : 10..30% */
    return 10U + (uint8_t)((mv - 3300U) * 20U / 200U);
  }
  else
  {
    /* 3.0V..3.3V : 0..10% */
    return (uint8_t)((mv - 3000U) * 10U / 300U);
  }
}

static void WS_SetColorForPercent(uint32_t now_ms)
{
  uint8_t r = 0, g = 0, b = 0;

  if (soc_pct <= 15U)
  {
    /* Blink red when critically low */
    if (((now_ms / 400U) & 0x1U) == 0U)
    {
      r = 255;
    }
  }
  else if (soc_pct <= 25U)
  {
    /* Solid red from 25% down to 15% */
    r = 255;
  }
  else if (soc_pct <= 50U)
  {
    /* Yellow at 50% fading to red at 25% */
    r = 255;
    g = (uint8_t)(((uint16_t)(soc_pct - 25U) * 255U) / (50U - 25U));
  }
  else
  {
    /* Green to yellow: green fixed, red fades in */
    g = 255;
    r = (uint8_t)(((uint16_t)(100U - soc_pct) * 255U) / (100U - 50U));
  }

  led.r = r;
  led.g = g;
  led.b = b;
  ws2812_sendarray((uint8_t *)&led, sizeof(led));
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

  VBat_AdcInit();
  WS_SendOff();
}

void WS2812_Ctrl_SetEnabled(uint8_t enable)
{
  ws_enabled = enable ? 1U : 0U;
  if (!ws_enabled)
  {
    WS_SendOff();
  }
}

void WS2812_Ctrl_Task(void)
{
  uint32_t now = HAL_GetTick();

  if (ws_enabled && (now - last_sample >= VBAT_SAMPLE_PERIOD_MS))
  {
    uint16_t raw = VBat_ReadVrefRaw();
    vdd_mv = VBat_ComputeVddMv(raw);
    soc_pct = VBat_VoltageToPercent(vdd_mv);
    last_sample = now;
  }

  if (ws_enabled)
  {
    WS_SetColorForPercent(now);
  }
}
