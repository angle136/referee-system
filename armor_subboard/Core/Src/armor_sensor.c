#include "armor_sensor.h"
#include "adc.h"

volatile uint32_t armor_adc_raw_debug;
volatile uint32_t armor_adc_voltage_mv_debug;

void ArmorSensor_Init(void)
{
  (void)HAL_ADCEx_Calibration_Start(&hadc1);
  armor_adc_raw_debug = 0U;
  armor_adc_voltage_mv_debug = 0U;
}

uint32_t ArmorSensor_ReadAdcRaw(void)
{
  uint32_t adc_raw = armor_adc_raw_debug;

  if (HAL_ADC_Start(&hadc1) != HAL_OK)
  {
    return adc_raw;
  }

  if (HAL_ADC_PollForConversion(&hadc1, 10U) == HAL_OK)
  {
    adc_raw = HAL_ADC_GetValue(&hadc1);
  }

  (void)HAL_ADC_Stop(&hadc1);

  armor_adc_raw_debug = adc_raw;
  armor_adc_voltage_mv_debug = (adc_raw * 3300U + 2047U) / 4095U;

  return adc_raw;
}

uint8_t ArmorSensor_ReadDxLevel(void)
{
  return (HAL_GPIO_ReadPin(DX_INPUT_GPIO_Port, DX_INPUT_Pin) == GPIO_PIN_SET) ? 1U : 0U;
}
