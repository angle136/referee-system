#include "referee_ui.h"

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
static uint8_t ui_page;

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
    else if (character == '>') { source[1] = 0x08; source[2] = 0x14; source[3] = 0x22; }
    else if (character == '<') { source[1] = 0x22; source[2] = 0x14; source[3] = 0x08; }
    else if (character == '=') { source[1] = source[2] = source[3] = 0x14; }

    /* Expand the compact source glyph to a regular 7x9 cell. This keeps the
     * application font self-contained while making the visual stroke and
     * spacing appropriate for the 128x64 panel. */
    for (i = 0U; i < 7U; ++i)
    {
        uint8_t source_column = (uint8_t)(((uint16_t)i * 5U) / 7U);
        uint8_t row;

        for (row = 0U; row < 9U; ++row)
        {
            uint8_t source_row = (uint8_t)(((uint16_t)row * 7U) / 9U);
            if ((source[source_column] & (uint8_t)(1U << source_row)) != 0U)
            {
                columns[i] |= (uint16_t)(1U << row);
            }
        }
    }
}

static void ui_text(int16_t x, int16_t y, const char *text)
{
    while (text != 0 && *text != '\0')
    {
        uint16_t glyph[7];
        uint8_t column;
        uint8_t row;

        ui_put_glyph(*text++, glyph);
        for (column = 0U; column < 7U; ++column)
        {
            for (row = 0U; row < 9U; ++row)
            {
                if ((glyph[column] & (uint8_t)(1U << row)) != 0U)
                {
                    OLED_DrawPixel(x + column, y + row);
                }
            }
        }
        x = (int16_t)(x + 8);
    }
}

static int16_t ui_u32(int16_t x, int16_t y, uint32_t value)
{
    char digits[11];
    uint8_t count = 0U;
    uint8_t i;

    do
    {
        digits[count++] = (char)('0' + (value % 10U));
        value /= 10U;
    } while (value != 0U && count < sizeof(digits));
    for (i = 0U; i < count / 2U; ++i)
    {
        char swap = digits[i];
        digits[i] = digits[count - 1U - i];
        digits[count - 1U - i] = swap;
    }
    digits[count] = '\0';
    ui_text(x, y, digits);
    return (int16_t)(x + count * 8U);
}

static void ui_title(int16_t x, const char *title)
{
    ui_text(x, 0, title);
    OLED_DrawHLine(x, 10, 128U);
}

static void ui_footer(int16_t x)
{
    ui_text((int16_t)(x + 104), 55, "<>");
    ui_text((int16_t)(x + 8), 55, "P");
    ui_u32((int16_t)(x + 16), 55, (uint32_t)ui_page + 1U);
    ui_text((int16_t)(x + 32), 55, "/4");
}

static void ui_draw_main(int16_t x)
{
    referee_state_snapshot_t state;
    referee_control_diagnostics_t control;

    referee_state_get_snapshot(&state);
    referee_control_get_diagnostics(&control);
    ui_title(x, "MAIN");
    ui_text((int16_t)(x + 2), 13, state.team == REFEREE_MAIN_TEAM_BLUE ? "TEAM B ID" : "TEAM R ID");
    ui_u32((int16_t)(x + 82), 13, state.team == REFEREE_MAIN_TEAM_BLUE ? REFEREE_MAIN_ROBOT_ID_BLUE : REFEREE_MAIN_ROBOT_ID_RED);
    ui_text((int16_t)(x + 2), 25, "HP");
    ui_u32((int16_t)(x + 26), 25, state.current_hp);
    ui_text((int16_t)(x + 58), 25, "/");
    ui_u32((int16_t)(x + 66), 25, state.maximum_hp);
    ui_text((int16_t)(x + 2), 37, "HIT");
    ui_u32((int16_t)(x + 34), 37, state.hit_count);
    ui_text((int16_t)(x + 70), 37, "TX");
    ui_u32((int16_t)(x + 94), 37, control.referee_tx_count);
    ui_footer(x);
}

static void ui_draw_armor(int16_t x)
{
    uint8_t port;
    ui_title(x, "ARMOR");
    for (port = 0U; port < REFEREE_MAIN_ARMOR_COUNT; ++port)
    {
        armor_link_diagnostics_t diagnostics;
        referee_state_snapshot_t state;

        armor_link_get_diagnostics(port, &diagnostics);
        referee_state_get_snapshot(&state);
        int16_t y = (int16_t)(13 + port * 10U);
        ui_text((int16_t)(x + 2), y, "P");
        ui_u32((int16_t)(x + 10), y, port);
        ui_text((int16_t)(x + 26), y, (state.online_mask & (1U << port)) != 0U ? "ON" : "--");
        ui_text((int16_t)(x + 50), y, "R");
        ui_u32((int16_t)(x + 58), y, diagnostics.packet_count);
        ui_text((int16_t)(x + 90), y, "H");
        ui_u32((int16_t)(x + 98), y, diagnostics.hit_count);
    }
    ui_footer(x);
}

static void ui_draw_protocol(int16_t x)
{
    referee_control_diagnostics_t control;
    referee_control_get_diagnostics(&control);
    ui_title(x, "PROTOCOL");
    ui_text((int16_t)(x + 2), 13, "TX");
    ui_u32((int16_t)(x + 26), 13, control.referee_tx_count);
    ui_text((int16_t)(x + 70), 13, "E");
    ui_u32((int16_t)(x + 78), 13, control.referee_tx_error_count);
    ui_text((int16_t)(x + 2), 25, "CFG");
    ui_u32((int16_t)(x + 34), 25, control.armor_config_tx_count);
    ui_text((int16_t)(x + 70), 25, "E");
    ui_u32((int16_t)(x + 78), 25, control.armor_config_tx_error_count);
    ui_text((int16_t)(x + 2), 37, "CMD");
    ui_u32((int16_t)(x + 34), 37, control.last_command_id);
    ui_text((int16_t)(x + 2), 43, "OUT");
    ui_text((int16_t)(x + 34), 43, control.referee_tx_error_count == 0U ? "OK" : "ERR");
    ui_footer(x);
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
    ui_title(x, "DEBUG");
    ui_text((int16_t)(x + 2), 13, "CRC");
    ui_u32((int16_t)(x + 34), 13, crc);
    ui_text((int16_t)(x + 70), 13, "DUP");
    ui_u32((int16_t)(x + 102), 13, duplicate);
    ui_text((int16_t)(x + 2), 25, "DROP");
    ui_u32((int16_t)(x + 42), 25, control.armor_event_drop_count);
    ui_text((int16_t)(x + 2), 37, "LAST P");
    ui_u32((int16_t)(x + 58), 37, state.last_hit_armor_id);
    ui_footer(x);
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
    if (event.action == KK_UI_INPUT_UP)
    {
        ui_page = (uint8_t)((ui_page + 3U) % 4U);
        KK_UI_Invalidate();
    }
    else if (event.action == KK_UI_INPUT_DOWN)
    {
        ui_page = (uint8_t)((ui_page + 1U) % 4U);
        KK_UI_Invalidate();
    }
}

bool KK_UI_CustomOnTick(KK_UI_PageId page, uint32_t now_ms)
{
    static uint32_t last_redraw;
    (void)page;
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
    case 0U: ui_draw_main(x_offset); break;
    case 1U: ui_draw_armor(x_offset); break;
    case 2U: ui_draw_protocol(x_offset); break;
    default: ui_draw_debug(x_offset); break;
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
                ui_display_stop();
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
