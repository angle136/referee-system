#include "armor_link.h"

#include <string.h>

#include "REFEREE/referee_protocol.h"
#include "referee_config.h"

typedef struct
{
    uint8_t buffer[REFEREE_MAIN_ARMOR_PACKET_SIZE];
    uint8_t length;
} armor_link_parser_t;

static armor_link_parser_t          parsers[REFEREE_MAIN_ARMOR_COUNT];
static armor_link_packet_callback_t packet_callback;
static volatile uint32_t            packet_count[REFEREE_MAIN_ARMOR_COUNT];
static volatile uint32_t            checksum_error_count[REFEREE_MAIN_ARMOR_COUNT];
static volatile uint16_t            reported_small_hit_count[REFEREE_MAIN_ARMOR_COUNT];
static volatile uint16_t            reported_big_hit_count[REFEREE_MAIN_ARMOR_COUNT];
static volatile uint8_t             reported_reset_epoch[REFEREE_MAIN_ARMOR_COUNT];
static volatile uint8_t             reported_reset_ack[REFEREE_MAIN_ARMOR_COUNT];

static uint8_t armor_packet_sum(const uint8_t *data)
{
    uint8_t sum = 0;

    for (size_t index = 0; index < REFEREE_MAIN_ARMOR_PACKET_SIZE - 1U; index++)
    {
        sum = (uint8_t)(sum + data[index]);
    }
    return sum;
}

static void armor_link_emit(uint8_t port_id, const uint8_t *data)
{
    armor_link_packet_t packet;

    packet.armor_id = data[1] & REFEREE_MAIN_ARMOR_ID_MASK;
    packet.small_hit_count = (uint16_t)data[2] | ((uint16_t)data[3] << 8U);
    packet.big_hit_count = (uint16_t)data[4] | ((uint16_t)data[5] << 8U);
    packet.reset_epoch = data[6];
    packet.reset_ack = (data[1] & REFEREE_MAIN_ARMOR_RESET_ACK) != 0U;
    packet.reset_sequence = packet.reset_ack
                                ? (data[1] & REFEREE_MAIN_ARMOR_ID_MASK)
                                : 0U;
    reported_small_hit_count[port_id] = packet.small_hit_count;
    reported_big_hit_count[port_id] = packet.big_hit_count;
    reported_reset_epoch[port_id] = packet.reset_epoch;
    reported_reset_ack[port_id] = packet.reset_ack;

    if (packet_callback != 0)
    {
        packet_callback(port_id, &packet);
    }
}

void armor_link_init(armor_link_packet_callback_t callback)
{
    memset(parsers, 0, sizeof(parsers));
    memset((void *)packet_count, 0, sizeof(packet_count));
    memset((void *)checksum_error_count, 0, sizeof(checksum_error_count));
    memset((void *)reported_small_hit_count, 0, sizeof(reported_small_hit_count));
    memset((void *)reported_big_hit_count, 0, sizeof(reported_big_hit_count));
    memset((void *)reported_reset_epoch, 0, sizeof(reported_reset_epoch));
    memset((void *)reported_reset_ack, 0, sizeof(reported_reset_ack));
    packet_callback = callback;
}

void armor_link_process(uint8_t port_id, const uint8_t *data, size_t length)
{
    armor_link_parser_t *parser;

    if (port_id >= REFEREE_MAIN_ARMOR_COUNT || data == 0)
    {
        return;
    }

    parser = &parsers[port_id];
    for (size_t index = 0; index < length; index++)
    {
        uint8_t byte = data[index];

        if (parser->length == 0)
        {
            if (byte == REFEREE_SOF)
            {
                parser->buffer[parser->length++] = byte;
            }
            continue;
        }

        parser->buffer[parser->length++] = byte;
        if (parser->length < sizeof(parser->buffer))
        {
            continue;
        }

        if (armor_packet_sum(parser->buffer) ==
            parser->buffer[REFEREE_MAIN_ARMOR_PACKET_SIZE - 1U])
        {
            packet_count[port_id]++;
            armor_link_emit(port_id, parser->buffer);
            parser->length = 0;
        }
        else
        {
            checksum_error_count[port_id]++;
            parser->length = (byte == REFEREE_SOF) ? 1U : 0U;
            if (parser->length == 1U)
            {
                parser->buffer[0] = byte;
            }
        }
    }
}

void armor_link_get_diagnostics(uint8_t port_id,
                                armor_link_diagnostics_t *diagnostics)
{
    if (port_id >= REFEREE_MAIN_ARMOR_COUNT || diagnostics == 0)
    {
        return;
    }

    diagnostics->packet_count = packet_count[port_id];
    diagnostics->checksum_error_count = checksum_error_count[port_id];
    diagnostics->small_hit_count = reported_small_hit_count[port_id];
    diagnostics->big_hit_count = reported_big_hit_count[port_id];
    diagnostics->reset_epoch = reported_reset_epoch[port_id];
    diagnostics->reset_ack = reported_reset_ack[port_id];
}
