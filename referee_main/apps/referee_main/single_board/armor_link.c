#include "armor_link.h"

#include <string.h>

#include "REFEREE/referee_protocol.h"
#include "referee_config.h"

typedef struct
{
    uint8_t buffer[REFEREE_MAIN_ARMOR_DEBUG_PACKET_SIZE];
    uint8_t length;
    uint8_t expected_length;
} armor_link_parser_t;

static armor_link_parser_t          parsers[REFEREE_MAIN_ARMOR_COUNT];
static armor_link_packet_callback_t packet_callback;
static volatile uint8_t              debug_mode;
static volatile uint32_t            packet_count[REFEREE_MAIN_ARMOR_COUNT];
static volatile uint32_t            checksum_error_count[REFEREE_MAIN_ARMOR_COUNT];
static volatile uint16_t            reported_small_hit_count[REFEREE_MAIN_ARMOR_COUNT];
static volatile uint16_t            reported_big_hit_count[REFEREE_MAIN_ARMOR_COUNT];
static volatile uint8_t             reported_reset_epoch[REFEREE_MAIN_ARMOR_COUNT];
static volatile uint8_t             reported_reset_ack[REFEREE_MAIN_ARMOR_COUNT];
static volatile uint8_t             reported_adc_debug[REFEREE_MAIN_ARMOR_COUNT];
static volatile uint16_t            reported_adc_samples[REFEREE_MAIN_ARMOR_COUNT]
                                                       [REFEREE_MAIN_ARMOR_ADC_HISTORY_COUNT];

static uint8_t armor_packet_sum(const uint8_t *data, size_t length)
{
    uint8_t sum = 0;

    for (size_t index = 0; index + 1U < length; index++)
    {
        sum = (uint8_t)(sum + data[index]);
    }
    return sum;
}

static void armor_link_emit(uint8_t port_id, const uint8_t *data)
{
    armor_link_packet_t packet;

    memset(&packet, 0, sizeof(packet));
    packet.frame_type = 0U;
    if (debug_mode != 0U && data[1] == REFEREE_MAIN_ARMOR_DEBUG_CMD)
    {
        packet.frame_type = REFEREE_MAIN_ARMOR_DEBUG_CMD;
        reported_adc_debug[port_id] = 1U;
        for (uint8_t index = 0U; index < REFEREE_MAIN_ARMOR_ADC_HISTORY_COUNT; index++)
        {
            packet.adc_samples[index] = (uint16_t)data[2U + index * 2U] |
                                        ((uint16_t)data[3U + index * 2U] << 8U);
            reported_adc_samples[port_id][index] = packet.adc_samples[index];
        }
        packet.armor_id = port_id;
        if (packet_callback != 0)
        {
            packet_callback(port_id, &packet);
        }
        return;
    }

    reported_adc_debug[port_id] = 0U;
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
    memset((void *)reported_adc_debug, 0, sizeof(reported_adc_debug));
    memset((void *)reported_adc_samples, 0, sizeof(reported_adc_samples));
    packet_callback = callback;
    debug_mode = 0U;
}

void armor_link_set_debug_mode(uint8_t enabled)
{
    debug_mode = enabled != 0U ? 1U : 0U;
    for (uint8_t port_id = 0U; port_id < REFEREE_MAIN_ARMOR_COUNT; port_id++)
    {
        parsers[port_id].length = 0U;
        parsers[port_id].expected_length = 0U;
    }
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
                parser->expected_length = 0U;
            }
            continue;
        }

        parser->buffer[parser->length++] = byte;
        if (parser->length == 2U)
        {
            parser->expected_length = (debug_mode != 0U &&
                                       byte == REFEREE_MAIN_ARMOR_DEBUG_CMD)
                                           ? REFEREE_MAIN_ARMOR_DEBUG_PACKET_SIZE
                                           : REFEREE_MAIN_ARMOR_PACKET_SIZE;
        }
        if (parser->expected_length == 0U ||
            parser->length < parser->expected_length)
        {
            continue;
        }

        if (armor_packet_sum(parser->buffer, parser->expected_length) ==
            parser->buffer[parser->expected_length - 1U])
        {
            packet_count[port_id]++;
            armor_link_emit(port_id, parser->buffer);
            parser->length = 0;
            parser->expected_length = 0U;
        }
        else
        {
            checksum_error_count[port_id]++;
            parser->length = (byte == REFEREE_SOF) ? 1U : 0U;
            parser->expected_length = 0U;
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
    diagnostics->adc_debug = reported_adc_debug[port_id];
    memcpy(diagnostics->adc_samples,
           (const void *)reported_adc_samples[port_id],
           sizeof(diagnostics->adc_samples));
}
