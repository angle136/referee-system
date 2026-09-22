#include "armor_led.h"
#include "armor_config.h"
#include "main.h"

#define ARMOR_LED_HIT_PHASE_MS 100U
#define ARMOR_LED_HIT_TOTAL_MS (ARMOR_LED_HIT_PHASE_MS * 3U)

static bool armor_hit_latched;
static uint32_t armor_hit_started_tick;

void ArmorLed_SetState(uint8_t team, bool hit)
{
  uint32_t now = HAL_GetTick();
  bool team_led_on = true;

  if (!hit)
  {
    armor_hit_latched = false;
  }
  else if (!armor_hit_latched)
  {
    armor_hit_started_tick = now;
    armor_hit_latched = true;
  }

  if (armor_hit_latched)
  {
    uint32_t elapsed = (uint32_t)(now - armor_hit_started_tick);
    if (elapsed < ARMOR_LED_HIT_TOTAL_MS)
    {
      uint32_t phase = elapsed / ARMOR_LED_HIT_PHASE_MS;
      /* Hit indication: off -> on -> off, then restore team color. */
      team_led_on = (phase == 1U);
    }
  }

  bool red_on = team_led_on && team == ARMOR_TEAM_RED;
  bool blue_on = team_led_on && team == ARMOR_TEAM_BLUE;

  HAL_GPIO_WritePin(LED_R_GPIO_Port, LED_R_Pin,
                   red_on ? GPIO_PIN_SET : GPIO_PIN_RESET);
  HAL_GPIO_WritePin(LED_B_GPIO_Port, LED_B_Pin,
                   blue_on ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void ArmorLed_SetTransition(bool red_on)
{
  HAL_GPIO_WritePin(LED_R_GPIO_Port, LED_R_Pin,
                   red_on ? GPIO_PIN_SET : GPIO_PIN_RESET);
  HAL_GPIO_WritePin(LED_B_GPIO_Port, LED_B_Pin,
                   red_on ? GPIO_PIN_RESET : GPIO_PIN_SET);
}
