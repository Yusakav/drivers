#include "ina226.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define TEST_ASSERT(cond)                                                       \
    do {                                                                        \
        if (!(cond)) {                                                          \
            printf("ASSERT failed at %s:%d: %s\n", __FILE__, __LINE__, #cond);  \
            return 1;                                                           \
        }                                                                       \
    } while (0)

typedef struct {
    uint16_t regs[256];
    uint8_t expect_addr;
    uint16_t writes;
    uint16_t reads;
} mock_ina226_t;

static int mock_read_reg16(void *user,
                           uint8_t address,
                           uint8_t reg,
                           uint16_t *value)
{
    mock_ina226_t *mock = (mock_ina226_t *)user;

    if ((mock == NULL) || (value == NULL) || (address != mock->expect_addr)) {
        return -1;
    }

    *value = mock->regs[reg];
    mock->reads++;
    return 0;
}

static int mock_write_reg16(void *user,
                            uint8_t address,
                            uint8_t reg,
                            uint16_t value)
{
    mock_ina226_t *mock = (mock_ina226_t *)user;

    if ((mock == NULL) || (address != mock->expect_addr)) {
        return -1;
    }

    if ((reg == INA226_REG_CONFIG) && ((value & 0x8000U) != 0U)) {
        mock->regs[reg] = 0x4127U;
    } else {
        mock->regs[reg] = value;
    }
    mock->writes++;
    return 0;
}

static const ina226_i2c_ops_t mock_ops = {
    .read_reg16 = mock_read_reg16,
    .write_reg16 = mock_write_reg16,
};

static void mock_init(mock_ina226_t *mock, uint8_t address)
{
    memset(mock, 0, sizeof(*mock));
    mock->expect_addr = address;
    mock->regs[INA226_REG_CONFIG] = 0x4127U;
    mock->regs[INA226_REG_MANUFACTURER] = INA226_MANUFACTURER_ID_VALUE;
    mock->regs[INA226_REG_DIE_ID] = INA226_DIE_ID_VALUE;
}

static int nearly_equal(float a, float b, float tolerance)
{
    float diff = a - b;
    if (diff < 0.0f) {
        diff = -diff;
    }
    return diff <= tolerance;
}

static int test_init_and_multi_instance(void)
{
    mock_ina226_t mock_a;
    mock_ina226_t mock_b;
    ina226_t dev_a;
    ina226_t dev_b;

    mock_init(&mock_a, INA226_ADDR_A0_GND_A1_GND);
    mock_init(&mock_b, INA226_ADDR_A0_VS_A1_GND);

    TEST_ASSERT(ina226_init(NULL, &mock_ops, &mock_a,
                            INA226_ADDR_A0_GND_A1_GND) == INA226_RET_NULL);
    TEST_ASSERT(ina226_init(&dev_a, NULL, &mock_a,
                            INA226_ADDR_A0_GND_A1_GND) == INA226_RET_NULL);
    TEST_ASSERT(ina226_init(&dev_a, &mock_ops, &mock_a, 0x50U) ==
                INA226_RET_RANGE);

    mock_a.regs[INA226_REG_MANUFACTURER] = 0x1234U;
    TEST_ASSERT(ina226_init(&dev_a, &mock_ops, &mock_a,
                            INA226_ADDR_A0_GND_A1_GND) == INA226_RET_ID);
    mock_a.regs[INA226_REG_MANUFACTURER] = INA226_MANUFACTURER_ID_VALUE;

    TEST_ASSERT(ina226_init(&dev_a, &mock_ops, &mock_a,
                            INA226_ADDR_A0_GND_A1_GND) == INA226_OK);
    TEST_ASSERT(ina226_init(&dev_b, &mock_ops, &mock_b,
                            INA226_ADDR_A0_VS_A1_GND) == INA226_OK);

    TEST_ASSERT(ina226_configure(&dev_a, INA226_AVG_16, INA226_CT_1100US,
                                 INA226_CT_1100US,
                                 INA226_MODE_SHUNT_BUS_CONT) == INA226_OK);
    TEST_ASSERT(mock_a.regs[INA226_REG_CONFIG] == 0x4527U);
    TEST_ASSERT(mock_b.regs[INA226_REG_CONFIG] == 0x4127U);

    TEST_ASSERT(ina226_reset(&dev_a) == INA226_OK);
    TEST_ASSERT(mock_a.regs[INA226_REG_CONFIG] == 0x4127U);
    TEST_ASSERT(dev_a.config_value == 0x4127U);
    TEST_ASSERT(dev_a.calibrated == 0U);

    return 0;
}

static int test_calibration(void)
{
    mock_ina226_t mock;
    ina226_t dev;
    ina226_calibration_t cal;

    mock_init(&mock, INA226_ADDR_A0_GND_A1_GND);
    TEST_ASSERT(ina226_init(&dev, &mock_ops, &mock,
                            INA226_ADDR_A0_GND_A1_GND) == INA226_OK);

    TEST_ASSERT(ina226_calibrate(&dev, 0.01f, 8.192f, 0.0f, &cal) ==
                INA226_OK);
    TEST_ASSERT(cal.calibration_value == 2048U);
    TEST_ASSERT(mock.regs[INA226_REG_CALIBRATION] == 2048U);
    TEST_ASSERT(nearly_equal(cal.current_lsb_A, 0.00025f, 0.0000001f));
    TEST_ASSERT(nearly_equal(cal.power_lsb_W, 0.00625f, 0.000001f));

    TEST_ASSERT(ina226_calibrate(&dev, 0.002f, 15.0f, 0.001f, &cal) ==
                INA226_OK);
    TEST_ASSERT(cal.calibration_value == 2560U);
    TEST_ASSERT(nearly_equal(cal.current_lsb_A, 0.001f, 0.0000001f));
    TEST_ASSERT(nearly_equal(cal.power_lsb_W, 0.025f, 0.000001f));

    TEST_ASSERT(ina226_calibrate(&dev, 0.0f, 15.0f, 0.0f, &cal) ==
                INA226_RET_PARAM);
    TEST_ASSERT(ina226_calibrate(&dev, 0.000001f, 0.001f, 0.0f, &cal) ==
                INA226_RET_RANGE);

    return 0;
}

static int test_measurements_and_alert(void)
{
    mock_ina226_t mock;
    ina226_t dev;
    ina226_meas_result_t meas;
    ina226_alert_status_t alert;
    uint16_t bus_raw;
    int16_t shunt_raw;
    float bus_v;
    float shunt_v;
    float current_a;
    float power_w;

    mock_init(&mock, INA226_ADDR_A0_GND_A1_GND);
    TEST_ASSERT(ina226_init(&dev, &mock_ops, &mock,
                            INA226_ADDR_A0_GND_A1_GND) == INA226_OK);

    mock.regs[INA226_REG_CURRENT] = 1000U;
    TEST_ASSERT(ina226_read_current(&dev, NULL, &current_a) ==
                INA226_RET_CALIBRATION);
    TEST_ASSERT(ina226_calibrate(&dev, 0.01f, 8.192f, 0.0f, NULL) ==
                INA226_OK);

    mock.regs[INA226_REG_SHUNT_VOLTAGE] = 0xFF9CU;
    mock.regs[INA226_REG_BUS_VOLTAGE] = 9600U;
    mock.regs[INA226_REG_CURRENT] = 1000U;
    mock.regs[INA226_REG_POWER] = 200U;
    mock.regs[INA226_REG_MASK_ENABLE] = 0x001CU;

    TEST_ASSERT(ina226_read_shunt_voltage(&dev, &shunt_raw, &shunt_v) ==
                INA226_OK);
    TEST_ASSERT(shunt_raw == -100);
    TEST_ASSERT(nearly_equal(shunt_v, -0.00025f, 0.0000001f));

    TEST_ASSERT(ina226_read_bus_voltage(&dev, &bus_raw, &bus_v) == INA226_OK);
    TEST_ASSERT(bus_raw == 9600U);
    TEST_ASSERT(nearly_equal(bus_v, 12.0f, 0.0001f));

    TEST_ASSERT(ina226_read_current(&dev, NULL, &current_a) == INA226_OK);
    TEST_ASSERT(nearly_equal(current_a, 0.25f, 0.0001f));

    TEST_ASSERT(ina226_read_power(&dev, NULL, &power_w) == INA226_OK);
    TEST_ASSERT(nearly_equal(power_w, 1.25f, 0.0001f));

    TEST_ASSERT(ina226_read_all(&dev, &meas) == INA226_OK);
    TEST_ASSERT(meas.shunt_raw == -100);
    TEST_ASSERT(meas.bus_raw == 9600U);
    TEST_ASSERT(meas.current_raw == 1000);
    TEST_ASSERT(meas.power_raw == 200U);
    TEST_ASSERT(meas.alert_function_flag == 1U);
    TEST_ASSERT(meas.conversion_ready == 1U);
    TEST_ASSERT(meas.math_overflow == 1U);

    TEST_ASSERT(ina226_configure_alert(&dev, INA226_ALERT_BUS_OVERVOLTAGE,
                                       11200U,
                                       INA226_ALERT_POLARITY_ACTIVE_HIGH,
                                       INA226_ALERT_LATCHED) == INA226_OK);
    TEST_ASSERT(mock.regs[INA226_REG_ALERT_LIMIT] == 11200U);
    TEST_ASSERT(mock.regs[INA226_REG_MASK_ENABLE] ==
                (uint16_t)(INA226_ALERT_BUS_OVERVOLTAGE | 0x0003U));

    TEST_ASSERT(ina226_read_alert(&dev, &alert) == INA226_OK);
    TEST_ASSERT(alert.active_high == 1U);
    TEST_ASSERT(alert.latched == 1U);

    return 0;
}

static int test_device_info_and_dump(void)
{
    mock_ina226_t mock;
    ina226_t dev;
    ina226_device_info_t info;
    uint16_t regs[8];

    mock_init(&mock, INA226_ADDR_A0_GND_A1_GND);
    TEST_ASSERT(ina226_init(&dev, &mock_ops, &mock,
                            INA226_ADDR_A0_GND_A1_GND) == INA226_OK);
    TEST_ASSERT(ina226_read_device_info(&dev, &info) == INA226_OK);
    TEST_ASSERT(info.manufacturer_id == INA226_MANUFACTURER_ID_VALUE);
    TEST_ASSERT(info.die_id == INA226_DIE_ID_VALUE);
    TEST_ASSERT(info.device_id == 0x0226U);
    TEST_ASSERT(info.revision_id == 0U);

    mock.regs[INA226_REG_ALERT_LIMIT] = 0xABCDU;
    TEST_ASSERT(ina226_dump_registers(&dev, regs) == INA226_OK);
    TEST_ASSERT(regs[INA226_REG_CONFIG] == 0x4127U);
    TEST_ASSERT(regs[INA226_REG_ALERT_LIMIT] == 0xABCDU);
    TEST_ASSERT(ina226_dump_registers(&dev, NULL) == INA226_RET_NULL);

    return 0;
}

int main(void)
{
    int ret;

    ret = test_init_and_multi_instance();
    if (ret != 0) {
        return ret;
    }
    ret = test_calibration();
    if (ret != 0) {
        return ret;
    }
    ret = test_measurements_and_alert();
    if (ret != 0) {
        return ret;
    }
    ret = test_device_info_and_dump();
    if (ret != 0) {
        return ret;
    }

    printf("INA226 tests passed\n");
    return 0;
}
