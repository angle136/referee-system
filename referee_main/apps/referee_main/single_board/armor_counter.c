#include "armor_counter.h"

#include <string.h>

static void armor_counter_set_baseline(armor_counter_port_t *port,
                                       uint16_t small_hit_count,
                                       uint16_t big_hit_count,
                                       uint8_t reset_epoch)
{
    port->small_hit_count = small_hit_count;
    port->big_hit_count = big_hit_count;
    port->reset_epoch = reset_epoch;
    port->baseline_valid = true;
}

void armor_counter_init(armor_counter_manager_t *manager)
{
    if (manager != 0)
    {
        memset(manager, 0, sizeof(*manager));
    }
}

void armor_counter_request_reset(armor_counter_manager_t *manager)
{
    if (manager == 0)
    {
        return;
    }

    manager->reset_epoch++;
    manager->reset_pending_mask =
        (uint8_t)((1UL << REFEREE_MAIN_ARMOR_COUNT) - 1UL);
    manager->reset_sent_mask = 0U;
    for (uint8_t port_id = 0U; port_id < REFEREE_MAIN_ARMOR_COUNT; port_id++)
    {
        manager->ports[port_id].baseline_valid = false;
    }
}

void armor_counter_mark_reset_sent(armor_counter_manager_t *manager,
                                   uint8_t port_id,
                                   uint8_t reset_sequence)
{
    if (manager == 0 || port_id >= REFEREE_MAIN_ARMOR_COUNT)
    {
        return;
    }
    manager->reset_sequence[port_id] = reset_sequence;
    manager->reset_sent_mask |= (uint8_t)(1U << port_id);
}

bool armor_counter_get_reset_request(const armor_counter_manager_t *manager,
                                     uint8_t port_id,
                                     uint8_t *reset_epoch)
{
    if (manager == 0 || reset_epoch == 0 ||
        port_id >= REFEREE_MAIN_ARMOR_COUNT)
    {
        return false;
    }

    *reset_epoch = manager->reset_epoch;
    return (manager->reset_pending_mask & (uint8_t)(1U << port_id)) != 0U;
}

armor_counter_result_t armor_counter_update(armor_counter_manager_t *manager,
                                            uint8_t port_id,
                                            uint16_t small_hit_count,
                                            uint16_t big_hit_count,
                                            uint8_t reset_epoch,
                                            bool reset_ack,
                                            uint8_t reset_sequence,
                                            armor_counter_delta_t *delta)
{
    armor_counter_port_t *port;
    uint16_t small_delta;
    uint16_t big_delta;

    if (manager == 0 || delta == 0 || port_id >= REFEREE_MAIN_ARMOR_COUNT)
    {
        return ARMOR_COUNTER_INVALID;
    }

    delta->small_hit_delta = 0U;
    delta->big_hit_delta = 0U;
    port = &manager->ports[port_id];

    if ((manager->reset_pending_mask & (uint8_t)(1U << port_id)) != 0U)
    {
        if (reset_epoch != manager->reset_epoch || !reset_ack ||
            (manager->reset_sent_mask & (uint8_t)(1U << port_id)) == 0U ||
            (reset_sequence & REFEREE_MAIN_ARMOR_ID_MASK) !=
                (manager->reset_sequence[port_id] & REFEREE_MAIN_ARMOR_ID_MASK))
        {
            return ARMOR_COUNTER_RESET_PENDING;
        }

        armor_counter_set_baseline(port,
                                   small_hit_count,
                                   big_hit_count,
                                   reset_epoch);
        manager->reset_pending_mask &= (uint8_t)~(1U << port_id);
        manager->reset_sent_mask &= (uint8_t)~(1U << port_id);
        return ARMOR_COUNTER_RESET_ACK;
    }

    if (!port->baseline_valid)
    {
        armor_counter_set_baseline(port,
                                   small_hit_count,
                                   big_hit_count,
                                   reset_epoch);
        return ARMOR_COUNTER_BASELINE;
    }

    if (reset_epoch != port->reset_epoch)
    {
        armor_counter_set_baseline(port,
                                   small_hit_count,
                                   big_hit_count,
                                   reset_epoch);
        manager->resync_count++;
        return ARMOR_COUNTER_RESYNC;
    }

    small_delta = (uint16_t)(small_hit_count - port->small_hit_count);
    big_delta = (uint16_t)(big_hit_count - port->big_hit_count);
    armor_counter_set_baseline(port,
                               small_hit_count,
                               big_hit_count,
                               reset_epoch);

    if (small_delta > REFEREE_MAIN_MAX_SMALL_HIT_DELTA ||
        big_delta > REFEREE_MAIN_MAX_BIG_HIT_DELTA)
    {
        manager->resync_count++;
        return ARMOR_COUNTER_RESYNC;
    }

    if (small_delta == 0U && big_delta == 0U)
    {
        return ARMOR_COUNTER_NO_CHANGE;
    }

    delta->small_hit_delta = small_delta;
    delta->big_hit_delta = big_delta;
    return ARMOR_COUNTER_HIT;
}

uint32_t armor_counter_get_resync_count(const armor_counter_manager_t *manager)
{
    return manager == 0 ? 0U : manager->resync_count;
}
