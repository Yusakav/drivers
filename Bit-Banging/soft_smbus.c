/**
 * @file soft_smbus.c
 * @brief SMBus convenience operations built on soft_i2c messages.
 */
#include "soft_smbus.h"

#include <string.h>

static uint8_t smbus_crc8(uint8_t crc, uint8_t value)
{
    uint8_t bit;

    crc = (uint8_t)(crc ^ value);
    for (bit = 0U; bit < 8U; ++bit) {
        crc = ((crc & 0x80U) != 0U)
                  ? (uint8_t)((uint8_t)(crc << 1U) ^ 0x07U)
                  : (uint8_t)(crc << 1U);
    }
    return crc;
}

static uint8_t smbus_crc_bytes(uint8_t crc,
                               const uint8_t *data,
                               size_t length)
{
    size_t index;
    for (index = 0U; index < length; ++index) {
        crc = smbus_crc8(crc, data[index]);
    }
    return crc;
}

static uint8_t smbus_address_byte(uint8_t address, uint8_t read)
{
    return (uint8_t)((uint16_t)((uint16_t)address << 1U) |
                     (uint16_t)((read != 0U) ? 1U : 0U));
}

static soft_i2c_ret_t smbus_validate_pec(soft_smbus_pec_t pec)
{
    return ((pec == SOFT_SMBUS_PEC_DISABLED) ||
            (pec == SOFT_SMBUS_PEC_ENABLED))
               ? SOFT_I2C_OK
               : SOFT_I2C_ERR_PARAM;
}

static soft_i2c_ret_t smbus_write(soft_i2c_bus_t *bus,
                                  uint8_t address,
                                  uint8_t *payload,
                                  size_t length,
                                  soft_smbus_pec_t pec)
{
    soft_i2c_msg_t message;
    soft_i2c_ret_t ret = smbus_validate_pec(pec);

    if (ret != SOFT_I2C_OK) {
        return ret;
    }
    if (pec == SOFT_SMBUS_PEC_ENABLED) {
        uint8_t crc = smbus_crc8(0U, smbus_address_byte(address, 0U));
        payload[length] = smbus_crc_bytes(crc, payload, length);
        ++length;
    }
    message.address = address;
    message.flags = 0U;
    message.buffer = payload;
    message.length = length;
    message.transferred = 0U;
    message.trailing_bytes = 0U;
    return soft_i2c_transfer(bus, &message, 1U);
}

static soft_i2c_ret_t smbus_command_read(soft_i2c_bus_t *bus,
                                         uint8_t address,
                                         uint8_t command,
                                         uint8_t *data,
                                         size_t data_length,
                                         soft_smbus_pec_t pec)
{
    soft_i2c_msg_t messages[2];
    size_t rx_length = data_length;
    soft_i2c_ret_t ret = smbus_validate_pec(pec);

    if (ret != SOFT_I2C_OK) {
        return ret;
    }
    if (pec == SOFT_SMBUS_PEC_ENABLED) {
        ++rx_length;
    }
    messages[0].address = address;
    messages[0].flags = 0U;
    messages[0].buffer = &command;
    messages[0].length = 1U;
    messages[0].transferred = 0U;
    messages[0].trailing_bytes = 0U;
    messages[1].address = address;
    messages[1].flags = SOFT_I2C_MSG_READ;
    messages[1].buffer = data;
    messages[1].length = rx_length;
    messages[1].transferred = 0U;
    messages[1].trailing_bytes = 0U;
    ret = soft_i2c_transfer(bus, messages, 2U);
    if ((ret == SOFT_I2C_OK) && (pec == SOFT_SMBUS_PEC_ENABLED)) {
        uint8_t crc = smbus_crc8(0U, smbus_address_byte(address, 0U));
        crc = smbus_crc8(crc, command);
        crc = smbus_crc8(crc, smbus_address_byte(address, 1U));
        crc = smbus_crc_bytes(crc, data, data_length);
        if (crc != data[data_length]) {
            ret = SOFT_I2C_ERR_PEC;
        }
    }
    return ret;
}

soft_i2c_ret_t soft_smbus_quick(soft_i2c_bus_t *bus,
                                uint8_t address,
                                uint8_t read)
{
    soft_i2c_msg_t message = {
        address,
        (uint16_t)((read != 0U) ? SOFT_I2C_MSG_READ : 0U),
        NULL,
        0U,
        0U,
        0U,
    };
    return soft_i2c_transfer(bus, &message, 1U);
}

soft_i2c_ret_t soft_smbus_send_byte(soft_i2c_bus_t *bus,
                                    uint8_t address,
                                    uint8_t value,
                                    soft_smbus_pec_t pec)
{
    uint8_t payload[2];
    payload[0] = value;
    return smbus_write(bus, address, payload, 1U, pec);
}

soft_i2c_ret_t soft_smbus_receive_byte(soft_i2c_bus_t *bus,
                                       uint8_t address,
                                       uint8_t *value,
                                       soft_smbus_pec_t pec)
{
    uint8_t received[2];
    soft_i2c_msg_t message;
    size_t length;
    soft_i2c_ret_t ret;

    if (value == NULL) {
        return SOFT_I2C_ERR_NULL;
    }
    ret = smbus_validate_pec(pec);
    if (ret != SOFT_I2C_OK) {
        return ret;
    }
    length = (pec == SOFT_SMBUS_PEC_ENABLED) ? 2U : 1U;
    message.address = address;
    message.flags = SOFT_I2C_MSG_READ;
    message.buffer = received;
    message.length = length;
    message.transferred = 0U;
    message.trailing_bytes = 0U;
    ret = soft_i2c_transfer(bus, &message, 1U);
    if ((ret == SOFT_I2C_OK) && (pec == SOFT_SMBUS_PEC_ENABLED)) {
        uint8_t crc = smbus_crc8(0U, smbus_address_byte(address, 1U));
        crc = smbus_crc8(crc, received[0]);
        if (crc != received[1]) {
            ret = SOFT_I2C_ERR_PEC;
        }
    }
    if (ret == SOFT_I2C_OK) {
        *value = received[0];
    }
    return ret;
}

soft_i2c_ret_t soft_smbus_write_byte_data(soft_i2c_bus_t *bus,
                                          uint8_t address,
                                          uint8_t command,
                                          uint8_t value,
                                          soft_smbus_pec_t pec)
{
    uint8_t payload[3];
    payload[0] = command;
    payload[1] = value;
    return smbus_write(bus, address, payload, 2U, pec);
}

soft_i2c_ret_t soft_smbus_read_byte_data(soft_i2c_bus_t *bus,
                                         uint8_t address,
                                         uint8_t command,
                                         uint8_t *value,
                                         soft_smbus_pec_t pec)
{
    uint8_t received[2];
    soft_i2c_ret_t ret;

    if (value == NULL) {
        return SOFT_I2C_ERR_NULL;
    }
    ret = smbus_command_read(bus, address, command, received, 1U, pec);
    if (ret == SOFT_I2C_OK) {
        *value = received[0];
    }
    return ret;
}

soft_i2c_ret_t soft_smbus_write_word_data(soft_i2c_bus_t *bus,
                                          uint8_t address,
                                          uint8_t command,
                                          uint16_t value,
                                          soft_smbus_pec_t pec)
{
    uint8_t payload[4];
    payload[0] = command;
    payload[1] = (uint8_t)value;
    payload[2] = (uint8_t)(value >> 8U);
    return smbus_write(bus, address, payload, 3U, pec);
}

soft_i2c_ret_t soft_smbus_read_word_data(soft_i2c_bus_t *bus,
                                         uint8_t address,
                                         uint8_t command,
                                         uint16_t *value,
                                         soft_smbus_pec_t pec)
{
    uint8_t received[3];
    soft_i2c_ret_t ret;

    if (value == NULL) {
        return SOFT_I2C_ERR_NULL;
    }
    ret = smbus_command_read(bus, address, command, received, 2U, pec);
    if (ret == SOFT_I2C_OK) {
        *value = (uint16_t)((uint16_t)received[0] |
                            (uint16_t)((uint16_t)received[1] << 8U));
    }
    return ret;
}

soft_i2c_ret_t soft_smbus_write_block_data(soft_i2c_bus_t *bus,
                                           uint8_t address,
                                           uint8_t command,
                                           const uint8_t *data,
                                           size_t length,
                                           soft_smbus_pec_t pec)
{
    uint8_t payload[SOFT_SMBUS_BLOCK_MAX + 3U];

    if (data == NULL) {
        return SOFT_I2C_ERR_NULL;
    }
    if ((length == 0U) || (length > SOFT_SMBUS_BLOCK_MAX)) {
        return SOFT_I2C_ERR_PARAM;
    }
    payload[0] = command;
    payload[1] = (uint8_t)length;
    (void)memcpy(&payload[2], data, length);
    return smbus_write(bus, address, payload, length + 2U, pec);
}

soft_i2c_ret_t soft_smbus_read_block_data(soft_i2c_bus_t *bus,
                                          uint8_t address,
                                          uint8_t command,
                                          uint8_t *data,
                                          size_t capacity,
                                          size_t *length,
                                          soft_smbus_pec_t pec)
{
    uint8_t received[SOFT_SMBUS_BLOCK_MAX + 2U] = {0U};
    soft_i2c_msg_t messages[2];
    size_t block_length;
    soft_i2c_ret_t ret;

    if ((data == NULL) || (length == NULL)) {
        return SOFT_I2C_ERR_NULL;
    }
    *length = 0U;
    ret = smbus_validate_pec(pec);
    if (ret != SOFT_I2C_OK) {
        return ret;
    }
    messages[0].address = address;
    messages[0].flags = 0U;
    messages[0].buffer = &command;
    messages[0].length = 1U;
    messages[0].transferred = 0U;
    messages[0].trailing_bytes = 0U;
    messages[1].address = address;
    messages[1].flags = SOFT_I2C_MSG_READ | SOFT_I2C_MSG_RECV_LEN;
    messages[1].buffer = received;
    messages[1].length = sizeof(received);
    messages[1].transferred = 0U;
    messages[1].trailing_bytes =
        (uint8_t)((pec == SOFT_SMBUS_PEC_ENABLED) ? 1U : 0U);
    ret = soft_i2c_transfer(bus, messages, 2U);
    if (ret != SOFT_I2C_OK) {
        if ((ret == SOFT_I2C_ERR_BUFFER) &&
            (messages[1].transferred == 1U) &&
            (received[0] > SOFT_SMBUS_BLOCK_MAX)) {
            return SOFT_I2C_ERR_PROTOCOL;
        }
        return ret;
    }
    block_length = (size_t)received[0];
    if ((block_length == 0U) || (block_length > SOFT_SMBUS_BLOCK_MAX)) {
        return SOFT_I2C_ERR_PROTOCOL;
    }
    if (pec == SOFT_SMBUS_PEC_ENABLED) {
        uint8_t crc = smbus_crc8(0U, smbus_address_byte(address, 0U));
        crc = smbus_crc8(crc, command);
        crc = smbus_crc8(crc, smbus_address_byte(address, 1U));
        crc = smbus_crc_bytes(crc, received, block_length + 1U);
        if (crc != received[block_length + 1U]) {
            return SOFT_I2C_ERR_PEC;
        }
    }
    if (capacity < block_length) {
        return SOFT_I2C_ERR_BUFFER;
    }
    (void)memcpy(data, &received[1], block_length);
    *length = block_length;
    return SOFT_I2C_OK;
}
