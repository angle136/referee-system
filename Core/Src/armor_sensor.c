#include "armor_sensor.h"
#include "adc.h"
#include "armor_debug.h"

void ArmorSensor_Init(void)
{
  if (HAL_ADCEx_Calibration_Start(&hadc1) != HAL_OK)
  {
    armor_adc_error_count++;
  }
}

uint32_t ArmorSensor_ReadAdcRaw(void)
{
  uint32_t adc_raw = armor_adc_raw;

  if (HAL_ADC_Start(&hadc1) != HAL_OK)
  {
    armor_adc_error_count++;
    return adc_raw;
  }
  if (HAL_ADC_PollForConversion(&hadc1, 10U) == HAL_OK)
  {
    adc_raw = HAL_ADC_GetValue(&hadc1);
  }
  else
  {
    armor_adc_error_count++;
  }
  if (HAL_ADC_Stop(&hadc1) != HAL_OK)
  {
    armor_adc_error_count++;
  }
  return adc_raw;
}

uint8_t ArmorSensor_ReadDxLevel(void)
{
  return (HAL_GPIO_ReadPin(DX_INPUT_GPIO_Port, DX_INPUT_Pin) == GPIO_PIN_SET) ? 1U : 0U;
}
