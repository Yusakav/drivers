# Software I2C / SMBus

This directory contains a platform-independent C99 software I2C master and
SMBus convenience layer. It uses no heap allocation, operating-system API, or
standard I/O.

## Electrical contract

`write_scl` and `write_sda` use open-drain semantics: `0` pulls the line low
and `1` releases it. Both lines must have pull-ups and must be readable through
`read_scl` and `read_sda`. Reading SCL enables clock-stretch timeouts; reading
SDA enables bus-busy and arbitration-loss detection.

```c
static void write_scl(void *user, uint8_t release_high);
static void write_sda(void *user, uint8_t release_high);
static uint8_t read_scl(void *user);
static uint8_t read_sda(void *user);
static void delay_us(void *user, uint32_t us);

soft_i2c_bus_t bus;
soft_i2c_config_t config = {
    .user = &gpio_context,
    .write_scl = write_scl,
    .write_sda = write_sda,
    .read_scl = read_scl,
    .read_sda = read_sda,
    .delay_us = delay_us,
    .half_period_us = 5U,
    .scl_timeout_us = 0U, /* default: 25 ms */
};

soft_i2c_ret_t ret = soft_i2c_init(&bus, &config);
```

The caller must serialize access to a bus. The driver does not enter critical
sections and does not automatically recover a failed bus.

## Combined register read

Messages in one call are separated by repeated START and share one final STOP.
Each message carries its own 7- or 10-bit address.

```c
uint8_t reg = 0x10U;
uint8_t value[2];
soft_i2c_msg_t messages[2] = {
    {0x40U, 0U, &reg, 1U, 0U, 0U},
    {0x40U, SOFT_I2C_MSG_READ, value, sizeof(value), 0U, 0U},
};

ret = soft_i2c_transfer(&bus, messages, 2U);
```

Use `SOFT_I2C_MSG_TEN_BIT` for a 10-bit address. `transferred` is reset and
updated by each call. `SOFT_I2C_MSG_RECV_LEN` is intended for protocols such
as SMBus Block Read: the first byte selects the remaining byte count, while
`length` is the buffer capacity and `trailing_bytes` reserves bytes such as
PEC.

## Probe, scan, and recovery

`soft_i2c_probe()` sends only an address phase. Applications can scan their
chosen address range by calling it repeatedly and formatting results outside
the driver. `soft_i2c_recover()` releases SDA, supplies at most nine SCL
pulses, stops early when SDA is released, and then emits STOP.

## SMBus

Include `soft_smbus.h` for Quick Command, Send/Receive Byte, Byte Data, Word
Data, and Block Data helpers. Word Data is little-endian as required by SMBus.
Block length is restricted to 1 through 32 bytes. Pass
`SOFT_SMBUS_PEC_ENABLED` to supported helpers to append or verify CRC-8 PEC.

INA226 uses ordinary I2C with big-endian 16-bit registers and therefore uses
two core messages instead of the SMBus Word Data helpers. BQ25710 examples use
the shared `bq25710_soft_i2c_adapter` and SMBus Word Data at address `0x09`.

## Migration from the old API

- Replace direct `soft_i2c_bus_t` callback initialization with
  `soft_i2c_config_t` plus `soft_i2c_init()`.
- Replace `i2c_transfer()` flags and register parameters with one or more
  `soft_i2c_msg_t` entries.
- Replace `i2c_scan()` with an application loop around `soft_i2c_probe()`.
- Handle the negative `soft_i2c_ret_t` values; address NACK, data NACK,
  timeout, bus busy, arbitration loss, protocol, PEC, and capacity errors are
  distinct.

## Host tests

```sh
cmake -S Bit-Banging -B build-bit-banging
cmake --build build-bit-banging
ctest --test-dir build-bit-banging --output-on-failure
```

The test double reconstructs bus traffic from GPIO edges and covers 7/10-bit
addressing, repeated START, NACKs, clock stretching, arbitration, recovery,
SMBus byte/word/block operations, and PEC.
