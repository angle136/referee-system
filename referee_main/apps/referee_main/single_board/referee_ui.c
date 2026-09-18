#include "referee_ui.h"

#include <string.h>
#include <stdint.h>

#include "armor_link.h"
#include "bsp_def.h"
#include "gpio.h"
#include "kk_oled.h"
#include "kk_ui.h"
#include "referee_config.h"
#include "referee_state.h"
#include "robot_control.h"
#include "tx_api.h"

#define LOG_TAG "referee_ui"
#define LOG_LVL LOG_LVL_INFO
#include "ulog_def.h"

typedef struct
{
    uint8_t last_raw;
    uint8_t stable;
    ULONG changed_at;
} ui_button_t;

static TX_THREAD ui_thread;
APPS_STACK_SECTION static uint8_t ui_thread_stack[REFEREE_MAIN_UI_STACK_SIZE];

static ui_button_t ui_key1;
static ui_button_t ui_key2;
static ui_button_t ui_key3;
static ui_button_t ui_key4;
static uint8_t ui_active;
static uint8_t ui_display_ready;
/* 0 = icon home, 1..4 = detail pages, 5 = settings menu. */
static uint8_t ui_page;
static uint8_t ui_home_selected;
static uint8_t ui_armor_offset;
static uint8_t ui_settings_selected;
static uint8_t ui_confirm_action;
static uint8_t ui_confirm_focus;
static uint32_t ui_toast_until;
static const char *ui_toast_text;

/* KK_UI validates that font pointers are present even for a custom-only app.
 * The custom page below uses the local 5x7 renderer, so this non-null marker is
 * intentionally never passed to OLED_SetFont(). It can be replaced by a
 * generated KK_OLED font once LEDFont is reachable. */
static const uint8_t ui_font_marker[23] = {0};

static const KK_UI_PageRoute ui_routes[] = {
    {KK_UI_PAGE_CUSTOM, 0U}
};

static const KK_UI_App ui_app = {
    .root_page = 1U,
    .routes = ui_routes,
    .route_count = 1U,
    .home_pages = 0,
    .home_page_count = 0U,
    .menu_pages = 0,
    .menu_page_count = 0U,
    .info_pages = 0,
    .info_page_count = 0U,
    .custom_page_count = 1U,
    .int_bindings = 0,
    .int_binding_count = 0U,
    .bool_bindings = 0,
    .bool_binding_count = 0U,
    .confirm_descs = 0,
    .confirm_desc_count = 0U,
    .fonts = {ui_font_marker, ui_font_marker, ui_font_marker},
    .texts = {"BACK", "CANCEL", "OK", "ON", "OFF", "MSG"}
};

static uint8_t ui_read_pressed(GPIO_TypeDef *port, uint16_t pin)
{
    return HAL_GPIO_ReadPin(port, pin) == GPIO_PIN_RESET;
}

static void ui_button_init(ui_button_t *button, uint8_t pressed, ULONG now)
{
    button->last_raw = pressed;
    button->stable = pressed;
    button->changed_at = now;
}

static uint8_t ui_button_press(ui_button_t *button, uint8_t pressed, ULONG now)
{
    if (pressed != button->last_raw)
    {
        button->last_raw = pressed;
        button->changed_at = now;
    }
    if ((now - button->changed_at) < REFEREE_MAIN_KEY_DEBOUNCE_MS)
    {
        return 0U;
    }
    if (pressed != button->stable)
    {
        button->stable = pressed;
        return pressed;
    }
    return 0U;
}

static void ui_put_glyph(char character, uint16_t columns[7])
{
    static const uint8_t letters[26][5] = {
        {0x7E,0x11,0x11,0x11,0x7E},{0x7F,0x49,0x49,0x49,0x36},
        {0x3E,0x41,0x41,0x41,0x22},{0x7F,0x41,0x41,0x22,0x1C},
        {0x7F,0x49,0x49,0x49,0x41},{0x7F,0x09,0x09,0x09,0x01},
        {0x3E,0x41,0x49,0x49,0x7A},{0x7F,0x08,0x08,0x08,0x7F},
        {0x00,0x41,0x7F,0x41,0x00},{0x20,0x40,0x41,0x3F,0x01},
        {0x7F,0x08,0x14,0x22,0x41},{0x7F,0x40,0x40,0x40,0x40},
        {0x7F,0x02,0x0C,0x02,0x7F},{0x7F,0x04,0x08,0x10,0x7F},
        {0x3E,0x41,0x41,0x41,0x3E},{0x7F,0x09,0x09,0x09,0x06},
        {0x3E,0x41,0x51,0x21,0x5E},{0x7F,0x09,0x19,0x29,0x46},
        {0x46,0x49,0x49,0x49,0x31},{0x01,0x01,0x7F,0x01,0x01},
        {0x3F,0x40,0x40,0x40,0x3F},{0x1F,0x20,0x40,0x20,0x1F},
        {0x7F,0x20,0x18,0x20,0x7F},{0x63,0x14,0x08,0x14,0x63},
        {0x07,0x08,0x70,0x08,0x07},{0x61,0x51,0x49,0x45,0x43}
    };
    static const uint8_t digits[10][5] = {
        {0x3E,0x45,0x49,0x51,0x3E},{0x00,0x21,0x7F,0x01,0x00},
        {0x23,0x45,0x49,0x51,0x21},{0x42,0x41,0x51,0x69,0x46},
        {0x0C,0x14,0x24,0x7F,0x04},{0x72,0x51,0x51,0x51,0x4E},
        {0x1E,0x29,0x49,0x49,0x06},{0x40,0x47,0x48,0x50,0x60},
        {0x36,0x49,0x49,0x49,0x36},{0x30,0x49,0x49,0x4A,0x3C}
    };
    uint8_t i;
    uint8_t source[5] = {0U};

    for (i = 0U; i < 7U; ++i) columns[i] = 0U;
    if (character >= 'a' && character <= 'z') character = (char)(character - 'a' + 'A');
    if (character >= 'A' && character <= 'Z')
    {
        for (i = 0U; i < 5U; ++i) source[i] = letters[character - 'A'][i];
    }
    else if (character >= '0' && character <= '9')
    {
        for (i = 0U; i < 5U; ++i) source[i] = digits[character - '0'][i];
    }
    else if (character == ':') { source[1] = 0x24; source[3] = 0x24; }
    else if (character == '-') { source[1] = source[2] = source[3] = 0x08; }
    else if (character == '/') { source[0] = 0x20; source[1] = 0x10; source[2] = 0x08; source[3] = 0x04; source[4] = 0x02; }
    else if (character == '.') { source[2] = 0x40; }
    else if (character == '%') { source[0] = 0x62; source[2] = 0x08; source[4] = 0x46; }
    else if (character == '?') { source[0] = 0x02; source[1] = 0x01; source[2] = 0x51; source[3] = 0x09; source[4] = 0x06; }
    else if (character == '>') { source[1] = 0x08; source[2] = 0x14; source[3] = 0x22; }
    else if (character == '<') { source[1] = 0x22; source[2] = 0x14; source[3] = 0x08; }
    else if (character == '=') { source[1] = source[2] = source[3] = 0x14; }

    /* Keep the original 5x7 columns intact. Scaling is done only by
     * ui_text_scaled(), using exact integer pixel blocks; interpolation here
     * was causing missing bottom strokes on the real SSD1306. */
    for (i = 0U; i < 5U; ++i)
    {
        columns[i] = source[i];
        if (character >= '0' && character <= '9')
        {
            uint16_t flipped = 0U;
            uint8_t row;
            for (row = 0U; row < 7U; ++row)
            {
                if ((source[i] & (uint8_t)(1U << row)) != 0U)
                {
                    flipped |= (uint16_t)(1U << (6U - row));
                }
            }
            columns[i] = flipped;
        }
    }
    columns[5] = 0U;
    columns[6] = 0U;
}

static void ui_text_scaled(int16_t x, int16_t y, const char *text,
                           uint8_t scale)
{
    while (text != 0 && *text != '\0')
    {
        uint16_t glyph[7];
        uint8_t column;
        uint8_t row;

        ui_put_glyph(*text++, glyph);
        for (column = 0U; column < 7U; ++column)
        {
            for (row = 0U; row < 8U; ++row)
            {
                if ((glyph[column] & (uint16_t)(1U << row)) != 0U)
                {
                    uint8_t dx;
                    uint8_t dy;
                    for (dx = 0U; dx < scale; ++dx)
                    {
                        for (dy = 0U; dy < scale; ++dy)
                        {
                            OLED_DrawPixel((int16_t)(x + column * scale + dx),
                                           (int16_t)(y + row * scale + dy));
                        }
                    }
                }
            }
        }
        x = (int16_t)(x + 8 * scale);
    }
}

static void ui_text(int16_t x, int16_t y, const char *text)
{
    ui_text_scaled(x, y, text, 1U);
}

static void ui_text_bold(int16_t x, int16_t y, const char *text)
{
    ui_text(x, y, text);
    ui_text((int16_t)(x + 1), y, text);
}

static void ui_text_big(int16_t x, int16_t y, const char *text)
{
    ui_text_scaled(x, y, text, 2U);
}

static void ui_text_large(int16_t x, int16_t y, const char *text)
{
    ui_text_scaled(x, y, text, 2U);
}

/* Compact 2x renderer for the MAIN page: five source columns become ten
 * pixels plus a two-pixel gap.  This preserves the large 14-pixel glyphs
 * without paying the normal 16-pixel advance for every character. */
static void ui_text_main_large(int16_t x, int16_t y, const char *text)
{
    while (text != 0 && *text != '\0')
    {
        uint16_t glyph[7];
        uint8_t column;
        uint8_t row;

        ui_put_glyph(*text++, glyph);
        for (column = 0U; column < 5U; ++column)
        {
            for (row = 0U; row < 7U; ++row)
            {
                if ((glyph[column] & (uint16_t)(1U << row)) != 0U)
                {
                    OLED_DrawBox((int16_t)(x + column * 2U),
                                 (int16_t)(y + row * 2U), 2U, 2U);
                }
            }
        }
        x = (int16_t)(x + 12);
    }
}

static void ui_u32_main_large(int16_t x, int16_t y, uint32_t value)
{
    char digits[11];
    uint8_t count = 0U;
    uint8_t i;

    do
    {
        digits[count++] = (char)('0' + (value % 10U));
        value /= 10U;
    } while (value != 0U && count < sizeof(digits) - 1U);
    for (i = 0U; i < count / 2U; ++i)
    {
        char swap = digits[i];
        digits[i] = digits[count - 1U - i];
        digits[count - 1U - i] = swap;
    }
    digits[count] = '\0';
    ui_text_main_large(x, y, digits);
}

static int16_t ui_u32_big(int16_t x, int16_t y, uint32_t value)
{
    char digits[11];
    uint8_t count = 0U;
    uint8_t i;

    do
    {
        digits[count++] = (char)('0' + (value % 10U));
        value /= 10U;
    } while (value != 0U && count < sizeof(digits) - 1U);
    for (i = 0U; i < count / 2U; ++i)
    {
        char swap = digits[i];
        digits[i] = digits[count - 1U - i];
        digits[count - 1U - i] = swap;
    }
    digits[count] = '\0';
    /* A one-pixel bold pass keeps numbers readable while fitting the rows. */
    ui_text(x, y, digits);
    ui_text((int16_t)(x + 1), y, digits);
    return (int16_t)(x + count * 8U);
}

static int16_t ui_u32_xl(int16_t x, int16_t y, uint32_t value)
{
    char digits[11];
    uint8_t count = 0U;
    uint8_t i;

    do
    {
        digits[count++] = (char)('0' + (value % 10U));
        value /= 10U;
    } while (value != 0U && count < sizeof(digits) - 1U);
    for (i = 0U; i < count / 2U; ++i)
    {
        char swap = digits[i];
        digits[i] = digits[count - 1U - i];
        digits[count - 1U - i] = swap;
    }
    digits[count] = '\0';
    ui_text_scaled(x, y, digits, 2U);
    return (int16_t)(x + count * 16U);
}

static int16_t ui_u32_fit(int16_t x, int16_t y, uint32_t value,
                          int16_t right_limit)
{
    uint32_t probe = value;
    uint8_t digits = 1U;
    while (probe >= 10U)
    {
        probe /= 10U;
        ++digits;
    }
    if ((int32_t)x + (int32_t)digits * 16 <= right_limit)
    {
        return ui_u32_xl(x, y, value);
    }
    return ui_u32_big(x, y, value);
}

static void ui_draw_main(int16_t x)
{
    referee_state_snapshot_t state;
    referee_control_diagnostics_t control;

    referee_state_get_snapshot(&state);
    referee_control_get_diagnostics(&control);
    ui_text_main_large((int16_t)(x + 2), 0,
                       state.team == REFEREE_MAIN_TEAM_BLUE ? "BLUE" : "RED");
    ui_text_main_large((int16_t)(x + 53), 0, "ID");
    ui_u32_main_large((int16_t)(x + 80), 0,
                      state.team == REFEREE_MAIN_TEAM_BLUE ?
                      REFEREE_MAIN_ROBOT_ID_BLUE : REFEREE_MAIN_ROBOT_ID_RED);

    ui_text_main_large((int16_t)(x + 2), 21, "HP");
    ui_u32_main_large((int16_t)(x + 30), 21, state.current_hp);
    OLED_DrawLine((int16_t)(x + 70), 21, (int16_t)(x + 75), 34);
    ui_u32_main_large((int16_t)(x + 80), 21, state.maximum_hp);

    ui_text_main_large((int16_t)(x + 2), 44, "HIT");
    if (state.hit_count < 100U)
        ui_u32_main_large((int16_t)(x + 42), 44, state.hit_count);
    else
        ui_u32_big((int16_t)(x + 42), 47, state.hit_count);
    ui_text_main_large((int16_t)(x + 66), 44, "TX");
    if (control.referee_tx_count < 1000U)
        ui_u32_main_large((int16_t)(x + 90), 44, control.referee_tx_count);
    else
        ui_u32_big((int16_t)(x + 90), 47, control.referee_tx_count);
}

static void ui_draw_armor(int16_t x)
{
    uint8_t port;
    for (port = ui_armor_offset;
         port < (uint8_t)(ui_armor_offset + 2U) &&
         port < REFEREE_MAIN_ARMOR_COUNT; ++port)
    {
        armor_link_diagnostics_t diagnostics;
        referee_state_snapshot_t state;

        armor_link_get_diagnostics(port, &diagnostics);
        referee_state_get_snapshot(&state);
        int16_t by = (int16_t)((port - ui_armor_offset) * 32U);
        ui_text_large((int16_t)(x + 2), by, "P");
        ui_u32_big((int16_t)(x + 20), (int16_t)(by + 4), port);
        ui_text_bold((int16_t)(x + 32), (int16_t)(by + 4),
                (state.online_mask & (1U << port)) != 0U ? "ON" : "--");
        ui_text_large((int16_t)(x + 54), by, "R");
        ui_u32_fit((int16_t)(x + 72), (int16_t)(by + 4), diagnostics.packet_count, 94);
        ui_text_large((int16_t)(x + 96), by, "H");
        ui_u32_fit((int16_t)(x + 114), (int16_t)(by + 4), diagnostics.hit_count, 127);
        if (port == ui_armor_offset)
        {
            OLED_DrawHLine(x, 30, 128U);
        }
    }
}

static void ui_draw_protocol(int16_t x)
{
    referee_control_diagnostics_t control;
    referee_control_get_diagnostics(&control);
    ui_text_bold((int16_t)(x + 2), 4, "TX");
    ui_u32_xl((int16_t)(x + 28), 0, control.referee_tx_count);
    ui_text_bold((int16_t)(x + 2), 20, "ERR");
    ui_u32_xl((int16_t)(x + 34), 16, control.referee_tx_error_count);
    ui_text_bold((int16_t)(x + 2), 36, "CFG");
    ui_u32_xl((int16_t)(x + 28), 32, control.armor_config_tx_count);
    ui_text_bold((int16_t)(x + 2), 52, "OUT");
    ui_text_bold((int16_t)(x + 38), 52,
                 control.referee_tx_error_count == 0U ? "OK" : "ERR");
}

static void ui_draw_debug(int16_t x)
{
    uint8_t port;
    uint32_t crc = 0U;
    uint32_t duplicate = 0U;
    referee_control_diagnostics_t control;
    referee_state_snapshot_t state;

    for (port = 0U; port < REFEREE_MAIN_ARMOR_COUNT; ++port)
    {
        armor_link_diagnostics_t diagnostics;
        armor_link_get_diagnostics(port, &diagnostics);
        crc += diagnostics.checksum_error_count;
        duplicate += diagnostics.duplicate_count;
    }
    referee_control_get_diagnostics(&control);
    referee_state_get_snapshot(&state);
    ui_text_bold((int16_t)(x + 2), 4, "CRC");
    ui_u32_fit((int16_t)(x + 34), 0, crc, 73);
    ui_text_bold((int16_t)(x + 74), 4, "DUP");
    ui_u32_big((int16_t)(x + 102), 2, duplicate);
    ui_text_bold((int16_t)(x + 2), 22, "DROP");
    ui_u32_fit((int16_t)(x + 42), 18, control.armor_event_drop_count, 127);
    ui_text_bold((int16_t)(x + 2), 40, "LAST P");
    ui_u32_fit((int16_t)(x + 58), 36, state.last_hit_armor_id, 127);
}

static void ui_draw_settings(int16_t x)
{
    static const char *const items[] = {"TEAM", "RESET"};
    uint8_t i;

    for (i = 0U; i < 2U; ++i)
    {
        int16_t y = (int16_t)(1 + i * 32U);
        if (i == ui_settings_selected)
        {
            /* Fill the selected row, then draw its text in clear mode.  The
             * 31-pixel rows leave a full blank line between the two large
             * glyphs, so neither the frame nor the descenders touch the next
             * item. */
            OLED_DrawRBox((int16_t)(x + 2), y, 124U, 29U, 3U);
            OLED_SetDrawMode(OLED_DRAW_CLEAR);
            /* Keep the selection border away from the rounded outer edge.
             * A plain inset frame is intentionally used here: it remains a
             * crisp black line on panels whose rounded-corner pixels bloom. */
            OLED_DrawFrame((int16_t)(x + 4), (int16_t)(y + 2), 120U, 25U);
        }
        else
        {
            OLED_SetDrawMode(OLED_DRAW_SET);
        }
        ui_text_main_large((int16_t)(x + 10), (int16_t)(y + 7), items[i]);
        ui_text_main_large((int16_t)(x + 106), (int16_t)(y + 7), ">");
        OLED_SetDrawMode(OLED_DRAW_SET);
    }
}

static void ui_draw_confirm(int16_t x)
{
    const char *title_line1 = ui_confirm_action == 1U ? "SWITCH" : "RESET";
    const char *title_line2 = ui_confirm_action == 1U ? "TEAM?" : "SYSTEM?";

    OLED_SetDrawMode(OLED_DRAW_SET);
    OLED_DrawRBox((int16_t)(x + 2), 2, 124U, 60U, 4U);
    OLED_SetDrawMode(OLED_DRAW_CLEAR);
    ui_text_main_large((int16_t)(x + 64 - (int16_t)(strlen(title_line1) * 6U)),
                       6, title_line1);
    ui_text_main_large((int16_t)(x + 64 - (int16_t)(strlen(title_line2) * 6U)),
                       21, title_line2);
    ui_text_main_large((int16_t)(x + 12), 44, "CANCEL");
    ui_text_main_large((int16_t)(x + 95), 44, "OK");
    OLED_SetDrawMode(OLED_DRAW_SET);
    if (ui_confirm_focus == 0U)
    {
        OLED_DrawRFrame((int16_t)(x + 7), 40, 82U, 22U, 3U);
    }
    else
    {
        OLED_DrawRFrame((int16_t)(x + 90), 40, 32U, 22U, 3U);
    }
}

static void ui_draw_toast(int16_t x)
{
    if (ui_toast_until == 0U || ui_toast_text == 0)
    {
        return;
    }
    OLED_SetDrawMode(OLED_DRAW_SET);
    OLED_DrawRBox((int16_t)(x + 8), 17, 112U, 30U, 4U);
    OLED_SetDrawMode(OLED_DRAW_CLEAR);
    ui_text_main_large((int16_t)(x + 64 - (int16_t)(strlen(ui_toast_text) * 6U)),
                       25, ui_toast_text);
    OLED_SetDrawMode(OLED_DRAW_SET);
}

/* Application-owned icon strip.  The shapes deliberately stay simple and
 * monochrome so they remain legible on a 128x64 SSD1306 panel. */
static void ui_draw_icon(uint8_t kind, int16_t x, int16_t y, uint8_t selected)
{
    if (selected != 0U)
    {
        OLED_DrawRFrame(x - 2, y - 2, 32U, 30U, 3U);
    }
    switch (kind)
    {
    case 0U: /* robot / main state */
        OLED_DrawFrame(x + 7, y + 7, 14U, 13U);
        OLED_DrawBox(x + 10, y + 2, 8U, 5U);
        OLED_DrawVLine(x + 14, y, 2U);
        OLED_DrawPixel(x + 11, y + 12);
        OLED_DrawPixel(x + 17, y + 12);
        OLED_DrawHLine(x + 10, y + 17, 8U);
        OLED_DrawVLine(x + 9, y + 20, 5U);
        OLED_DrawVLine(x + 19, y + 20, 5U);
        break;
    case 1U: /* armor / shield */
        OLED_DrawLine(x + 14, y + 2, x + 24, y + 7);
        OLED_DrawLine(x + 24, y + 7, x + 21, y + 20);
        OLED_DrawLine(x + 21, y + 20, x + 14, y + 25);
        OLED_DrawLine(x + 14, y + 25, x + 7, y + 20);
        OLED_DrawLine(x + 7, y + 20, x + 4, y + 7);
        OLED_DrawLine(x + 4, y + 7, x + 14, y + 2);
        OLED_DrawHLine(x + 9, y + 13, 10U);
        break;
    case 2U: /* serial / protocol */
        OLED_DrawCircle(x + 9, y + 13, 5U);
        OLED_DrawCircle(x + 19, y + 13, 5U);
        OLED_DrawHLine(x + 12, y + 13, 4U);
        OLED_DrawLine(x + 3, y + 5, x + 9, y + 1);
        OLED_DrawLine(x + 25, y + 21, x + 19, y + 25);
        break;
    case 4U: /* settings */
        OLED_DrawCircle(x + 14, y + 13, 7U);
        OLED_DrawDisc(x + 14, y + 13, 2U);
        OLED_DrawLine(x + 14, y + 1, x + 14, y + 5);
        OLED_DrawLine(x + 14, y + 21, x + 14, y + 25);
        OLED_DrawLine(x + 2, y + 13, x + 6, y + 13);
        OLED_DrawLine(x + 22, y + 13, x + 26, y + 13);
        break;
    default: /* diagnostics */
        OLED_DrawFrame(x + 4, y + 4, 21U, 21U);
        OLED_DrawVLine(x + 9, y + 18, 3U);
        OLED_DrawVLine(x + 14, y + 13, 8U);
        OLED_DrawVLine(x + 19, y + 8, 13U);
        break;
    }
}

static void ui_draw_home(int16_t x)
{
    static const char *const labels[] = {"MAIN", "ARMOR", "PROTO", "DEBUG", "SET"};
    uint8_t i;
    for (i = 0U; i < 5U; ++i)
    {
        int16_t delta;

        /* Show only the selected icon and its immediate neighbours.  A
         * simple clamp is not sufficient for five entries: when selection is
         * at either end it would draw two different icons at the same x. */
        if (i == ui_home_selected)
            delta = 0;
        else if (i == (uint8_t)((ui_home_selected + 1U) % 5U))
            delta = 1;
        else if (i == (uint8_t)((ui_home_selected + 4U) % 5U))
            delta = -1;
        else
            continue;

        if (delta >= -1 && delta <= 1)
        {
            int16_t icon_x = (int16_t)(48 + delta * 44);
            ui_draw_icon(i, (int16_t)(x + icon_x), 2,
                         i == ui_home_selected ? 1U : 0U);
        }
    }
    OLED_DrawRBox((int16_t)(x + 8), 42, 112U, 22U, 4U);
    OLED_SetDrawMode(OLED_DRAW_CLEAR);
    ui_text_big((int16_t)(x + 64 - (int16_t)(strlen(labels[ui_home_selected]) * 8U)),
                44, labels[ui_home_selected]);
    OLED_SetDrawMode(OLED_DRAW_SET);
    ui_text_big((int16_t)(x + 1), 44, "<");
    ui_text_big((int16_t)(x + 113), 44, ">");
}

void KK_UI_CustomOnEnter(KK_UI_PageId page)
{
    (void)page;
    KK_UI_Invalidate();
}

void KK_UI_CustomOnLeave(KK_UI_PageId page)
{
    (void)page;
}

void KK_UI_CustomOnInput(KK_UI_PageId page, KK_UI_InputEvent event)
{
    (void)page;
    if (ui_confirm_action != 0U)
    {
        if (event.action == KK_UI_INPUT_UP || event.action == KK_UI_INPUT_DOWN)
        {
            ui_confirm_focus = (uint8_t)(ui_confirm_focus == 0U ? 1U : 0U);
            KK_UI_Invalidate();
        }
        else if (event.action == KK_UI_INPUT_OK)
        {
            if (ui_confirm_focus != 0U)
            {
                if (ui_confirm_action == 1U)
                {
                    referee_state_toggle_team();
                    referee_control_refresh_led();
                    ui_toast_text = "TEAM OK";
                }
                else
                {
                    referee_state_reset();
                    referee_control_refresh_led();
                    ui_toast_text = "RESET OK";
                }
                ui_toast_until = tx_time_get() + 1500U;
            }
            ui_confirm_action = 0U;
            KK_UI_Invalidate();
        }
    }
    else if (ui_page == 5U && event.action == KK_UI_INPUT_UP)
    {
        ui_settings_selected = (uint8_t)((ui_settings_selected + 1U) % 2U);
        KK_UI_Invalidate();
    }
    else if (ui_page == 5U && event.action == KK_UI_INPUT_DOWN)
    {
        ui_settings_selected = (uint8_t)((ui_settings_selected + 1U) % 2U);
        KK_UI_Invalidate();
    }
    else if (ui_page == 5U && event.action == KK_UI_INPUT_OK)
    {
        ui_confirm_action = (uint8_t)(ui_settings_selected + 1U);
        ui_confirm_focus = 0U;
        KK_UI_Invalidate();
    }
    else if (ui_page == 2U && event.action == KK_UI_INPUT_UP)
    {
        ui_armor_offset = 0U;
        KK_UI_Invalidate();
    }
    else if (ui_page == 2U && event.action == KK_UI_INPUT_DOWN)
    {
        ui_armor_offset = 2U;
        KK_UI_Invalidate();
    }
    else if (ui_page == 0U && event.action == KK_UI_INPUT_UP)
    {
        ui_home_selected = (uint8_t)((ui_home_selected + 4U) % 5U);
        KK_UI_Invalidate();
    }
    else if (ui_page == 0U && event.action == KK_UI_INPUT_DOWN)
    {
        ui_home_selected = (uint8_t)((ui_home_selected + 1U) % 5U);
        KK_UI_Invalidate();
    }
    else if (ui_page == 0U && event.action == KK_UI_INPUT_OK)
    {
        ui_page = (uint8_t)(ui_home_selected + 1U);
        if (ui_page == 2U)
        {
            ui_armor_offset = 0U;
        }
        if (ui_page == 5U)
        {
            ui_settings_selected = 0U;
        }
        KK_UI_Invalidate();
    }
    else if (ui_page != 0U && event.action == KK_UI_INPUT_OK)
    {
        ui_page = 0U;
        KK_UI_Invalidate();
    }
}

bool KK_UI_CustomOnTick(KK_UI_PageId page, uint32_t now_ms)
{
    static uint32_t last_redraw;
    (void)page;
    if (ui_toast_until != 0U &&
        (int32_t)(now_ms - ui_toast_until) >= 0)
    {
        ui_toast_until = 0U;
        ui_toast_text = 0;
        return true;
    }
    if ((now_ms - last_redraw) >= 100U)
    {
        last_redraw = now_ms;
        return true;
    }
    return false;
}

void KK_UI_CustomOnDraw(KK_UI_PageId page, int16_t x_offset,
                        int16_t clip_x, uint16_t clip_width)
{
    (void)page;
    OLED_SetClipWindow(clip_x, 0, clip_width, 64U);
    switch (ui_page)
    {
    case 0U: ui_draw_home(x_offset); break;
    case 1U: ui_draw_main(x_offset); break;
    case 2U: ui_draw_armor(x_offset); break;
    case 3U: ui_draw_protocol(x_offset); break;
    case 5U: ui_draw_settings(x_offset); break;
    default: ui_draw_debug(x_offset); break;
    }
    if (ui_confirm_action != 0U)
    {
        ui_draw_confirm(x_offset);
    }
    else if (ui_page == 5U)
    {
        ui_draw_toast(x_offset);
    }
    OLED_ResetClipWindow();
}

static UINT ui_display_start(void)
{
    OLED_Status status;
    KK_UI_Status ui_status;

    if (!ui_display_ready)
    {
        status = OLED_Init();
        if (status != OLED_OK)
        {
            LOG_W("OLED init failed: %d", (int)status);
            return TX_NOT_DONE;
        }
        ui_status = KK_UI_Init(&ui_app);
        if (ui_status != KK_UI_OK)
        {
            LOG_E("KK_UI init failed: %d", (int)ui_status);
            return TX_NOT_DONE;
        }
        ui_display_ready = 1U;
    }
    (void)OLED_SetPowerSave(false);
    ui_active = 1U;
    ui_page = 0U;
    ui_home_selected = 0U;
    ui_armor_offset = 0U;
    ui_settings_selected = 0U;
    ui_confirm_action = 0U;
    ui_confirm_focus = 0U;
    ui_toast_until = 0U;
    ui_toast_text = 0;
    KK_UI_Invalidate();
    return TX_SUCCESS;
}

static void ui_display_stop(void)
{
    ui_active = 0U;
    if (ui_display_ready)
    {
        (void)OLED_SetPowerSave(true);
    }
}

static void ui_thread_entry(ULONG argument)
{
    ULONG now;

    (void)argument;
    now = tx_time_get();
    ui_button_init(&ui_key1, ui_read_pressed(KEY_1_GPIO_Port, KEY_1_Pin), now);
    ui_button_init(&ui_key2, ui_read_pressed(KEY_2_GPIO_Port, KEY_2_Pin), now);
    ui_button_init(&ui_key3, ui_read_pressed(KEY_3_GPIO_Port, KEY_3_Pin), now);
    ui_button_init(&ui_key4, ui_read_pressed(KEY_4_GPIO_Port, KEY_4_Pin), now);

    while (1)
    {
        uint8_t key1;
        uint8_t key2;
        uint8_t key3;
        uint8_t key4;

        now = tx_time_get();
        key1 = ui_read_pressed(KEY_1_GPIO_Port, KEY_1_Pin);
        key2 = ui_read_pressed(KEY_2_GPIO_Port, KEY_2_Pin);
        key3 = ui_read_pressed(KEY_3_GPIO_Port, KEY_3_Pin);
        key4 = ui_read_pressed(KEY_4_GPIO_Port, KEY_4_Pin);

        if (!ui_active)
        {
            if (ui_button_press(&ui_key1, key1, now) != 0U)
            {
                referee_state_toggle_team();
                referee_control_refresh_led();
            }
            if (ui_button_press(&ui_key2, key2, now) != 0U)
            {
                referee_state_reset();
                referee_control_refresh_led();
            }
            if (ui_button_press(&ui_key3, key3, now) != 0U ||
                ui_button_press(&ui_key4, key4, now) != 0U)
            {
                (void)ui_display_start();
            }
        }
        else
        {
            if (ui_button_press(&ui_key4, key4, now) != 0U)
            {
                /* K4 is the global UI-exit key only on the icon home.  Once
                 * inside an application page it behaves as a back key; in a
                 * confirmation dialog it first cancels that dialog. */
                if (ui_confirm_action != 0U)
                {
                    ui_confirm_action = 0U;
                    ui_confirm_focus = 0U;
                    KK_UI_Invalidate();
                }
                else if (ui_page != 0U)
                {
                    ui_page = 0U;
                    KK_UI_Invalidate();
                }
                else
                {
                    ui_display_stop();
                }
            }
            else
            {
                KK_UI_Input input = {
                    (uint8_t)((key1 ? KK_UI_KEY_UP : 0U) |
                              (key2 ? KK_UI_KEY_DOWN : 0U) |
                              (key3 ? KK_UI_KEY_OK : 0U)),
                    0
                };
                if (KK_UI_Update(now, input) != KK_UI_OK)
                {
                    (void)KK_UI_RecoverDisplay();
                }
            }
        }
        tx_thread_sleep(REFEREE_MAIN_UI_THREAD_PERIOD_MS);
    }
}

UINT referee_ui_init(void)
{
    return tx_thread_create(&ui_thread,
                            "referee_ui",
                            ui_thread_entry,
                            0,
                            ui_thread_stack,
                            sizeof(ui_thread_stack),
                            18,
                            18,
                            TX_NO_TIME_SLICE,
                            TX_AUTO_START);
}
