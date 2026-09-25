#ifndef _REFEREE_MAIN_CONTROL_H_
#define _REFEREE_MAIN_CONTROL_H_

#include <stdint.h>

typedef struct
{
    uint32_t referee_tx_count;
    uint32_t referee_tx_error_count;
    uint32_t armor_config_tx_count;
    uint32_t armor_config_tx_error_count;
    uint32_t armor_counter_resync_count;
    uint16_t last_command_id;
} referee_control_diagnostics_t;

void robot_control_init(void);
void referee_control_get_diagnostics(referee_control_diagnostics_t *diagnostics);
void referee_control_toggle_team(void);
void referee_control_reset_system(void);
void referee_control_start_adc_debug(void);
void referee_control_stop_adc_debug(void);
uint8_t referee_control_is_adc_debug_active(void);

#endif /* _REFEREE_MAIN_CONTROL_H_ */
