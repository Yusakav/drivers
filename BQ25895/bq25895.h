/**
 * @file    bq25895.h
 * @brief   BQ25895 充电管理芯片寄存器定义头文件
 * @details 包含 BQ25895 全部寄存器地址、位域宏定义、枚举类型。
 *          芯片 I2C 从机地址: 0x6A (7-bit).
 *          寄存器范围: 0x00 ~ 0x14 (21 个寄存器).
 * @version 1.0.0
 * @date    2026-06-13
 */

#ifndef __BQ25895_H__
#define __BQ25895_H__

#ifdef __cplusplus
extern "C" {
#endif

/*===========================================================================
 * INCLUDES
 *===========================================================================*/
#include <stdint.h>
#include <stdbool.h>

/*===========================================================================
 * I2C ADDRESS
 *===========================================================================*/
#define BQ25895_I2C_ADDR_7BIT           (0x6AU)   /**< I2C 7-bit slave address */
#define BQ25895_I2C_ADDR_WRITE          (0xD4U)   /**< I2C 8-bit write address */
#define BQ25895_I2C_ADDR_READ           (0xD5U)   /**< I2C 8-bit read address */

/*===========================================================================
 * REGISTER ADDRESS MAP (0x00 ~ 0x14)
 *===========================================================================*/
#define BQ25895_REG00                   0x00U     /**< Input Source Control */
#define BQ25895_REG01                   0x01U     /**< Boost & VINDPM Offset */
#define BQ25895_REG02                   0x02U     /**< ADC / ICO / DPDM Control */
#define BQ25895_REG03                   0x03U     /**< Charge / OTG / SYS Control */
#define BQ25895_REG04                   0x04U     /**< Charge Current & Pump Ctrl */
#define BQ25895_REG05                   0x05U     /**< Precharge & Termination */
#define BQ25895_REG06                   0x06U     /**< Charge Voltage & Threshold */
#define BQ25895_REG07                   0x07U     /**< Timer / Watchdog Control */
#define BQ25895_REG08                   0x08U     /**< IRCOMP / Thermal Regulation */
#define BQ25895_REG09                   0x09U     /**< ICO / BATFET / PumpX Control */
#define BQ25895_REG0A                   0x0AU     /**< Boost Voltage Control */
#define BQ25895_REG0B                   0x0BU     /**< Status Register (Read-Only) */
#define BQ25895_REG0C                   0x0CU     /**< Fault Register (Read-Only) */
#define BQ25895_REG0D                   0x0DU     /**< VINDPM Threshold */
#define BQ25895_REG0E                   0x0EU     /**< ADC Battery Voltage (Read-Only) */
#define BQ25895_REG0F                   0x0FU     /**< ADC System Voltage (Read-Only) */
#define BQ25895_REG10                   0x10U     /**< ADC TS Voltage (Read-Only) */
#define BQ25895_REG11                   0x11U     /**< ADC VBUS Voltage (Read-Only) */
#define BQ25895_REG12                   0x12U     /**< ADC Charge Current (Read-Only) */
#define BQ25895_REG13                   0x13U     /**< DPM Status (Read-Only) */
#define BQ25895_REG14                   0x14U     /**< Part Information / Reset */

#define BQ25895_REG_COUNT               21U       /**< 寄存器总数 */
#define BQ25895_REG_MAX_ADDR            BQ25895_REG14

/*===========================================================================
 * REG00: Input Source Control Register (0x00)
 * Default: 0x08
 *===========================================================================*/
#define BQ25895_REG00_EN_HIZ_POS        7U
#define BQ25895_REG00_EN_HIZ_MSK        (0x01U << 7)
#define BQ25895_REG00_EN_HIZ_DISABLE    0U
#define BQ25895_REG00_EN_HIZ_ENABLE     1U

#define BQ25895_REG00_EN_ILIM_POS       6U
#define BQ25895_REG00_EN_ILIM_MSK       (0x01U << 6)
#define BQ25895_REG00_EN_ILIM_DISABLE   0U
#define BQ25895_REG00_EN_ILIM_ENABLE    1U

#define BQ25895_REG00_IINLIM_POS        0U
#define BQ25895_REG00_IINLIM_MSK        0x3FU
#define BQ25895_REG00_IINLIM_OFFSET     100     /**< Offset: 100 mA */
#define BQ25895_REG00_IINLIM_STEP       50      /**< Step: 50 mA */
#define BQ25895_REG00_IINLIM_MIN        100     /**< 100 mA  (0x00) */
#define BQ25895_REG00_IINLIM_MAX        3250    /**< 3250 mA (0x3F) */

/*===========================================================================
 * REG01: Boost Mode & VINDPM Offset Control Register (0x01)
 * Default: 0x05
 *===========================================================================*/
#define BQ25895_REG01_BHOT_POS          6U
#define BQ25895_REG01_BHOT_MSK          (0x03U << 6)
#define BQ25895_REG01_BHOT_THR0         (0x00U << 6)   /**< VBHOT1: 34.75% of REGN */
#define BQ25895_REG01_BHOT_THR1         (0x01U << 6)   /**< VBHOT0: 37.75% of REGN */
#define BQ25895_REG01_BHOT_THR2         (0x02U << 6)   /**< VBHOT2: 31.25% of REGN */
#define BQ25895_REG01_BHOT_DISABLE      (0x03U << 6)   /**< Disable boost thermal protection */

#define BQ25895_REG01_BCOLD_POS         5U
#define BQ25895_REG01_BCOLD_MSK         (0x01U << 5)
#define BQ25895_REG01_BCOLD_THR0        0U             /**< VBCOLD0: 77% of REGN (default) */
#define BQ25895_REG01_BCOLD_THR1        1U             /**< VBCOLD1: 80% of REGN */

#define BQ25895_REG01_VINDPM_OS_POS     0U
#define BQ25895_REG01_VINDPM_OS_MSK     0x1FU
#define BQ25895_REG01_VINDPM_OS_OFFSET  0       /**< Offset: 0 mV */
#define BQ25895_REG01_VINDPM_OS_STEP    100     /**< Step: 100 mV */
#define BQ25895_REG01_VINDPM_OS_MIN     0
#define BQ25895_REG01_VINDPM_OS_MAX     3100

/*===========================================================================
 * REG02: ADC / ICO / DPDM Control Register (0x02)
 * Default: 0x3D
 *===========================================================================*/
#define BQ25895_REG02_CONV_START_POS    7U
#define BQ25895_REG02_CONV_START_MSK    (0x01U << 7)

#define BQ25895_REG02_CONV_RATE_POS     6U
#define BQ25895_REG02_CONV_RATE_MSK     (0x01U << 6)
#define BQ25895_REG02_CONV_RATE_ONESHOT 0U
#define BQ25895_REG02_CONV_RATE_1S      1U

#define BQ25895_REG02_BOOST_FREQ_POS    5U
#define BQ25895_REG02_BOOST_FREQ_MSK    (0x01U << 5)
#define BQ25895_REG02_BOOST_FREQ_1P5MHZ 0U
#define BQ25895_REG02_BOOST_FREQ_500KHZ 1U

#define BQ25895_REG02_ICO_EN_POS        4U
#define BQ25895_REG02_ICO_EN_MSK        (0x01U << 4)
#define BQ25895_REG02_ICO_EN_DISABLE    0U
#define BQ25895_REG02_ICO_EN_ENABLE     1U

#define BQ25895_REG02_HVDCP_EN_POS      3U
#define BQ25895_REG02_HVDCP_EN_MSK      (0x01U << 3)

#define BQ25895_REG02_MAXC_EN_POS       2U
#define BQ25895_REG02_MAXC_EN_MSK       (0x01U << 2)

#define BQ25895_REG02_FORCE_DPDM_POS    1U
#define BQ25895_REG02_FORCE_DPDM_MSK    (0x01U << 1)

#define BQ25895_REG02_AUTO_DPDM_EN_POS  0U
#define BQ25895_REG02_AUTO_DPDM_EN_MSK  (0x01U << 0)

/*===========================================================================
 * REG03: Charge / OTG / SYS Control Register (0x03)
 * Default: 0x3A
 *===========================================================================*/
#define BQ25895_REG03_BAT_LOADEN_POS    7U
#define BQ25895_REG03_BAT_LOADEN_MSK    (0x01U << 7)

#define BQ25895_REG03_WD_RST_POS        6U
#define BQ25895_REG03_WD_RST_MSK        (0x01U << 6)

#define BQ25895_REG03_OTG_CONFIG_POS    5U
#define BQ25895_REG03_OTG_CONFIG_MSK    (0x01U << 5)
#define BQ25895_REG03_OTG_DISABLE       0U
#define BQ25895_REG03_OTG_ENABLE        1U

#define BQ25895_REG03_CHG_CONFIG_POS    4U
#define BQ25895_REG03_CHG_CONFIG_MSK    (0x01U << 4)
#define BQ25895_REG03_CHG_DISABLE       0U
#define BQ25895_REG03_CHG_ENABLE        1U

#define BQ25895_REG03_SYS_MIN_POS       1U
#define BQ25895_REG03_SYS_MIN_MSK       0x0EU
#define BQ25895_REG03_SYS_MIN_OFFSET    3000    /**< Offset: 3000 mV */
#define BQ25895_REG03_SYS_MIN_STEP      100     /**< Step: 100 mV */

/* SYS_MIN values */
#define BQ25895_SYS_MIN_3V0             0x00U   /**< 001 -> 3.0V, but default 101=3.5V */
#define BQ25895_SYS_MIN_3V1             0x02U
#define BQ25895_SYS_MIN_3V2             0x04U
#define BQ25895_SYS_MIN_3V3             0x06U
#define BQ25895_SYS_MIN_3V4             0x08U
#define BQ25895_SYS_MIN_3V5             0x0AU   /**< Default */
#define BQ25895_SYS_MIN_3V6             0x0CU
#define BQ25895_SYS_MIN_3V7             0x0EU

/*===========================================================================
 * REG04: Charge Current & Pump Control Register (0x04)
 * Default: 0x20
 *===========================================================================*/
#define BQ25895_REG04_EN_PUMPX_POS      7U
#define BQ25895_REG04_EN_PUMPX_MSK      (0x01U << 7)

#define BQ25895_REG04_ICHG_POS          0U
#define BQ25895_REG04_ICHG_MSK          0x7FU
#define BQ25895_REG04_ICHG_OFFSET       0       /**< Offset: 0 mA */
#define BQ25895_REG04_ICHG_STEP         64      /**< Step: 64 mA */
#define BQ25895_REG04_ICHG_MIN          0
#define BQ25895_REG04_ICHG_MAX          5056    /**< 0x4F * 64 = 5056 mA */
#define BQ25895_REG04_ICHG_MAX_CODE     0x4F

/*===========================================================================
 * REG05: Precharge & Termination Control Register (0x05)
 * Default: 0x13
 *===========================================================================*/
#define BQ25895_REG05_IPRECHG_POS       4U
#define BQ25895_REG05_IPRECHG_MSK       0xF0U
#define BQ25895_REG05_IPRECHG_OFFSET    64      /**< Offset: 64 mA */
#define BQ25895_REG05_IPRECHG_STEP      64      /**< Step: 64 mA */
#define BQ25895_REG05_IPRECHG_MIN       64
#define BQ25895_REG05_IPRECHG_MAX       1024

#define BQ25895_REG05_ITERM_POS         0U
#define BQ25895_REG05_ITERM_MSK         0x0FU
#define BQ25895_REG05_ITERM_OFFSET      64      /**< Offset: 64 mA */
#define BQ25895_REG05_ITERM_STEP        64      /**< Step: 64 mA */
#define BQ25895_REG05_ITERM_MIN         64
#define BQ25895_REG05_ITERM_MAX         1024

/*===========================================================================
 * REG06: Charge Voltage & Threshold Control Register (0x06)
 * Default: 0x5E
 *===========================================================================*/
#define BQ25895_REG06_VREG_POS          2U
#define BQ25895_REG06_VREG_MSK          0xFCU
#define BQ25895_REG06_VREG_OFFSET       3840    /**< Offset: 3840 mV */
#define BQ25895_REG06_VREG_STEP         16      /**< Step: 16 mV */
#define BQ25895_REG06_VREG_MIN          3840
#define BQ25895_REG06_VREG_MAX          4608    /**< 0x30 * 16 + 3840 = 4608 mV */

#define BQ25895_REG06_BATLOWV_POS       1U
#define BQ25895_REG06_BATLOWV_MSK       (0x01U << 1)
#define BQ25895_REG06_BATLOWV_2V8       0U
#define BQ25895_REG06_BATLOWV_3V0       1U

#define BQ25895_REG06_VRECHG_POS        0U
#define BQ25895_REG06_VRECHG_MSK        (0x01U << 0)
#define BQ25895_REG06_VRECHG_100MV      0U
#define BQ25895_REG06_VRECHG_200MV      1U

/*===========================================================================
 * REG07: Timer / Watchdog Control Register (0x07)
 * Default: 0x9D
 *===========================================================================*/
#define BQ25895_REG07_EN_TERM_POS       7U
#define BQ25895_REG07_EN_TERM_MSK       (0x01U << 7)
#define BQ25895_REG07_EN_TERM_DISABLE   0U
#define BQ25895_REG07_EN_TERM_ENABLE    1U

#define BQ25895_REG07_STAT_DIS_POS      6U
#define BQ25895_REG07_STAT_DIS_MSK      (0x01U << 6)

#define BQ25895_REG07_WATCHDOG_POS      4U
#define BQ25895_REG07_WATCHDOG_MSK      (0x03U << 4)
#define BQ25895_REG07_WATCHDOG_DISABLE  (0x00U << 4)
#define BQ25895_REG07_WATCHDOG_40S      (0x01U << 4)
#define BQ25895_REG07_WATCHDOG_80S      (0x02U << 4)
#define BQ25895_REG07_WATCHDOG_160S     (0x03U << 4)

#define BQ25895_REG07_EN_TIMER_POS      3U
#define BQ25895_REG07_EN_TIMER_MSK      (0x01U << 3)

#define BQ25895_REG07_CHG_TIMER_POS     1U
#define BQ25895_REG07_CHG_TIMER_MSK     (0x03U << 1)
#define BQ25895_REG07_CHG_TIMER_5HRS    (0x00U << 1)
#define BQ25895_REG07_CHG_TIMER_8HRS    (0x01U << 1)
#define BQ25895_REG07_CHG_TIMER_12HRS   (0x02U << 1)
#define BQ25895_REG07_CHG_TIMER_20HRS   (0x03U << 1)

/*===========================================================================
 * REG08: IRCOMP / Thermal Regulation Control Register (0x08)
 * Default: 0x03
 *===========================================================================*/
#define BQ25895_REG08_BAT_COMP_POS      5U
#define BQ25895_REG08_BAT_COMP_MSK      0xE0U
#define BQ25895_REG08_BAT_COMP_OFFSET   0
#define BQ25895_REG08_BAT_COMP_STEP     20      /**< Step: 20 mOhm */
#define BQ25895_REG08_BAT_COMP_MAX      140

#define BQ25895_REG08_VCLAMP_POS        2U
#define BQ25895_REG08_VCLAMP_MSK        0x1CU
#define BQ25895_REG08_VCLAMP_OFFSET     0
#define BQ25895_REG08_VCLAMP_STEP       32      /**< Step: 32 mV */
#define BQ25895_REG08_VCLAMP_MAX        224

#define BQ25895_REG08_TREG_POS          0U
#define BQ25895_REG08_TREG_MSK          0x03U
#define BQ25895_REG08_TREG_60C          0x00U
#define BQ25895_REG08_TREG_80C          0x01U
#define BQ25895_REG08_TREG_100C         0x02U
#define BQ25895_REG08_TREG_120C         0x03U

/*===========================================================================
 * REG09: ICO / BATFET / PumpX Control Register (0x09)
 * Default: 0x44
 *===========================================================================*/
#define BQ25895_REG09_FORCE_ICO_POS     7U
#define BQ25895_REG09_FORCE_ICO_MSK     (0x01U << 7)

#define BQ25895_REG09_TMR2X_EN_POS      6U
#define BQ25895_REG09_TMR2X_EN_MSK      (0x01U << 6)

#define BQ25895_REG09_BATFET_DIS_POS    5U
#define BQ25895_REG09_BATFET_DIS_MSK    (0x01U << 5)
#define BQ25895_REG09_BATFET_ON         0U
#define BQ25895_REG09_BATFET_OFF        1U

#define BQ25895_REG09_BATFET_DLY_POS    3U
#define BQ25895_REG09_BATFET_DLY_MSK    (0x01U << 3)
#define BQ25895_REG09_BATFET_DLY_IMM    0U
#define BQ25895_REG09_BATFET_DLY_10S    1U

#define BQ25895_REG09_BATFET_RST_EN_POS 2U
#define BQ25895_REG09_BATFET_RST_EN_MSK (0x01U << 2)

#define BQ25895_REG09_PUMPX_UP_POS      1U
#define BQ25895_REG09_PUMPX_UP_MSK      (0x01U << 1)

#define BQ25895_REG09_PUMPX_DN_POS      0U
#define BQ25895_REG09_PUMPX_DN_MSK      (0x01U << 0)

/*===========================================================================
 * REG0A: Boost Voltage Control Register (0x0A)
 * Default: 0x93
 *===========================================================================*/
#define BQ25895_REG0A_BOOSTV_POS        4U
#define BQ25895_REG0A_BOOSTV_MSK        0xF0U
#define BQ25895_REG0A_BOOSTV_OFFSET     4550    /**< Offset: 4550 mV */
#define BQ25895_REG0A_BOOSTV_STEP       64      /**< Step: 64 mV */
#define BQ25895_REG0A_BOOSTV_MIN        4550
#define BQ25895_REG0A_BOOSTV_MAX        5510

/*===========================================================================
 * REG0B: Status Register (Read-Only) (0x0B)
 *===========================================================================*/
#define BQ25895_REG0B_VBUS_STAT_POS     5U
#define BQ25895_REG0B_VBUS_STAT_MSK     0xE0U
#define BQ25895_REG0B_CHRG_STAT_POS     3U
#define BQ25895_REG0B_CHRG_STAT_MSK     0x18U
#define BQ25895_REG0B_PG_STAT_POS       2U
#define BQ25895_REG0B_PG_STAT_MSK       (0x01U << 2)
#define BQ25895_REG0B_SDP_STAT_POS      1U
#define BQ25895_REG0B_SDP_STAT_MSK      (0x01U << 1)
#define BQ25895_REG0B_VSYS_STAT_POS     0U
#define BQ25895_REG0B_VSYS_STAT_MSK     (0x01U << 0)

/*===========================================================================
 * REG0C: Fault Register (Read-Only) (0x0C)
 *===========================================================================*/
#define BQ25895_REG0C_WATCHDOG_FAULT_POS 7U
#define BQ25895_REG0C_WATCHDOG_FAULT_MSK (0x01U << 7)
#define BQ25895_REG0C_BOOST_FAULT_POS   6U
#define BQ25895_REG0C_BOOST_FAULT_MSK   (0x01U << 6)
#define BQ25895_REG0C_CHRG_FAULT_POS    4U
#define BQ25895_REG0C_CHRG_FAULT_MSK    (0x03U << 4)
#define BQ25895_REG0C_BAT_FAULT_POS     3U
#define BQ25895_REG0C_BAT_FAULT_MSK     (0x01U << 3)
#define BQ25895_REG0C_NTC_FAULT_POS     0U
#define BQ25895_REG0C_NTC_FAULT_MSK     0x07U

/*===========================================================================
 * REG0D: VINDPM Threshold Register (0x0D)
 * Default: 0x12
 *===========================================================================*/
#define BQ25895_REG0D_FORCE_VINDPM_POS  7U
#define BQ25895_REG0D_FORCE_VINDPM_MSK  (0x01U << 7)
#define BQ25895_REG0D_FORCE_VINDPM_REL  0U
#define BQ25895_REG0D_FORCE_VINDPM_ABS  1U

#define BQ25895_REG0D_VINDPM_POS        0U
#define BQ25895_REG0D_VINDPM_MSK        0x7FU
#define BQ25895_REG0D_VINDPM_OFFSET     2600    /**< Offset: 2600 mV */
#define BQ25895_REG0D_VINDPM_STEP       100     /**< Step: 100 mV */
#define BQ25895_REG0D_VINDPM_MIN        3900
#define BQ25895_REG0D_VINDPM_MAX        15300

/*===========================================================================
 * REG0E: ADC Battery Voltage Register (Read-Only) (0x0E)
 *===========================================================================*/
#define BQ25895_REG0E_THERM_STAT_POS    7U
#define BQ25895_REG0E_THERM_STAT_MSK    (0x01U << 7)
#define BQ25895_REG0E_BATV_POS          0U
#define BQ25895_REG0E_BATV_MSK          0x7FU
#define BQ25895_REG0E_BATV_OFFSET       2304    /**< Offset: 2304 mV */
#define BQ25895_REG0E_BATV_STEP         20      /**< Step: 20 mV */
#define BQ25895_REG0E_BATV_MAX          4848

/*===========================================================================
 * REG0F: ADC System Voltage Register (Read-Only) (0x0F)
 *===========================================================================*/
#define BQ25895_REG0F_SYSV_POS          0U
#define BQ25895_REG0F_SYSV_MSK          0x7FU
#define BQ25895_REG0F_SYSV_OFFSET       2304    /**< Offset: 2304 mV */
#define BQ25895_REG0F_SYSV_STEP         20      /**< Step: 20 mV */
#define BQ25895_REG0F_SYSV_MAX          4848

/*===========================================================================
 * REG10: ADC TS Voltage Register (Read-Only) (0x10)
 *===========================================================================*/
#define BQ25895_REG10_TSPCT_POS         0U
#define BQ25895_REG10_TSPCT_MSK         0x7FU
#define BQ25895_REG10_TSPCT_OFFSET      2100    /**< Offset: 21.00% -> scaled x100 */
#define BQ25895_REG10_TSPCT_STEP        46      /**< Step: 0.465% -> scaled x100 */
#define BQ25895_REG10_TSPCT_MAX         8000

/*===========================================================================
 * REG11: ADC VBUS Voltage Register (Read-Only) (0x11)
 *===========================================================================*/
#define BQ25895_REG11_VBUS_GD_POS       7U
#define BQ25895_REG11_VBUS_GD_MSK       (0x01U << 7)
#define BQ25895_REG11_VBUSV_POS         0U
#define BQ25895_REG11_VBUSV_MSK         0x7FU
#define BQ25895_REG11_VBUSV_OFFSET      2600    /**< Offset: 2600 mV */
#define BQ25895_REG11_VBUSV_STEP        100     /**< Step: 100 mV */
#define BQ25895_REG11_VBUSV_MAX         15300

/*===========================================================================
 * REG12: ADC Charge Current Register (Read-Only) (0x12)
 *===========================================================================*/
#define BQ25895_REG12_ICHGR_POS         0U
#define BQ25895_REG12_ICHGR_MSK         0x7FU
#define BQ25895_REG12_ICHGR_OFFSET      0       /**< Offset: 0 mA */
#define BQ25895_REG12_ICHGR_STEP        50      /**< Step: 50 mA */
#define BQ25895_REG12_ICHGR_MAX         6350

/*===========================================================================
 * REG13: DPM Status Register (Read-Only) (0x13)
 *===========================================================================*/
#define BQ25895_REG13_VDPM_STAT_POS     7U
#define BQ25895_REG13_VDPM_STAT_MSK     (0x01U << 7)
#define BQ25895_REG13_IDPM_STAT_POS     6U
#define BQ25895_REG13_IDPM_STAT_MSK     (0x01U << 6)
#define BQ25895_REG13_IDPM_LIM_POS      0U
#define BQ25895_REG13_IDPM_LIM_MSK      0x3FU
#define BQ25895_REG13_IDPM_LIM_OFFSET   100
#define BQ25895_REG13_IDPM_LIM_STEP     50
#define BQ25895_REG13_IDPM_LIM_MAX      3250

/*===========================================================================
 * REG14: Part Information / Reset Register (0x14)
 *===========================================================================*/
#define BQ25895_REG14_REG_RST_POS       7U
#define BQ25895_REG14_REG_RST_MSK       (0x01U << 7)

#define BQ25895_REG14_ICO_OPTIMIZED_POS 6U
#define BQ25895_REG14_ICO_OPTIMIZED_MSK (0x01U << 6)

#define BQ25895_REG14_PN_POS            3U
#define BQ25895_REG14_PN_MSK            0x38U
#define BQ25895_REG14_PN_BQ25895        0x38U   /**< 111 = BQ25895 */

#define BQ25895_REG14_TS_PROFILE_POS    2U
#define BQ25895_REG14_TS_PROFILE_MSK    (0x01U << 2)

#define BQ25895_REG14_DEV_REV_POS       0U
#define BQ25895_REG14_DEV_REV_MSK       0x03U

/*===========================================================================
 * ERROR CODES
 *===========================================================================*/
#define BQ25895_OK                      0x00U   /**< 操作成功 */
#define BQ25895_ERR_I2C                 0x01U   /**< I2C 通信错误 */
#define BQ25895_ERR_PARAM               0x02U   /**< 参数无效 */
#define BQ25895_ERR_TIMEOUT             0x03U   /**< 操作超时 */
#define BQ25895_ERR_NOT_INIT            0x04U   /**< 未初始化 */
#define BQ25895_ERR_STATE               0x05U   /**< 状态错误 */
#define BQ25895_ERR_FAULT               0x06U   /**< 故障状态 */
#define BQ25895_ERR_BUSY                0x07U   /**< 设备忙 */

/*===========================================================================
 * ENUMERATIONS
 *===========================================================================*/

/** @brief VBUS 输入源类型 */
typedef enum {
    BQ25895_VBUS_UNKNOWN          = 0,  /**< 未知 / 无输入 */
    BQ25895_VBUS_USB_SDP          = 1,  /**< USB SDP (500mA) */
    BQ25895_VBUS_USB_CDP          = 2,  /**< USB CDP (1.5A) */
    BQ25895_VBUS_USB_DCP          = 3,  /**< USB DCP (3.25A) */
    BQ25895_VBUS_MAX_CHARGE       = 4,  /**< 可调高压适配器 (MaxCharge) 1.5A */
    BQ25895_VBUS_UNKNOWN_ADAPTER  = 5,  /**< 未知适配器 (500mA) */
    BQ25895_VBUS_NON_STD_ADAPTER  = 6,  /**< 非标准适配器 (1A/2A/2.1A/2.4A) */
    BQ25895_VBUS_OTG              = 7,  /**< OTG 升压模式 */
} bq25895_vbus_stat_t;

/** @brief 充电状态 */
typedef enum {
    BQ25895_CHRG_NOT_CHARGING     = 0,  /**< 未充电 */
    BQ25895_CHRG_PRECHARGE        = 1,  /**< 预充电 (VBAT < VBATLOWV) */
    BQ25895_CHRG_FAST_CHARGE      = 2,  /**< 快速充电 (CC / CV) */
    BQ25895_CHRG_TERM_DONE        = 3,  /**< 充电完成 */
} bq25895_chrg_stat_t;

/** @brief 充电故障类型 */
typedef enum {
    BQ25895_CHRG_FAULT_NORMAL     = 0,  /**< 正常 */
    BQ25895_CHRG_FAULT_INPUT      = 1,  /**< 输入故障 (OVP / 欠压) */
    BQ25895_CHRG_FAULT_THERMAL    = 2,  /**< 热关断 */
    BQ25895_CHRG_FAULT_TIMER      = 3,  /**< 充电安全定时器超时 */
} bq25895_chrg_fault_t;

/** @brief NTC 温度故障状态 */
typedef enum {
    BQ25895_NTC_NORMAL            = 0,  /**< 正常 (Buck 模式) */
    BQ25895_NTC_TS_COLD           = 1,  /**< TS 低温报警 (充电阻断) */
    BQ25895_NTC_TS_HOT            = 2,  /**< TS 高温报警 (充电阻断) */
    BQ25895_NTC_BOOST_COLD        = 5,  /**< 升压模式低温 (Boost 模式) */
    BQ25895_NTC_BOOST_HOT         = 6,  /**< 升压模式高温 (Boost 模式) */
} bq25895_ntc_fault_t;

/** @brief 看门狗定时器配置 */
typedef enum {
    BQ25895_WDT_DISABLE           = 0,  /**< 禁能看门狗 */
    BQ25895_WDT_40S               = 1,  /**< 40 秒 */
    BQ25895_WDT_80S               = 2,  /**< 80 秒 */
    BQ25895_WDT_160S              = 3,  /**< 160 秒 */
} bq25895_watchdog_t;

/** @brief 充电安全定时器 */
typedef enum {
    BQ25895_CHG_TIMER_5HRS        = 0,  /**< 5 小时 */
    BQ25895_CHG_TIMER_8HRS        = 1,  /**< 8 小时 */
    BQ25895_CHG_TIMER_12HRS       = 2,  /**< 12 小时 (默认) */
    BQ25895_CHG_TIMER_20HRS       = 3,  /**< 20 小时 */
} bq25895_chg_timer_t;

/** @brief 热调节阈值 */
typedef enum {
    BQ25895_TREG_60C              = 0,  /**< 60 °C */
    BQ25895_TREG_80C              = 1,  /**< 80 °C */
    BQ25895_TREG_100C             = 2,  /**< 100 °C */
    BQ25895_TREG_120C             = 3,  /**< 120 °C (默认) */
} bq25895_treg_t;

/** @brief 升压模式热保护阈值 */
typedef enum {
    BQ25895_BHOT_VBHOT1           = 0,  /**< VBHOT1: 34.75% of REGN (默认) */
    BQ25895_BHOT_VBHOT0           = 1,  /**< VBHOT0: 37.75% of REGN */
    BQ25895_BHOT_VBHOT2           = 2,  /**< VBHOT2: 31.25% of REGN (65°C w/ 103AT) */
    BQ25895_BHOT_DISABLE          = 3,  /**< 禁能升压温度保护 */
} bq25895_bhot_t;

/** @brief 升压模式低温阈值 */
typedef enum {
    BQ25895_BCOLD_VBCOLD0         = 0,  /**< VBCOLD0: 77% of REGN (-10°C w/ 103AT) (默认) */
    BQ25895_BCOLD_VBCOLD1         = 1,  /**< VBCOLD1: 80% of REGN (-20°C w/ 103AT) */
} bq25895_bcold_t;

/** @brief SYS_MIN 系统最低电压 */
typedef enum {
    BQ25895_SYS_MIN_3V0           = 3000,
    BQ25895_SYS_MIN_3V1           = 3100,
    BQ25895_SYS_MIN_3V2           = 3200,
    BQ25895_SYS_MIN_3V3           = 3300,
    BQ25895_SYS_MIN_3V4           = 3400,
    BQ25895_SYS_MIN_3V5           = 3500,
    BQ25895_SYS_MIN_3V6           = 3600,
    BQ25895_SYS_MIN_3V7           = 3700,
} bq25895_sys_min_t;

/** @brief ADC 转换模式 */
typedef enum {
    BQ25895_ADC_ONESHOT           = 0,  /**< 单次转换 */
    BQ25895_ADC_CONT_1S           = 1,  /**< 1 秒连续转换 */
} bq25895_adc_mode_t;

/** @brief 升压频率 */
typedef enum {
    BQ25895_BOOST_FREQ_1P5MHZ     = 0,  /**< 1.5 MHz */
    BQ25895_BOOST_FREQ_500KHZ     = 1,  /**< 500 kHz */
} bq25895_boost_freq_t;

/** @brief 电池低电压充电阈值 */
typedef enum {
    BQ25895_BATLOWV_2V8           = 0,  /**< 2.8 V */
    BQ25895_BATLOWV_3V0           = 1,  /**< 3.0 V */
} bq25895_batlowv_t;

/** @brief 再充电阈值 */
typedef enum {
    BQ25895_VRECHG_100MV          = 0,  /**< 低于充电电压 100 mV */
    BQ25895_VRECHG_200MV          = 1,  /**< 低于充电电压 200 mV */
} bq25895_vrechg_t;

/** @brief BATFET 关断延时 */
typedef enum {
    BQ25895_BATFET_DLY_IMMEDIATE  = 0,  /**< 立即关断 */
    BQ25895_BATFET_DLY_10S        = 1,  /**< 延时 ~10s 关断 (进入运输模式) */
} bq25895_batfet_dly_t;

/*===========================================================================
 * DATA STRUCTURES
 *===========================================================================*/

/** @brief ADC 转换结果结构体 */
typedef struct {
    bool      therm_stat;         /**< 热调节状态: true = 正在热调节 */
    uint16_t  vbat_mv;            /**< 电池电压 (mV). 范围: 2304 ~ 4848 */
    uint16_t  vsys_mv;            /**< 系统电压 (mV). 范围: 2304 ~ 4848 */
    uint16_t  ts_pct;             /**< TS 电压百分比 x100 (例如 4250 = 42.50% of REGN) */
    uint16_t  vbus_mv;            /**< VBUS 电压 (mV). 范围: 2600 ~ 15300 */
    bool      vbus_good;          /**< VBUS 状态: true = 已连接有效输入 */
    uint16_t  ichg_ma;            /**< 充电电流 (mA). 范围: 0 ~ 6350 */
} bq25895_adc_result_t;

/** @brief 完整状态结构体 */
typedef struct {
    bq25895_vbus_stat_t   vbus_stat;    /**< VBUS 输入源类型 */
    bq25895_chrg_stat_t   chrg_stat;    /**< 充电状态 */
    bool                  pg_stat;      /**< 电源正常标志 */
    bool                  sdp_stat;     /**< USB500 输入标志 */
    bool                  vsys_stat;    /**< SYS_MIN 调节标志 */
    bool                  vdpm_stat;    /**< VINDPM 调节标志 */
    bool                  idpm_stat;    /**< IINDPM 调节标志 */
    uint16_t              idpm_lim_ma;  /**< ICO 优化后的输入限流 (mA) */
} bq25895_status_t;

/** @brief 故障状态结构体 */
typedef struct {
    bool                  watchdog_fault;   /**< 看门狗超时标志 */
    bool                  boost_fault;      /**< 升压模式故障标志 */
    bq25895_chrg_fault_t  chrg_fault;       /**< 充电故障类型 */
    bool                  bat_fault;        /**< 电池过压故障 */
    bq25895_ntc_fault_t   ntc_fault;        /**< NTC 温度故障 */
} bq25895_fault_t;

/** @brief 器件信息结构体 */
typedef struct {
    uint8_t  part_number;      /**< 器件型号: 7 = BQ25895 */
    bool     ts_profile;       /**< 温度曲线: 0 = 冷/热, 1 = ... */
    uint8_t  dev_revision;     /**< 器件版本 */
    bool     ico_optimized;    /**< ICO 优化完成标志 */
} bq25895_device_info_t;

#ifdef __cplusplus
}
#endif

#endif /* __BQ25895_H__ */
