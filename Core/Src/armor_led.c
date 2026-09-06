#include "armor_led.h"
#include "main.h"

void ArmorLed_SetHit(bool hit)
{
  HAL_GPIO_WritePin(LED_R_GPIO_Port, LED_R_Pin, hit ? GPIO_PIN_SET : GPIO_PIN_RESET);
  HAL_GPIO_WritePin(LED_B_GPIO_Port, LED_B_Pin, hit ? GPIO_PIN_RESET : GPIO_PIN_SET);
}
