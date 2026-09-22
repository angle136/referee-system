#ifndef _REFEREE_MAIN_ARMOR_COUNTER_H_
#define _REFEREE_MAIN_ARMOR_COUNTER_H_

#include <stdbool.h>
#include <stdint.h>

#include "referee_config.h"

typedef enum
{
    ARMOR_COUNTER_BASELINE = 0,
    ARMOR_COUNTER_NO_CHANGE,
    ARMOR_COUNTER_HIT,
    ARMOR_COUNTER_RESYNC,
    ARMOR_COUNTER_RESET_PENDING,
    ARMOR_COUNTER_RESET_ACK,
    ARMOR_COUNTER_INVALID
} armor_counter_result_t;

typedef struct
{
    uint16_t small_hit_count;
    uint16_t big_hit_count;
    uint8_t  reset_epoch;
    bool     baseline_valid;
} armor_counter_port_t;

typedef struct
{
    armor_counter_port_t ports[REFEREE_MAIN_ARMOR_COUNT];
    uint32_t resync_count;
    uint8_t  reset_epoch;
    uint8_t  reset_pending_mask;
    uint8_t  reset_sent_mask;
    uint8_t  reset_sequence[REFEREE_MAIN_ARMOR_COUNT];
} armor_counter_manager_t;

typedef struct
{
    uint16_t small_hit_delta;
    uint16_t big_hit_delta;
} armor_counter_delta_t;

void armor_counter_init(armor_counter_manager_t *manager);
void armor_counter_request_reset(armor_counter_manager_t *manager);
void armor_counter_mark_reset_sent(armor_counter_manager_t *manager,
                                   uint8_t port_id,
                                   uint8_t reset_sequence);
bool armor_counter_get_reset_request(const armor_counter_manager_t *manager,
                                     uint8_t port_id,
                                     uint8_t *reset_epoch);
armor_counter_result_t armor_counter_update(armor_counter_manager_t *manager,
                                            uint8_t port_id,
                                            uint16_t small_hit_count,
                                            uint16_t big_hit_count,
                                            uint8_t reset_epoch,
                                            bool reset_ack,
                                            uint8_t reset_sequence,
                                            armor_counter_delta_t *delta);
uint32_t armor_counter_get_resync_count(const armor_counter_manager_t *manager);

#endif
