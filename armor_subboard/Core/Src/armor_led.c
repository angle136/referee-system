#include "armor_led.h"
#include "armor_config.h"
#include "main.h"

void ArmorLed_SetState(uint8_t team, bool hit)
{
  bool red_on = (team == ARMOR_TEAM_RED) ? !hit : hit;

  HAL_GPIO_WritePin(LED_R_GPIO_Port, LED_R_Pin,
                   red_on ? GPIO_PIN_SET : GPIO_PIN_RESET);
  HAL_GPIO_WritePin(LED_B_GPIO_Port, LED_B_Pin,
                   red_on ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

void ArmorLed_SetTransition(bool red_on)
{
  HAL_GPIO_WritePin(LED_R_GPIO_Port, LED_R_Pin,
                   red_on ? GPIO_PIN_SET : GPIO_PIN_RESET);
  HAL_GPIO_WritePin(LED_B_GPIO_Port, LED_B_Pin,
                   red_on ? GPIO_PIN_RESET : GPIO_PIN_SET);
}
