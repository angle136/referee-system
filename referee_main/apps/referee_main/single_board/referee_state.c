#include "referee_state.h"

#include "referee_config.h"
#include "tx_api.h"

typedef struct
{
    uint16_t current_hp;
    uint16_t maximum_hp;
    uint8_t  team;
    ULONG    last_seen_tick[REFEREE_MAIN_ARMOR_COUNT];
    uint8_t  seen_mask;
    uint8_t  last_hit_armor_id;
    uint32_t hit_count;
} referee_state_t;

static TX_MUTEX        referee_state_mutex;
static referee_state_t referee_state;

static void referee_state_reset_values(void)
{
    referee_state.current_hp = referee_state.maximum_hp;
    referee_state.team = REFEREE_MAIN_INITIAL_TEAM;
    referee_state.seen_mask = 0U;
    referee_state.last_hit_armor_id = 0U;
    referee_state.hit_count = 0U;
    for (uint8_t armor_id = 0U; armor_id < REFEREE_MAIN_ARMOR_COUNT; armor_id++)
    {
        referee_state.last_seen_tick[armor_id] = 0U;
    }
}

int referee_state_init(void)
{
    UINT status = tx_mutex_create(&referee_state_mutex, "referee_state", TX_INHERIT);
    if (status != TX_SUCCESS)
    {
        return -1;
    }

    referee_state.maximum_hp = REFEREE_MAIN_MAX_HP;
    referee_state_reset_values();
    return 0;
}

void referee_state_toggle_team(void)
{
    tx_mutex_get(&referee_state_mutex, TX_WAIT_FOREVER);
    if (referee_state.team == REFEREE_MAIN_TEAM_RED)
    {
        referee_state.team = REFEREE_MAIN_TEAM_BLUE;
    }
    else
    {
        referee_state.team = REFEREE_MAIN_TEAM_RED;
    }
    tx_mutex_put(&referee_state_mutex);
}

void referee_state_reset(void)
{
    tx_mutex_get(&referee_state_mutex, TX_WAIT_FOREVER);
    referee_state_reset_values();
    tx_mutex_put(&referee_state_mutex);
}

void referee_state_mark_armor_seen(uint8_t armor_id)
{
    if (armor_id >= REFEREE_MAIN_ARMOR_COUNT)
    {
        return;
    }

    tx_mutex_get(&referee_state_mutex, TX_WAIT_FOREVER);
    referee_state.seen_mask |= (uint8_t)(1U << armor_id);
    referee_state.last_seen_tick[armor_id] = tx_time_get();
    tx_mutex_put(&referee_state_mutex);
}

void referee_state_apply_hits(uint8_t armor_id,
                              uint16_t small_hit_delta,
                              uint16_t big_hit_delta)
{
    uint32_t damage;
    uint32_t total_hits;

    if (armor_id >= REFEREE_MAIN_ARMOR_COUNT)
    {
        return;
    }

    damage = (uint32_t)small_hit_delta * REFEREE_MAIN_SMALL_HIT_DAMAGE +
             (uint32_t)big_hit_delta * REFEREE_MAIN_BIG_HIT_DAMAGE;
    total_hits = (uint32_t)small_hit_delta + big_hit_delta;
    tx_mutex_get(&referee_state_mutex, TX_WAIT_FOREVER);
    if (referee_state.current_hp > damage)
    {
        referee_state.current_hp = (uint16_t)(referee_state.current_hp - damage);
    }
    else
    {
        referee_state.current_hp = 0;
    }
    referee_state.seen_mask |= (uint8_t)(1U << armor_id);
    referee_state.last_seen_tick[armor_id] = tx_time_get();
    referee_state.last_hit_armor_id = armor_id;
    referee_state.hit_count += total_hits;
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
    snapshot->team = referee_state.team;
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
    snapshot->hit_count = referee_state.hit_count;
    tx_mutex_put(&referee_state_mutex);
}
