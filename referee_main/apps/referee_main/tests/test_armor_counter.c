#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "armor_counter.h"

static armor_counter_result_t update(armor_counter_manager_t *manager,
                                     uint16_t small,
                                     uint16_t big,
                                     uint8_t epoch,
                                     armor_counter_delta_t *delta)
{
    return armor_counter_update(manager, 0U, small, big, epoch, true, 1U, delta);
}

int main(void)
{
    armor_counter_manager_t manager;
    armor_counter_delta_t delta;
    uint8_t reset_epoch;

    armor_counter_init(&manager);
    assert(armor_counter_update(NULL, 0U, 0U, 0U, 0U, true, 1U, &delta) ==
           ARMOR_COUNTER_INVALID);
    assert(armor_counter_update(&manager,
                                REFEREE_MAIN_ARMOR_COUNT,
                                0U,
                                0U,
                                0U,
                                true,
                                1U,
                                &delta) == ARMOR_COUNTER_INVALID);
    assert(armor_counter_update(&manager, 0U, 0U, 0U, 0U, true, 1U, NULL) ==
           ARMOR_COUNTER_INVALID);
    assert(update(&manager, 10U, 2U, 0U, &delta) == ARMOR_COUNTER_BASELINE);
    assert(update(&manager, 10U, 2U, 0U, &delta) == ARMOR_COUNTER_NO_CHANGE);
    assert(update(&manager, 11U, 3U, 0U, &delta) == ARMOR_COUNTER_HIT);
    assert(delta.small_hit_delta == 1U && delta.big_hit_delta == 1U);
    assert(update(&manager, 14U, 3U, 0U, &delta) == ARMOR_COUNTER_HIT);
    assert(delta.small_hit_delta == 3U && delta.big_hit_delta == 0U);

    armor_counter_init(&manager);
    assert(update(&manager, 0xFFFEU, 0xFFFFU, 0U, &delta) ==
           ARMOR_COUNTER_BASELINE);
    assert(update(&manager, 1U, 0U, 0U, &delta) == ARMOR_COUNTER_HIT);
    assert(delta.small_hit_delta == 3U && delta.big_hit_delta == 1U);

    armor_counter_init(&manager);
    assert(update(&manager, 5U, 1U, 0U, &delta) == ARMOR_COUNTER_BASELINE);
    assert(update(&manager, 100U, 1U, 0U, &delta) == ARMOR_COUNTER_RESYNC);
    assert(armor_counter_get_resync_count(&manager) == 1U);
    assert(update(&manager, 101U, 1U, 0U, &delta) == ARMOR_COUNTER_HIT);
    assert(delta.small_hit_delta == 1U && delta.big_hit_delta == 0U);

    armor_counter_request_reset(&manager);
    assert(armor_counter_get_reset_request(&manager, 0U, &reset_epoch));
    assert(reset_epoch == 1U);
    assert(armor_counter_update(&manager, 0U, 0U, 0U, 1U, false, 1U, &delta) ==
           ARMOR_COUNTER_RESET_PENDING);
    assert(update(&manager, 102U, 1U, 0U, &delta) ==
           ARMOR_COUNTER_RESET_PENDING);
    armor_counter_mark_reset_sent(&manager, 0U, 1U);
    assert(armor_counter_update(&manager, 0U, 0U, 0U, 1U, true, 2U, &delta) ==
           ARMOR_COUNTER_RESET_PENDING);
    assert(update(&manager, 0U, 0U, 1U, &delta) == ARMOR_COUNTER_RESET_ACK);
    assert(!armor_counter_get_reset_request(&manager, 0U, &reset_epoch));
    assert(update(&manager, 1U, 0U, 1U, &delta) == ARMOR_COUNTER_HIT);

    assert(update(&manager, 9U, 2U, 2U, &delta) == ARMOR_COUNTER_RESYNC);
    assert(armor_counter_get_resync_count(&manager) == 2U);

    armor_counter_init(&manager);
    assert(armor_counter_update(&manager, 1U, 7U, 0U, 0U, true, 1U, &delta) ==
           ARMOR_COUNTER_BASELINE);
    armor_counter_request_reset(&manager);
    armor_counter_mark_reset_sent(&manager, 0U, 1U);
    assert(armor_counter_update(&manager, 0U, 0U, 0U, 1U, true, 1U, &delta) ==
           ARMOR_COUNTER_RESET_ACK);
    assert(armor_counter_get_reset_request(&manager, 1U, &reset_epoch));
    armor_counter_mark_reset_sent(&manager, 1U, 1U);
    assert(armor_counter_update(&manager, 1U, 0U, 0U, 1U, true, 1U, &delta) ==
           ARMOR_COUNTER_RESET_ACK);
    assert(!armor_counter_get_reset_request(&manager, 1U, &reset_epoch));

    puts("armor_counter host test: PASS");
    return 0;
}
