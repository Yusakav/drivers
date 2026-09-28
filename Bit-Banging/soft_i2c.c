/**
 * @file soft_i2c.c
 * @brief Platform-independent, open-drain software I2C master.
 */
#include "soft_i2c.h"

#define SOFT_I2C_MSG_FLAGS_MASK \
    (SOFT_I2C_MSG_READ | SOFT_I2C_MSG_TEN_BIT | SOFT_I2C_MSG_RECV_LEN)

static void i2c_delay(const soft_i2c_bus_t *bus, uint32_t us)
{
    bus->config.delay_us(bus->config.user, us);
}

static void i2c_delay_half(const soft_i2c_bus_t *bus)
{
    i2c_delay(bus, bus->config.half_period_us);
}

static void i2c_release(const soft_i2c_bus_t *bus)
{
    bus->config.write_sda(bus->config.user, 1U);
    bus->config.write_scl(bus->config.user, 1U);
}

static soft_i2c_ret_t i2c_wait_scl_high(const soft_i2c_bus_t *bus)
{
    uint32_t remaining = bus->config.scl_timeout_us;

    if (remaining == 0U) {
        remaining = SOFT_I2C_DEFAULT_TIMEOUT_US;
    }
    while (bus->config.read_scl(bus->config.user) == 0U) {
        if (remaining == 0U) {
            return SOFT_I2C_ERR_TIMEOUT;
        }
        i2c_delay(bus, 1U);
        --remaining;
    }
    return SOFT_I2C_OK;
}

static soft_i2c_ret_t i2c_start(const soft_i2c_bus_t *bus, uint8_t repeated)
{
    soft_i2c_ret_t ret;

    bus->config.write_sda(bus->config.user, 1U);
    i2c_delay_half(bus);
    bus->config.write_scl(bus->config.user, 1U);
    ret = i2c_wait_scl_high(bus);
    if (ret != SOFT_I2C_OK) {
        return ret;
    }
    if (bus->config.read_sda(bus->config.user) == 0U) {
        return (repeated != 0U) ? SOFT_I2C_ERR_ARBITRATION
                                : SOFT_I2C_ERR_BUS_BUSY;
    }
    i2c_delay_half(bus);
    bus->config.write_sda(bus->config.user, 0U);
    i2c_delay_half(bus);
    bus->config.write_scl(bus->config.user, 0U);
    i2c_delay_half(bus);
    return SOFT_I2C_OK;
}

static soft_i2c_ret_t i2c_stop(const soft_i2c_bus_t *bus)
{
    soft_i2c_ret_t ret;

    bus->config.write_sda(bus->config.user, 0U);
    i2c_delay_half(bus);
    bus->config.write_scl(bus->config.user, 1U);
    ret = i2c_wait_scl_high(bus);
    if (ret != SOFT_I2C_OK) {
        bus->config.write_sda(bus->config.user, 1U);
        return ret;
    }
    i2c_delay_half(bus);
    bus->config.write_sda(bus->config.user, 1U);
    i2c_delay_half(bus);
    return (bus->config.read_sda(bus->config.user) != 0U)
               ? SOFT_I2C_OK
               : SOFT_I2C_ERR_BUS_BUSY;
}

static soft_i2c_ret_t i2c_write_bit(const soft_i2c_bus_t *bus, uint8_t bit)
{
    soft_i2c_ret_t ret;

    bus->config.write_sda(bus->config.user, bit);
    i2c_delay_half(bus);
    bus->config.write_scl(bus->config.user, 1U);
    ret = i2c_wait_scl_high(bus);
    if (ret != SOFT_I2C_OK) {
        return ret;
    }
    if ((bit != 0U) && (bus->config.read_sda(bus->config.user) == 0U)) {
        return SOFT_I2C_ERR_ARBITRATION;
    }
    i2c_delay_half(bus);
    bus->config.write_scl(bus->config.user, 0U);
    i2c_delay_half(bus);
    return SOFT_I2C_OK;
}

static soft_i2c_ret_t i2c_write_byte(const soft_i2c_bus_t *bus,
                                     uint8_t value,
                                     soft_i2c_ret_t nack_error)
{
    uint8_t bit;
    soft_i2c_ret_t ret;

    for (bit = 0U; bit < 8U; ++bit) {
        ret = i2c_write_bit(bus, (uint8_t)((value & 0x80U) != 0U));
        if (ret != SOFT_I2C_OK) {
            return ret;
        }
        value = (uint8_t)(value << 1U);
    }

    bus->config.write_sda(bus->config.user, 1U);
    i2c_delay_half(bus);
    bus->config.write_scl(bus->config.user, 1U);
    ret = i2c_wait_scl_high(bus);
    if (ret != SOFT_I2C_OK) {
        return ret;
    }
    ret = (bus->config.read_sda(bus->config.user) == 0U)
              ? SOFT_I2C_OK
              : nack_error;
    i2c_delay_half(bus);
    bus->config.write_scl(bus->config.user, 0U);
    i2c_delay_half(bus);
    return ret;
}

static soft_i2c_ret_t i2c_read_byte_bits(const soft_i2c_bus_t *bus,
                                         uint8_t *value)
{
    uint8_t bit;
    uint8_t result = 0U;
    soft_i2c_ret_t ret;

    bus->config.write_sda(bus->config.user, 1U);
    for (bit = 0U; bit < 8U; ++bit) {
        i2c_delay_half(bus);
        bus->config.write_scl(bus->config.user, 1U);
        ret = i2c_wait_scl_high(bus);
        if (ret != SOFT_I2C_OK) {
            return ret;
        }
        result = (uint8_t)(result << 1U);
        if (bus->config.read_sda(bus->config.user) != 0U) {
            result = (uint8_t)(result | 1U);
        }
        i2c_delay_half(bus);
        bus->config.write_scl(bus->config.user, 0U);
        i2c_delay_half(bus);
    }
    *value = result;
    return SOFT_I2C_OK;
}

static soft_i2c_ret_t i2c_read_byte(const soft_i2c_bus_t *bus,
                                    uint8_t *value,
                                    uint8_t acknowledge)
{
    soft_i2c_ret_t ret = i2c_read_byte_bits(bus, value);
    if (ret != SOFT_I2C_OK) {
        return ret;
    }
    return i2c_write_bit(bus,
                         (uint8_t)((acknowledge != 0U) ? 0U : 1U));
}

static soft_i2c_ret_t i2c_send_address(const soft_i2c_bus_t *bus,
                                       const soft_i2c_msg_t *message)
{
    uint8_t read = (uint8_t)((message->flags & SOFT_I2C_MSG_READ) != 0U);
    soft_i2c_ret_t ret;

    if ((message->flags & SOFT_I2C_MSG_TEN_BIT) == 0U) {
        uint8_t address = (uint8_t)((message->address << 1U) | read);
        return i2c_write_byte(bus, address, SOFT_I2C_ERR_ADDR_NACK);
    }

    {
        uint8_t header = (uint8_t)(0xF0U |
                           (uint8_t)(((message->address >> 8U) & 0x03U) << 1U));
        ret = i2c_write_byte(bus, header, SOFT_I2C_ERR_ADDR_NACK);
        if (ret != SOFT_I2C_OK) {
            return ret;
        }
        ret = i2c_write_byte(bus, (uint8_t)message->address,
                             SOFT_I2C_ERR_ADDR_NACK);
        if ((ret != SOFT_I2C_OK) || (read == 0U)) {
            return ret;
        }
        ret = i2c_start(bus, 1U);
        if (ret != SOFT_I2C_OK) {
            return ret;
        }
        return i2c_write_byte(bus, (uint8_t)(header | 1U),
                              SOFT_I2C_ERR_ADDR_NACK);
    }
}

static soft_i2c_ret_t i2c_validate_message(const soft_i2c_msg_t *message)
{
    if ((message->flags & (uint16_t)(~SOFT_I2C_MSG_FLAGS_MASK)) != 0U) {
        return SOFT_I2C_ERR_PARAM;
    }
    if ((((message->flags & SOFT_I2C_MSG_TEN_BIT) != 0U) &&
         (message->address > 0x03FFU)) ||
        (((message->flags & SOFT_I2C_MSG_TEN_BIT) == 0U) &&
         (message->address > 0x007FU))) {
        return SOFT_I2C_ERR_PARAM;
    }
    if ((message->length > 0U) && (message->buffer == NULL)) {
        return SOFT_I2C_ERR_NULL;
    }
    if ((message->flags & SOFT_I2C_MSG_RECV_LEN) != 0U) {
        if (((message->flags & SOFT_I2C_MSG_READ) == 0U) ||
            (message->length == 0U)) {
            return SOFT_I2C_ERR_PARAM;
        }
    } else if (message->trailing_bytes != 0U) {
        return SOFT_I2C_ERR_PARAM;
    }
    return SOFT_I2C_OK;
}

soft_i2c_ret_t soft_i2c_init(soft_i2c_bus_t *bus,
                             const soft_i2c_config_t *config)
{
    if ((bus == NULL) || (config == NULL)) {
        return SOFT_I2C_ERR_NULL;
    }
    if ((config->write_scl == NULL) || (config->write_sda == NULL) ||
        (config->read_scl == NULL) || (config->read_sda == NULL) ||
        (config->delay_us == NULL) || (config->half_period_us == 0U)) {
        return SOFT_I2C_ERR_PARAM;
    }
    bus->config = *config;
    bus->initialized = 1U;
    i2c_release(bus);
    return SOFT_I2C_OK;
}

soft_i2c_ret_t soft_i2c_transfer(soft_i2c_bus_t *bus,
                                 soft_i2c_msg_t *messages,
                                 size_t count)
{
    size_t index;
    uint8_t transaction_started = 0U;
    soft_i2c_ret_t ret;

    if ((bus == NULL) || (messages == NULL)) {
        return SOFT_I2C_ERR_NULL;
    }
    if ((bus->initialized == 0U) || (count == 0U)) {
        return SOFT_I2C_ERR_PARAM;
    }
    for (index = 0U; index < count; ++index) {
        ret = i2c_validate_message(&messages[index]);
        if (ret != SOFT_I2C_OK) {
            return ret;
        }
        messages[index].transferred = 0U;
    }

    for (index = 0U; index < count; ++index) {
        soft_i2c_msg_t *message = &messages[index];
        size_t transfer_length = message->length;
        size_t byte_index;

        ret = i2c_start(bus, (uint8_t)(index != 0U));
        if (ret != SOFT_I2C_OK) {
            goto transfer_error;
        }
        transaction_started = 1U;
        ret = i2c_send_address(bus, message);
        if (ret != SOFT_I2C_OK) {
            goto transfer_error;
        }

        if ((message->flags & SOFT_I2C_MSG_READ) != 0U) {
            if ((message->flags & SOFT_I2C_MSG_RECV_LEN) != 0U) {
                ret = i2c_read_byte_bits(bus, &message->buffer[0]);
                if (ret != SOFT_I2C_OK) {
                    goto transfer_error;
                }
                message->transferred = 1U;
                transfer_length = 1U + (size_t)message->buffer[0] +
                                  (size_t)message->trailing_bytes;
                if (transfer_length > message->length) {
                    (void)i2c_write_bit(bus, 1U);
                    ret = SOFT_I2C_ERR_BUFFER;
                    goto transfer_error;
                }
                ret = i2c_write_bit(bus,
                                    (uint8_t)((transfer_length > 1U) ? 0U : 1U));
                if (ret != SOFT_I2C_OK) {
                    goto transfer_error;
                }
            }
            byte_index = ((message->flags & SOFT_I2C_MSG_RECV_LEN) != 0U)
                             ? 1U
                             : 0U;
            for (; byte_index < transfer_length; ++byte_index) {
                uint8_t acknowledge = (uint8_t)(byte_index + 1U < transfer_length);
                ret = i2c_read_byte(bus, &message->buffer[byte_index], acknowledge);
                if (ret != SOFT_I2C_OK) {
                    goto transfer_error;
                }
                message->transferred = byte_index + 1U;
            }
        } else {
            for (byte_index = 0U; byte_index < transfer_length; ++byte_index) {
                ret = i2c_write_byte(bus, message->buffer[byte_index],
                                     SOFT_I2C_ERR_DATA_NACK);
                if (ret != SOFT_I2C_OK) {
                    goto transfer_error;
                }
                message->transferred = byte_index + 1U;
            }
        }
    }

    return i2c_stop(bus);

transfer_error:
    if ((ret == SOFT_I2C_ERR_ARBITRATION) ||
        (transaction_started == 0U)) {
        i2c_release(bus);
        return ret;
    }
    (void)i2c_stop(bus);
    return ret;
}

soft_i2c_ret_t soft_i2c_probe(soft_i2c_bus_t *bus,
                              uint16_t address,
                              uint8_t ten_bit)
{
    soft_i2c_msg_t message = {
        address,
        (uint16_t)((ten_bit != 0U) ? SOFT_I2C_MSG_TEN_BIT : 0U),
        NULL,
        0U,
        0U,
        0U,
    };
    return soft_i2c_transfer(bus, &message, 1U);
}

soft_i2c_ret_t soft_i2c_recover(soft_i2c_bus_t *bus)
{
    uint8_t pulse;
    soft_i2c_ret_t ret;

    if (bus == NULL) {
        return SOFT_I2C_ERR_NULL;
    }
    if (bus->initialized == 0U) {
        return SOFT_I2C_ERR_PARAM;
    }

    bus->config.write_sda(bus->config.user, 1U);
    bus->config.write_scl(bus->config.user, 1U);
    ret = i2c_wait_scl_high(bus);
    if (ret != SOFT_I2C_OK) {
        return ret;
    }

    for (pulse = 0U;
         (pulse < 9U) && (bus->config.read_sda(bus->config.user) == 0U);
         ++pulse) {
        bus->config.write_scl(bus->config.user, 0U);
        i2c_delay_half(bus);
        bus->config.write_scl(bus->config.user, 1U);
        ret = i2c_wait_scl_high(bus);
        if (ret != SOFT_I2C_OK) {
            return ret;
        }
        i2c_delay_half(bus);
    }
    return i2c_stop(bus);
}
