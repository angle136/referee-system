#include "armor_sensor.h"
#include "adc.h"

static uint32_t armor_last_adc_raw;

void ArmorSensor_Init(void)
{
  (void)HAL_ADCEx_Calibration_Start(&hadc1);
  armor_last_adc_raw = 0U;
}

uint32_t ArmorSensor_ReadAdcRaw(void)
{
  uint32_t adc_raw = armor_last_adc_raw;

  if (HAL_ADC_Start(&hadc1) != HAL_OK)
  {
    return adc_raw;
  }
  if (HAL_ADC_PollForConversion(&hadc1, 10U) == HAL_OK)
  {
    adc_raw = HAL_ADC_GetValue(&hadc1);
  }
  (void)HAL_ADC_Stop(&hadc1);
  armor_last_adc_raw = adc_raw;
  return adc_raw;
}

uint8_t ArmorSensor_ReadDxLevel(void)
{
  return (HAL_GPIO_ReadPin(DX_INPUT_GPIO_Port, DX_INPUT_Pin) == GPIO_PIN_SET) ? 1U : 0U;
}
