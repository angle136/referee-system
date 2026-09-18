#include "kk_oled_driver.h"

#include "i2c.h"
#include "kk_oled_internal.h"

/* SSD1306 128x64 I2C module, no external reset pin. */
#define OLED_I2C_ADDRESS_7BIT       0x3CU
#define OLED_I2C_ADDRESS_HAL        ((uint16_t)(OLED_I2C_ADDRESS_7BIT << 1U))
#define OLED_CONTROL_COMMAND        0x00U
#define OLED_CONTROL_DATA           0x40U
#define OLED_COLUMN_OFFSET          0U
#define OLED_BLOCKING_TIMEOUT_MS    100U

static volatile bool oled_driver_busy;

static OLED_Status oled_hal_status(HAL_StatusTypeDef status)
{
    if (status == HAL_OK) {
        return OLED_OK;
    }
    if (status == HAL_BUSY) {
        return OLED_BUSY;
    }
    return OLED_ERROR;
}

static HAL_StatusTypeDef oled_send_command_blocking(const uint8_t *command,
                                                     uint16_t length)
{
    return HAL_I2C_Mem_Write(&hi2c1, OLED_I2C_ADDRESS_HAL,
                             OLED_CONTROL_COMMAND, I2C_MEMADD_SIZE_8BIT,
                             (uint8_t *)command, length,
                             OLED_BLOCKING_TIMEOUT_MS);
}

static HAL_StatusTypeDef oled_send_data_blocking(const uint8_t *data,
                                                  uint16_t length)
{
    return HAL_I2C_Mem_Write(&hi2c1, OLED_I2C_ADDRESS_HAL,
                             OLED_CONTROL_DATA, I2C_MEMADD_SIZE_8BIT,
                             (uint8_t *)data, length,
                             OLED_BLOCKING_TIMEOUT_MS);
}

static void oled_prepare_page_command(uint8_t page, uint8_t min_x,
                                      uint8_t command[3])
{
    uint8_t column = (uint8_t)(min_x + OLED_COLUMN_OFFSET);

    command[0] = (uint8_t)(0xB0U | page);
    command[1] = (uint8_t)(column & 0x0FU);
    command[2] = (uint8_t)(0x10U | (column >> 4U));
}

OLED_Status OLED_DriverInit(void)
{
    static const uint8_t init_commands[] = {
        0xAEU,
        0xD5U, 0x80U,
        0xA8U, 0x3FU,
        0xD3U, 0x00U,
        0x40U,
        0x8DU, 0x14U,
        0x20U, 0x02U,
        0xA1U,
        0xC8U,
        0xDAU, 0x12U,
        0x81U, 0x7FU,
        0xD9U, 0xF1U,
        0xDBU, 0x40U,
        0xA4U,
        0xA6U
    };
    static const uint8_t zeros[OLED_PHYSICAL_WIDTH] = {0};
    uint8_t page;

    if (oled_driver_busy) {
        return OLED_BUSY;
    }
    oled_driver_busy = true;

    /* With no reset pin, power-up settling plus this full software sequence
     * is the only available reset path. */
    HAL_Delay(20U);
    if (oled_send_command_blocking(init_commands,
                                   (uint16_t)sizeof(init_commands)) != HAL_OK) {
        oled_driver_busy = false;
        return OLED_ERROR;
    }

    for (page = 0U; page < OLED_PHYSICAL_PAGES; ++page) {
        uint8_t command[3];

        oled_prepare_page_command(page, 0U, command);
        if (oled_send_command_blocking(command, (uint16_t)sizeof(command)) != HAL_OK ||
            oled_send_data_blocking(zeros, (uint16_t)sizeof(zeros)) != HAL_OK) {
            oled_driver_busy = false;
            return OLED_ERROR;
        }
    }

    {
        const uint8_t display_on = 0xAFU;
        if (oled_send_command_blocking(&display_on, 1U) != HAL_OK) {
            oled_driver_busy = false;
            return OLED_ERROR;
        }
    }

    oled_driver_busy = false;
    return OLED_OK;
}

OLED_Status OLED_DriverWriteBlocking(void)
{
    const uint8_t *buffer = OLED_InternalGetTransferBuffer();
    uint8_t page;

    if (oled_driver_busy) {
        return OLED_BUSY;
    }
    oled_driver_busy = true;

    for (page = 0U; page < OLED_PHYSICAL_PAGES; ++page) {
        uint8_t min_x = OLED_InternalGetTransferMinX(page);
        uint8_t max_x = OLED_InternalGetTransferMaxX(page);
        uint8_t command[3];
        uint16_t length;

        if (min_x >= OLED_PHYSICAL_WIDTH) {
            continue;
        }
        oled_prepare_page_command(page, min_x, command);
        length = (uint16_t)max_x - min_x + 1U;
        if (oled_send_command_blocking(command, (uint16_t)sizeof(command)) != HAL_OK ||
            oled_send_data_blocking(buffer + (uint16_t)page * OLED_PHYSICAL_WIDTH + min_x,
                                    length) != HAL_OK) {
            oled_driver_busy = false;
            return OLED_ERROR;
        }
    }

    oled_driver_busy = false;
    return OLED_OK;
}

OLED_Status OLED_DriverWriteIT(void)
{
    return OLED_UNSUPPORTED;
}

OLED_Status OLED_DriverWriteDMA(void)
{
    return OLED_UNSUPPORTED;
}

bool OLED_DriverIsBusy(void)
{
    return oled_driver_busy;
}

OLED_Status OLED_DriverSetContrast(uint8_t value)
{
    const uint8_t command[2] = {0x81U, value};

    if (oled_driver_busy) {
        return OLED_BUSY;
    }
    return oled_hal_status(oled_send_command_blocking(command,
                                                       (uint16_t)sizeof(command)));
}

OLED_Status OLED_DriverSetPowerSave(bool enable)
{
    uint8_t command = enable ? 0xAEU : 0xAFU;

    if (oled_driver_busy) {
        return OLED_BUSY;
    }
    return oled_hal_status(oled_send_command_blocking(&command, 1U));
}

void OLED_DriverHandleMemTxComplete(void)
{
    /* IT/DMA refresh is deliberately unsupported by this first port. */
}

void OLED_DriverHandleError(void)
{
    /* IT/DMA refresh is deliberately unsupported by this first port. */
}
