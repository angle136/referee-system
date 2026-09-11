#include "referee_state.h"

#include "referee_config.h"
#include "tx_api.h"

typedef struct
{
    uint16_t current_hp;
    uint16_t maximum_hp;
    ULONG    last_seen_tick[REFEREE_MAIN_ARMOR_COUNT];
    uint8_t  seen_mask;
    uint8_t  last_hit_armor_id;
    uint8_t  last_hit_dx;
    uint16_t last_hit_ax_raw;
    uint32_t hit_count;
} referee_state_t;

static TX_MUTEX        referee_state_mutex;
static referee_state_t referee_state;

int referee_state_init(void)
{
    UINT status = tx_mutex_create(&referee_state_mutex, "referee_state", TX_INHERIT);
    if (status != TX_SUCCESS)
    {
        return -1;
    }

    referee_state.current_hp = REFEREE_MAIN_MAX_HP;
    referee_state.maximum_hp = REFEREE_MAIN_MAX_HP;
    referee_state.seen_mask = 0;
    referee_state.last_hit_armor_id = 0;
    referee_state.last_hit_dx = 0;
    referee_state.last_hit_ax_raw = 0;
    referee_state.hit_count = 0;
    for (uint8_t armor_id = 0; armor_id < REFEREE_MAIN_ARMOR_COUNT; armor_id++)
    {
        referee_state.last_seen_tick[armor_id] = 0;
    }
    return 0;
}

void referee_state_mark_armor_seen(uint8_t armor_id, uint16_t ax_raw, uint8_t dx)
{
    if (armor_id >= REFEREE_MAIN_ARMOR_COUNT)
    {
        return;
    }

    tx_mutex_get(&referee_state_mutex, TX_WAIT_FOREVER);
    referee_state.seen_mask |= (uint8_t)(1U << armor_id);
    referee_state.last_seen_tick[armor_id] = tx_time_get();
    referee_state.last_hit_ax_raw = ax_raw;
    referee_state.last_hit_dx = dx;
    tx_mutex_put(&referee_state_mutex);
}

void referee_state_apply_hit(uint8_t armor_id, uint16_t ax_raw, uint8_t dx)
{
    if (armor_id >= REFEREE_MAIN_ARMOR_COUNT)
    {
        return;
    }

    tx_mutex_get(&referee_state_mutex, TX_WAIT_FOREVER);
    if (referee_state.current_hp > REFEREE_MAIN_HURT_DAMAGE)
    {
        referee_state.current_hp -= REFEREE_MAIN_HURT_DAMAGE;
    }
    else
    {
        referee_state.current_hp = 0;
    }
    referee_state.seen_mask |= (uint8_t)(1U << armor_id);
    referee_state.last_seen_tick[armor_id] = tx_time_get();
    referee_state.last_hit_armor_id = armor_id;
    referee_state.last_hit_ax_raw = ax_raw;
    referee_state.last_hit_dx = dx;
    referee_state.hit_count++;
    tx_mutex_put(&referee_state_mutex);
}

void referee_state_get_snapshot(referee_state_snapshot_t *snapshot)
{
    ULONG now;

    if (snapshot == 0)
    {
        return;
    }

    now = tx_time_get();
    tx_mutex_get(&referee_state_mutex, TX_WAIT_FOREVER);
    snapshot->current_hp = referee_state.current_hp;
    snapshot->maximum_hp = referee_state.maximum_hp;
    snapshot->online_mask = 0;
    for (uint8_t armor_id = 0; armor_id < REFEREE_MAIN_ARMOR_COUNT; armor_id++)
    {
        if ((referee_state.seen_mask & (uint8_t)(1U << armor_id)) != 0 &&
            (now - referee_state.last_seen_tick[armor_id]) <
            REFEREE_MAIN_ARMOR_OFFLINE_TIMEOUT_MS)
        {
            snapshot->online_mask |= (uint8_t)(1U << armor_id);
        }
    }
    snapshot->last_hit_armor_id = referee_state.last_hit_armor_id;
    snapshot->last_hit_ax_raw = referee_state.last_hit_ax_raw;
    snapshot->last_hit_dx = referee_state.last_hit_dx;
    snapshot->hit_count = referee_state.hit_count;
    tx_mutex_put(&referee_state_mutex);
}
