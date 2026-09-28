#include "soft_i2c.h"
#include "soft_smbus.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef enum {
    MOCK_MASTER_BITS,
    MOCK_SLAVE_ACK,
    MOCK_SLAVE_BITS,
    MOCK_MASTER_ACK,
} mock_phase_t;

typedef struct {
    uint8_t master_scl;
    uint8_t master_sda;
    uint8_t actual_scl;
    uint8_t slave_sda;
    uint8_t started;
    uint8_t address_byte;
    uint8_t read_direction;
    uint8_t bit_count;
    uint8_t current_byte;
    mock_phase_t phase;

    uint8_t received[128];
    size_t received_count;
    uint8_t read_data[128];
    size_t read_count;
    size_t read_index;

    unsigned starts;
    unsigned stops;
    unsigned acknowledgements;
    unsigned nack_at;
    unsigned recovery_pulses;
    unsigned release_after_pulses;
    uint8_t stuck_sda;
    uint8_t force_arbitration;
    uint8_t stretch_forever;
    unsigned stretch_each;
    unsigned stretch_remaining;
} mock_i2c_t;

static void mock_falling_edge(mock_i2c_t *mock)
{
    if (mock->started == 0U) {
        if (mock->stuck_sda != 0U) {
            ++mock->recovery_pulses;
            if ((mock->release_after_pulses != 0U) &&
                (mock->recovery_pulses >= mock->release_after_pulses)) {
                mock->stuck_sda = 0U;
            }
        }
        return;
    }

    if ((mock->phase == MOCK_MASTER_BITS) && (mock->bit_count == 8U)) {
        mock->phase = MOCK_SLAVE_ACK;
        mock->bit_count = 0U;
    } else if (mock->phase == MOCK_SLAVE_ACK) {
        mock->slave_sda = 1U;
        mock->phase = (mock->read_direction != 0U)
                          ? MOCK_SLAVE_BITS
                          : MOCK_MASTER_BITS;
        mock->bit_count = 0U;
    } else if ((mock->phase == MOCK_SLAVE_BITS) && (mock->bit_count == 8U)) {
        mock->slave_sda = 1U;
        mock->phase = MOCK_MASTER_ACK;
        mock->bit_count = 0U;
        ++mock->read_index;
    } else if (mock->phase == MOCK_MASTER_ACK) {
        mock->phase = MOCK_SLAVE_BITS;
        mock->bit_count = 0U;
    }
}

static void mock_rising_edge(mock_i2c_t *mock)
{
    if (mock->started == 0U) {
        return;
    }
    if (mock->phase == MOCK_MASTER_BITS) {
        mock->current_byte = (uint8_t)(mock->current_byte << 1U);
        if (mock->master_sda != 0U) {
            mock->current_byte = (uint8_t)(mock->current_byte | 1U);
        }
        ++mock->bit_count;
        if (mock->bit_count == 8U) {
            assert(mock->received_count < sizeof(mock->received));
            mock->received[mock->received_count++] = mock->current_byte;
            if (mock->address_byte != 0U) {
                mock->read_direction = (uint8_t)(mock->current_byte & 1U);
                mock->address_byte = 0U;
            }
            mock->current_byte = 0U;
        }
    } else if (mock->phase == MOCK_SLAVE_ACK) {
        ++mock->acknowledgements;
        mock->slave_sda = (mock->acknowledgements == mock->nack_at) ? 1U : 0U;
    } else if (mock->phase == MOCK_SLAVE_BITS) {
        uint8_t value = 0xFFU;
        if (mock->read_index < mock->read_count) {
            value = mock->read_data[mock->read_index];
        }
        mock->slave_sda = (uint8_t)(((uint16_t)value >>
                                     (uint16_t)(7U - mock->bit_count)) &
                                    UINT16_C(1));
        ++mock->bit_count;
    }
}

static void mock_write_scl(void *user, uint8_t release_high)
{
    mock_i2c_t *mock = (mock_i2c_t *)user;
    uint8_t level = (uint8_t)(release_high != 0U);

    mock->master_scl = level;
    if (level == 0U) {
        if (mock->actual_scl != 0U) {
            mock_falling_edge(mock);
        }
        mock->actual_scl = 0U;
    } else if (mock->actual_scl == 0U) {
        mock->stretch_remaining = mock->stretch_each;
    }
}

static void mock_write_sda(void *user, uint8_t release_high)
{
    mock_i2c_t *mock = (mock_i2c_t *)user;
    uint8_t level = (uint8_t)(release_high != 0U);

    if (mock->actual_scl != 0U) {
        if ((mock->master_sda != 0U) && (level == 0U)) {
            mock->started = 1U;
            mock->address_byte = 1U;
            mock->read_direction = 0U;
            mock->phase = MOCK_MASTER_BITS;
            mock->bit_count = 0U;
            mock->current_byte = 0U;
            mock->slave_sda = 1U;
            ++mock->starts;
        } else if ((mock->master_sda == 0U) && (level != 0U)) {
            mock->started = 0U;
            mock->slave_sda = 1U;
            ++mock->stops;
        }
    }
    mock->master_sda = level;
}

static uint8_t mock_read_scl(void *user)
{
    mock_i2c_t *mock = (mock_i2c_t *)user;

    if (mock->master_scl == 0U) {
        return 0U;
    }
    if (mock->stretch_forever != 0U) {
        return 0U;
    }
    if (mock->stretch_remaining > 0U) {
        --mock->stretch_remaining;
        return 0U;
    }
    if (mock->actual_scl == 0U) {
        mock->actual_scl = 1U;
        mock_rising_edge(mock);
    }
    return 1U;
}

static uint8_t mock_read_sda(void *user)
{
    mock_i2c_t *mock = (mock_i2c_t *)user;

    if (mock->stuck_sda != 0U) {
        return 0U;
    }
    if ((mock->force_arbitration != 0U) && (mock->started != 0U) &&
        (mock->phase == MOCK_MASTER_BITS) && (mock->master_sda != 0U)) {
        return 0U;
    }
    return (uint8_t)(mock->master_sda & mock->slave_sda);
}

static void mock_delay(void *user, uint32_t us)
{
    (void)user;
    (void)us;
}

static void setup_bus(mock_i2c_t *mock, soft_i2c_bus_t *bus)
{
    soft_i2c_config_t config;

    (void)memset(mock, 0, sizeof(*mock));
    (void)memset(bus, 0, sizeof(*bus));
    mock->master_scl = 1U;
    mock->master_sda = 1U;
    mock->actual_scl = 1U;
    mock->slave_sda = 1U;
    config.user = mock;
    config.write_scl = mock_write_scl;
    config.write_sda = mock_write_sda;
    config.read_scl = mock_read_scl;
    config.read_sda = mock_read_sda;
    config.delay_us = mock_delay;
    config.half_period_us = 1U;
    config.scl_timeout_us = 4U;
    assert(soft_i2c_init(bus, &config) == SOFT_I2C_OK);
}

static uint8_t test_crc8(uint8_t crc, uint8_t value)
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

static void test_init_and_validation(void)
{
    soft_i2c_bus_t bus = {0};
    soft_i2c_config_t config = {0};
    soft_i2c_msg_t message = {0};

    assert(soft_i2c_init(NULL, &config) == SOFT_I2C_ERR_NULL);
    assert(soft_i2c_init(&bus, &config) == SOFT_I2C_ERR_PARAM);
    assert(soft_i2c_transfer(&bus, &message, 1U) == SOFT_I2C_ERR_PARAM);
}

static void test_seven_bit_combined_transfer(void)
{
    mock_i2c_t mock;
    soft_i2c_bus_t bus;
    uint8_t command = 0x10U;
    uint8_t data[2] = {0U, 0U};
    soft_i2c_msg_t messages[2] = {
        {0x50U, 0U, &command, 1U, 0U, 0U},
        {0x50U, SOFT_I2C_MSG_READ, data, sizeof(data), 0U, 0U},
    };

    setup_bus(&mock, &bus);
    mock.read_data[0] = 0xAAU;
    mock.read_data[1] = 0x55U;
    mock.read_count = 2U;
    assert(soft_i2c_transfer(&bus, messages, 2U) == SOFT_I2C_OK);
    assert(data[0] == 0xAAU && data[1] == 0x55U);
    assert(mock.starts == 2U && mock.stops == 1U);
    assert(mock.received_count == 3U);
    assert(mock.received[0] == 0xA0U);
    assert(mock.received[1] == 0x10U);
    assert(mock.received[2] == 0xA1U);
    assert(messages[0].transferred == 1U && messages[1].transferred == 2U);
}

static void test_ten_bit_read(void)
{
    mock_i2c_t mock;
    soft_i2c_bus_t bus;
    uint8_t value = 0U;
    uint8_t write_value = 0xC3U;
    soft_i2c_msg_t message = {
        0x02AAU, SOFT_I2C_MSG_READ | SOFT_I2C_MSG_TEN_BIT,
        &value, 1U, 0U, 0U,
    };

    setup_bus(&mock, &bus);
    mock.read_data[0] = 0x5AU;
    mock.read_count = 1U;
    assert(soft_i2c_transfer(&bus, &message, 1U) == SOFT_I2C_OK);
    assert(value == 0x5AU);
    assert(mock.starts == 2U && mock.stops == 1U);
    assert(mock.received_count == 3U);
    assert(mock.received[0] == 0xF4U);
    assert(mock.received[1] == 0xAAU);
    assert(mock.received[2] == 0xF5U);

    setup_bus(&mock, &bus);
    message.flags = SOFT_I2C_MSG_TEN_BIT;
    message.buffer = &write_value;
    message.transferred = 0U;
    assert(soft_i2c_transfer(&bus, &message, 1U) == SOFT_I2C_OK);
    assert(mock.starts == 1U && mock.stops == 1U);
    assert(mock.received_count == 3U);
    assert(mock.received[0] == 0xF4U);
    assert(mock.received[1] == 0xAAU);
    assert(mock.received[2] == 0xC3U);
}

static void test_errors_and_recovery(void)
{
    mock_i2c_t mock;
    soft_i2c_bus_t bus;
    uint8_t value = 0x22U;
    soft_i2c_msg_t message = {0x30U, 0U, &value, 1U, 0U, 0U};

    setup_bus(&mock, &bus);
    mock.nack_at = 1U;
    assert(soft_i2c_transfer(&bus, &message, 1U) == SOFT_I2C_ERR_ADDR_NACK);
    assert(mock.stops == 1U);

    setup_bus(&mock, &bus);
    mock.nack_at = 2U;
    assert(soft_i2c_transfer(&bus, &message, 1U) == SOFT_I2C_ERR_DATA_NACK);

    setup_bus(&mock, &bus);
    mock.stuck_sda = 1U;
    assert(soft_i2c_transfer(&bus, &message, 1U) == SOFT_I2C_ERR_BUS_BUSY);

    setup_bus(&mock, &bus);
    mock.force_arbitration = 1U;
    assert(soft_i2c_transfer(&bus, &message, 1U) == SOFT_I2C_ERR_ARBITRATION);

    setup_bus(&mock, &bus);
    mock.stretch_each = 2U;
    assert(soft_i2c_transfer(&bus, &message, 1U) == SOFT_I2C_OK);

    setup_bus(&mock, &bus);
    mock.stretch_forever = 1U;
    assert(soft_i2c_transfer(&bus, &message, 1U) == SOFT_I2C_ERR_TIMEOUT);

    setup_bus(&mock, &bus);
    mock.stuck_sda = 1U;
    mock.release_after_pulses = 3U;
    assert(soft_i2c_recover(&bus) == SOFT_I2C_OK);
    assert(mock.recovery_pulses == 3U);

    setup_bus(&mock, &bus);
    mock.stuck_sda = 1U;
    assert(soft_i2c_recover(&bus) == SOFT_I2C_ERR_BUS_BUSY);
    assert(mock.recovery_pulses == 9U);
}

static void test_probe_and_smbus_writes(void)
{
    mock_i2c_t mock;
    soft_i2c_bus_t bus;
    uint8_t block[3] = {1U, 2U, 3U};
    uint8_t crc;

    setup_bus(&mock, &bus);
    assert(soft_i2c_probe(&bus, 0x2AU, 0U) == SOFT_I2C_OK);
    assert(mock.received_count == 1U && mock.received[0] == 0x54U);

    setup_bus(&mock, &bus);
    assert(soft_smbus_quick(&bus, 0x09U, 1U) == SOFT_I2C_OK);
    assert(mock.received[0] == 0x13U);

    setup_bus(&mock, &bus);
    assert(soft_smbus_send_byte(&bus, 0x09U, 0x5AU,
                                SOFT_SMBUS_PEC_DISABLED) == SOFT_I2C_OK);
    assert(mock.received[1] == 0x5AU);

    setup_bus(&mock, &bus);
    assert(soft_smbus_write_byte_data(&bus, 0x09U, 0x20U, 0x33U,
                                      SOFT_SMBUS_PEC_DISABLED) == SOFT_I2C_OK);
    assert(mock.received[1] == 0x20U && mock.received[2] == 0x33U);

    setup_bus(&mock, &bus);
    assert(soft_smbus_write_word_data(&bus, 0x09U, 0x12U, 0xABCDU,
                                      SOFT_SMBUS_PEC_ENABLED) == SOFT_I2C_OK);
    assert(mock.received[1] == 0x12U);
    assert(mock.received[2] == 0xCDU && mock.received[3] == 0xABU);
    crc = test_crc8(0U, 0x12U);
    crc = test_crc8(crc, 0x12U);
    crc = test_crc8(crc, 0xCDU);
    crc = test_crc8(crc, 0xABU);
    assert(mock.received[4] == crc);

    setup_bus(&mock, &bus);
    assert(soft_smbus_write_block_data(&bus, 0x09U, 0x30U, block, 3U,
                                       SOFT_SMBUS_PEC_DISABLED) == SOFT_I2C_OK);
    assert(mock.received[1] == 0x30U && mock.received[2] == 3U);
    assert(mock.received[3] == 1U && mock.received[5] == 3U);
    assert(soft_smbus_write_block_data(&bus, 0x09U, 0x30U, block, 0U,
                                       SOFT_SMBUS_PEC_DISABLED) == SOFT_I2C_ERR_PARAM);
}

static void test_smbus_reads_and_pec(void)
{
    mock_i2c_t mock;
    soft_i2c_bus_t bus;
    uint8_t byte = 0U;
    uint16_t word = 0U;
    uint8_t block[4] = {0U};
    size_t length = 0U;
    uint8_t crc;

    setup_bus(&mock, &bus);
    mock.read_data[0] = 0x7EU;
    crc = test_crc8(0U, 0x13U);
    mock.read_data[1] = test_crc8(crc, 0x7EU);
    mock.read_count = 2U;
    assert(soft_smbus_receive_byte(&bus, 0x09U, &byte,
                                   SOFT_SMBUS_PEC_ENABLED) == SOFT_I2C_OK);
    assert(byte == 0x7EU);

    setup_bus(&mock, &bus);
    mock.read_data[0] = 0x44U;
    mock.read_count = 1U;
    assert(soft_smbus_read_byte_data(&bus, 0x09U, 0x11U, &byte,
                                     SOFT_SMBUS_PEC_DISABLED) == SOFT_I2C_OK);
    assert(byte == 0x44U);

    setup_bus(&mock, &bus);
    mock.read_data[0] = 0x34U;
    mock.read_data[1] = 0x12U;
    mock.read_count = 2U;
    assert(soft_smbus_read_word_data(&bus, 0x09U, 0x22U, &word,
                                     SOFT_SMBUS_PEC_DISABLED) == SOFT_I2C_OK);
    assert(word == 0x1234U);

    setup_bus(&mock, &bus);
    mock.read_data[0] = 3U;
    mock.read_data[1] = 0x10U;
    mock.read_data[2] = 0x20U;
    mock.read_data[3] = 0x30U;
    crc = test_crc8(0U, 0x12U);
    crc = test_crc8(crc, 0x33U);
    crc = test_crc8(crc, 0x13U);
    crc = test_crc8(crc, 3U);
    crc = test_crc8(crc, 0x10U);
    crc = test_crc8(crc, 0x20U);
    mock.read_data[4] = test_crc8(crc, 0x30U);
    mock.read_count = 5U;
    assert(soft_smbus_read_block_data(&bus, 0x09U, 0x33U, block,
                                      sizeof(block), &length,
                                      SOFT_SMBUS_PEC_ENABLED) == SOFT_I2C_OK);
    assert(length == 3U && block[0] == 0x10U && block[2] == 0x30U);

    setup_bus(&mock, &bus);
    mock.read_data[0] = 3U;
    mock.read_data[1] = 1U;
    mock.read_data[2] = 2U;
    mock.read_data[3] = 3U;
    mock.read_count = 4U;
    assert(soft_smbus_read_block_data(&bus, 0x09U, 0x33U, block,
                                      2U, &length,
                                      SOFT_SMBUS_PEC_DISABLED) == SOFT_I2C_ERR_BUFFER);
    assert(length == 0U);

    setup_bus(&mock, &bus);
    mock.read_data[0] = 0U;
    mock.read_count = 1U;
    assert(soft_smbus_read_block_data(&bus, 0x09U, 0x33U, block,
                                      sizeof(block), &length,
                                      SOFT_SMBUS_PEC_DISABLED) == SOFT_I2C_ERR_PROTOCOL);

    setup_bus(&mock, &bus);
    mock.read_data[0] = 33U;
    mock.read_count = 1U;
    assert(soft_smbus_read_block_data(&bus, 0x09U, 0x33U, block,
                                      sizeof(block), &length,
                                      SOFT_SMBUS_PEC_DISABLED) == SOFT_I2C_ERR_PROTOCOL);

    setup_bus(&mock, &bus);
    mock.read_data[0] = 0x5AU;
    mock.read_data[1] = 0x00U;
    mock.read_count = 2U;
    assert(soft_smbus_receive_byte(&bus, 0x09U, &byte,
                                   SOFT_SMBUS_PEC_ENABLED) == SOFT_I2C_ERR_PEC);
}

int main(void)
{
    static const uint8_t check[] = "123456789";
    uint8_t crc = 0U;
    size_t index;

    for (index = 0U; index < sizeof(check) - 1U; ++index) {
        crc = test_crc8(crc, check[index]);
    }
    assert(crc == 0xF4U);

    test_init_and_validation();
    test_seven_bit_combined_transfer();
    test_ten_bit_read();
    test_errors_and_recovery();
    test_probe_and_smbus_writes();
    test_smbus_reads_and_pec();
    return 0;
}
