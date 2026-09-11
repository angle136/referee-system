#ifndef _REFEREE_MAIN_STATE_H_
#define _REFEREE_MAIN_STATE_H_

#include <stdint.h>

typedef struct
{
    uint16_t current_hp;
    uint16_t maximum_hp;
    uint8_t  online_mask;
    uint8_t  last_hit_armor_id;
    uint8_t  last_hit_dx;
    uint16_t last_hit_ax_raw;
    uint32_t hit_count;
} referee_state_snapshot_t;

int referee_state_init(void);
void referee_state_mark_armor_seen(uint8_t armor_id, uint16_t ax_raw, uint8_t dx);
void referee_state_apply_hit(uint8_t armor_id, uint16_t ax_raw, uint8_t dx);
void referee_state_get_snapshot(referee_state_snapshot_t *snapshot);

#endif
