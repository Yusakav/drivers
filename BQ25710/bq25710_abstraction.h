/**
 * @file bq25710_abstraction.h
 * @author nyarukov (luckychaoyue1@gmail.com)
 * @brief 
 * @version 0.1
 * @date 2026-03-06
 * 
 * @copyright Copyright (c) 2026
 * 
 */

#ifndef BQ25710_ABSTRACTION_H
#define BQ25710_ABSTRACTION_H

#include <stdint.h>

// clang-format off


#define BQ25710_VERSION                     "1.0.0"
#define BQ25710_I2C_TIMEOUT_MS              100        // I2C 通信超时时间
#define BQ25710_DEFAULT_ADDR                0x09       // I2C 地址
#define BQ25710_DEVICE_ID_VAL               0x0089     // 器件 ID 固定值
#define BQ25710_MFR_ID_VAL                  0x0040     // 厂商 ID (TI)

#define BQ25710_CHG_CURR_MIN                0          // 最小充电电流 (mA)
#define BQ25710_CHG_CURR_MAX                8128       // 最大充电电流 (mA)
#define BQ25710_CHG_CURR_STEP               64         // 充电电流步进 (mA)

#define BQ25710_CHG_VOLT_MIN                0          // 最小充电电压 (mV)
#define BQ25710_CHG_VOLT_MAX                22392      // 最大充电电压 (mV)
#define BQ25710_CHG_VOLT_STEP               8          // 充电电压步进 (mV)

#define BQ25710_IN_CURR_MIN                 0          // 最小输入电流 (mA)
#define BQ25710_IN_CURR_MAX                 6350       // 最大输入电流 (mA)
#define BQ25710_IN_CURR_STEP                50         // 输入电流步进 (mA)

#define BQ25710_VINDPM_MIN                  3200       // 最小 VINDPM (mV)
#define BQ25710_VINDPM_MAX                  19520      // 最大 VINDPM (mV)
#define BQ25710_VINDPM_STEP                 64         // VINDPM 步进 (mV)

#define BQ25710_MINSYS_MIN                  0          // 最小系统电压 (mV)
#define BQ25710_MINSYS_MAX                  16128      // 最大系统电压 (mV)
#define BQ25710_MINSYS_STEP                 256        // 系统电压步进 (mV)

#define BQ25710_OTG_VOLT_LOW_MIN            3000       // OTG 低量程最小 (mV)
#define BQ25710_OTG_VOLT_LOW_MAX            19520      // OTG 低量程最大 (mV)

#define BQ25710_OTG_VOLT_HIGH_MIN           4280       // OTG 高量程最小 (mV)
#define BQ25710_OTG_VOLT_HIGH_MAX           20800      // OTG 高量程最大 (mV)

#define BQ25710_OTG_CURR_MIN                0          // OTG 最小电流 (mA)
#define BQ25710_OTG_CURR_MAX                6350       // OTG 最大电流 (mA)
#define BQ25710_OTG_CURR_STEP               50         // OTG 电流步进 (mA)


/**
 * @brief BQ25710 寄存器地址宏定义 (SMBus 通讯地址)
 */
/* 配置与选项寄存器 */
#define BQ25710_REG_CHARGE_OPTION_0    0x12  // 充电控制0：包含看门狗、PWM频率、低功耗模式等
#define BQ25710_REG_CHARGE_OPTION_1    0x30  // 充电控制1：包含检测电阻阻值设置、探针增益、运输模式
#define BQ25710_REG_CHARGE_OPTION_2    0x31  // 充电控制2：包含过流保护阈值(ACOC/BATOC)、峰值功率模式
#define BQ25710_REG_CHARGE_OPTION_3    0x32  // 充电控制3：包含OTG使能、ICO使能、软件复位功能
#define BQ25710_REG_PROCHOT_OPTION_0   0x33  // PROCHOT控制0：VSYS下冲阈值、电流限制保护延迟
#define BQ25710_REG_PROCHOT_OPTION_1   0x34  // PROCHOT控制1：PROCHOT引脚的各种触发源配置
#define BQ25710_REG_ADC_OPTION         0x35  // ADC控制：ADC通道使能、采样频率、单次/连续模式转换

/* 限值与设定寄存器 */
#define BQ25710_REG_CHARGE_CURRENT     0x14  // 充电电流设定：设置电池端的最大充电电流 (ICHG)
#define BQ25710_REG_MAX_CHARGE_VOLT    0x15  // 最大充电电压：设置电池端的恒压充电阈值 (VREG)
#define BQ25710_REG_INPUT_VOLTAGE      0x3D  // 输入电压设定：即VINDPM阈值，防止输入源被拉死
#define BQ25710_REG_MIN_SYS_VOLTAGE    0x3E  // 最小系统电压：当电池欠压时维持系统母线的最低电压
#define BQ25710_REG_IN_CURR_HOST       0x3F  // 输入电流设定：主控设定的适配器最大电流限制 (InCurrDpm)

/* OTG (反向输出) 寄存器 */
#define BQ25710_REG_OTG_VOLTAGE        0x3B  // OTG 输出电压：设置反向从 VBUS 输出的电压
#define BQ25710_REG_OTG_CURRENT        0x3C  // OTG 输出电流：设置反向输出的限流值

/* 状态监测寄存器 (只读) */
#define BQ25710_REG_CHARGER_STATUS     0x20  // 充电器状态：反映AC接入、DPM调节、故障锁存状态
#define BQ25710_REG_PROCHOT_STATUS     0x21  // PROCHOT状态：反映是哪个具体事件触发了PROCHOT报警
#define BQ25710_REG_IN_CURR_DPM        0x22  // InCurrDpm_DPM状态：反映当前ICO或DPM环路实际应用的输入电流限制

/* ADC 采样结果寄存器 (只读) */
#define BQ25710_REG_ADC_VBUS_PSYS      0x23  // ADC-VBUS/PSYS：输入电压与系统总功率采样
#define BQ25710_REG_ADC_IBAT           0x24  // ADC-IBAT：电池端的实时充/放电电流采样
#define BQ25710_REG_ADC_IIN_CMPIN      0x25  // ADC-IIN/CMPIN：适配器输入电流与独立比较器电压采样
#define BQ25710_REG_ADC_VSYS_VBAT      0x26  // ADC-VSYS/VBAT：系统母线电压与电池端电压采样

/* 身份识别寄存器 */
#define BQ25710_REG_MANUFACTURE_ID     0xFE  // 厂商 ID：固定为 0x0040 (TI)
#define BQ25710_REG_DEVICE_ID          0xFF  // 器件 ID：固定为 0x0089 (BQ25710)


/**
 * @brief BQ25710 ChargeOption0 寄存器联合体 (SMBus 地址 12h)
 */
typedef union {
    uint16_t all;                           // 16位原始数值
    uint8_t low_byte;                       // 低4位原始数值
    uint8_t high_byte;                      // 高4位原始数值
    struct {
        // --- 低 8 位 (Low Byte) ---
        uint16_t CHRG_INHIBIT      : 1;     // [0]   充电抑制：0b-启用充电，1b-禁止充电；默认值 0b
        uint16_t EN_IDPM           : 1;     // [1]   IDPM 启用：0b-禁用，1b-启用调节环路；默认值 1b
        uint16_t EN_LDO            : 1;     // [2]   LDO 模式启用：0b-禁用，1b-启用(预充时维持系统最小电压)；默认值 1b
        uint16_t IBAT_GAIN         : 1;     // [3]   IBAT 放大器倍率：0b-16x(默认)，1b-8x；默认值 1b
        uint16_t IADPT_GAIN        : 1;     // [4]   IADPT 放大器倍率：0b-20x，1b-40x；默认值 0b
        uint16_t EN_LEARN          : 1;     // [5]   学习模式：0b-禁用，1b-启用(电池放电校准模式)；默认值 0b
        uint16_t SYS_SHORT_DISABLE : 1;     // [6]   系统短路保护禁用：0b-进入断续模式，1b-禁用断续模式；默认值 0b
        uint16_t RESERVED_7        : 1;     // [7]   保留位；默认值 0b

        // --- 高 8 位 (High Byte) ---
        uint16_t LOW_PTM_RIPPLE    : 1;     // [8]   PTM 模式纹波降低：0b-禁用，1b-启用；默认值 1b
        uint16_t PWM_FREQ          : 1;     // [9]   开关频率：0b-1200kHz，1b-800kHz；默认值 1b
        uint16_t EN_OOA            : 1;     // [10]  音频噪声抑制 (Out-of-Audio)：0b-不限制 PFM，1b-频率限制在 >25kHz；默认值 1b
        uint16_t OTG_ON_CHRGOK     : 1;     // [11]  OTG 模式 CHRG_OK 控制：0b-禁用，1b-开启时驱动 CHRG_OK 为高；默认值 0b
        uint16_t IDPM_AUTO_DISABLE : 1;     // [12]  IDPM 自动禁用：0b-禁用功能，1b-电池移除时自动禁用 IDPM；默认值 0b
        uint16_t WDTMR_ADJ         : 2;     // [14-13] 看门狗计时器调节：00b-禁用，01b-5s，10b-88s，11b-175s；默认值 11b
        uint16_t EN_LWPWR          : 1;     // [15]  低功耗模式启用：0b-性能模式(ADC可用)，1b-低功耗模式(ADC关闭)；默认值 1b
    } maps;
} BQ25710_ChargeOption0_t;

/**
 * @brief BQ25710 ChargeOption1 寄存器联合体 (SMBus 地址 30h)
 */
typedef union {
    uint16_t all;                           // 16位原始数值
    uint8_t low_byte;                       // 低4位原始数值
    uint8_t high_byte;                      // 高4位原始数值
    struct {
        // --- 低 8 位 (Low Byte) ---
        uint16_t AUTO_WAKEUP_EN    : 1;     // [0]   自动唤醒启用：0b-禁用，1b-启用(电池低于最小电压时自动128mA预充)；默认值 1b
        uint16_t EN_SHIP_DCHG      : 1;     // [1]   运输模式放电：0b-禁用，1b-启用(SRN引脚140ms内放电至3.8V以下)；默认值 0b
        uint16_t EN_PTM            : 1;     // [2]   PTM 模式启用：0b-禁用，1b-启用；默认值 0b
        uint16_t FORCE_LATCHOFF    : 1;     // [3]   强制电源路径关闭：0b-禁用，1b-启用(独立比较器触发时断开Q1/Q4)；默认值 0b
        uint16_t CMP_DEG           : 2;     // [5-4] 独立比较器抗尖峰脉冲时间：00b-禁用，01b-1us，10b-2ms，11b-5s；默认值 01b
        uint16_t CMP_POL           : 1;     // [6]   独立比较器输出极性：0b-高于阈值输出低，1b-低于阈值输出低；默认值 0b
        uint16_t CMP_REF           : 1;     // [7]   独立比较器内部基准：0b-2.3V，1b-1.2V；默认值 0b

        // --- 高 8 位 (High Byte) ---
        uint16_t PTM_PINSEL        : 1;     // [8]   ILIM_HIZ 引脚功能选择：0b-拉低进入高阻模式，1b-拉低进入PTM；默认值 0b
        uint16_t PSYS_RATIO        : 1;     // [9]   PSYS 增益比率：0b-0.25uA/W，1b-1uA/W；默认值 1b
        uint16_t RSNS_RSR          : 1;     // [10]  充电检测电阻 RSR 阻值：0b-10mOhm，1b-20mOhm；默认值 0b
        uint16_t RSNS_RAC          : 1;     // [11]  输入检测电阻 RAC 阻值：0b-10mOhm，1b-20mOhm；默认值 0b
        uint16_t EN_PSYS           : 1;     // [12]  PSYS 启用：0b-关闭以减小Iq，1b-开启PSYS检测及缓冲器；默认值 0b
        uint16_t EN_PROCHOT_LPWR   : 2;     // [14-13] 电池模式低功耗 PROCHOT 启用：00b-禁用，01b-启用VSYS低功耗检测；默认值 00b
        uint16_t EN_IBAT           : 1;     // [15]  IBAT 启用：0b-关闭以减小Iq，1b-开启输出缓冲器；默认值 0b
    } maps;
} BQ25710_ChargeOption1_t;

/**
 * @brief BQ25710 ChargeOption2 寄存器联合体 (SMBus 地址 31h)
 */
typedef union {
    uint16_t all;                           // 16位原始数值
    uint8_t low_byte;                       // 低4位原始数值
    uint8_t high_byte;                      // 高4位原始数值
    struct {
        // --- 低 8 位 (Low Byte) ---
        uint16_t BATOC_VTH         : 1;     // [0]   电池放电过流阈值：0b-133% PROCHOT IDCHG，1b-200% PROCHOT IDCHG；默认值 1b
        uint16_t EN_BATOC          : 1;     // [1]   BATOC 启用：0b-禁用，1b-启用电池放电过流保护；默认值 1b
        uint16_t ACOC_VTH          : 1;     // [2]   ACOC 限制：0b-133% ILIM2，1b-200% ILIM2；默认值 1b
        uint16_t EN_ACOC           : 1;     // [3]   ACOC 启用：0b-禁用，1b-启用输入过流保护(133%或200% ILIM2)；默认值 0b
        uint16_t ACX_OCP           : 1;     // [4]   输入电流 OCP 阈值：0b-280mV，1b-150mV (检测 ACP-ACN)；默认值 1b
        uint16_t Q2_OCP            : 1;     // [5]   Q2 OCP 阈值：0b-210mV，1b-150mV (检测 Q2 VDS)；默认值 1b
        uint16_t EN_ICHG_IDCHG     : 1;     // [6]   IBAT 引脚功能：0b-作为放电电流，1b-作为充电电流；默认值 0b
        uint16_t EN_EXTILIM        : 1;     // [7]   外部输入电流限制：0b-由寄存器 0x3F 设置，1b-由引脚和寄存器较小值设置；默认值 1b

        // --- 高 8 位 (High Byte) ---
        uint16_t PKPWR_TMAX        : 2;     // [9-8]  峰值功率模式过载/弛豫时间：00b-5ms，01b-10ms，10b-20ms，11b-40ms；默认值 10b
        uint16_t PKPWR_RELAX_STAT  : 1;     // [10]  弛豫周期指示：0b-未处于弛豫周期，1b-处于弛豫模式(写0退出)；默认值 0b
        uint16_t PKPWR_OVLD_STAT   : 1;     // [11]  过载周期指示：0b-未处于过载周期，1b-处于峰值功率模式(写0退出)；默认值 0b
        uint16_t EN_PKPWR_VSYS     : 1;     // [12]  VSYS 下冲触发峰值模式：0b-禁用，1b-启用；默认值 0b
        uint16_t EN_PKPWR_IDPM     : 1;     // [13]  电流过冲触发峰值模式：0b-禁用，1b-启用；默认值 0b
        uint16_t PKPWR_TOVLD_DEG   : 2;     // [15-14] 峰值模式输入过载时间：00b-1ms，01b-2ms，10b-10ms，11b-20ms；默认值 00b
    } maps;
} BQ25710_ChargeOption2_t;


/**
 * @brief BQ25710 ChargeOption3 寄存器联合体 (SMBus 地址 32h)
 */
typedef union {
    uint16_t all;                           // 16位原始数值
    uint8_t low_byte;                       // 低4位原始数值
    uint8_t high_byte;                      // 高4位原始数值
    struct {
        // --- 低 8 位 (Low Byte) ---
        uint16_t PSYS_OTG_IDCHG    : 1;     // [0]   OTG 期间 PSYS 功能：0b-电池放电功率减去 OTG 功率，1b-仅用作电池放电功率；默认值 0b
        uint16_t BATFETOFF_HIZ     : 1;     // [1]   高阻态期间控制 BATFET：0b-开启，1b-关闭；默认值 0b
        uint16_t OTG_RANGE_LOW     : 1;     // [2]   OTG 输出电压范围选择：0b-高范围(4.28V-20.8V)，1b-低范围(3V-19.52V)；默认值 0b
        uint16_t IL_AVG            : 2;     // [4-3] 电感平均电流钳位：00b-6A，01b-10A，10b-15A，11b-禁用；默认值 10b
        uint16_t OTG_VAP_MODE      : 1;     // [5]   外部引脚功能选择：0b-OTG/VAP引脚控制VAP模式，1b-OTG/VAP引脚控制OTG模式；默认值 1b
        uint16_t EN_CON_VAP        : 1;     // [6]   启用保守 VAP 模式：0b-禁用，1b-启用；默认值 0b
        uint16_t RESERVED_7        : 1;     // [7]   保留位；默认值 0b

        // --- 高 8 位 (High Byte) ---
        uint16_t RESERVED_10_8     : 3;     // [10-8] 保留位；默认值 000b
        uint16_t EN_ICO_MODE       : 1;     // [11]  ICO 算法启用：0b-禁用，1b-启用(输入电流优化)；默认值 0b
        uint16_t EN_OTG            : 1;     // [12]  OTG 模式启用：0b-禁用，1b-启用(从电池为 VBUS 供电)；默认值 0b
        uint16_t RESET_VINDPM      : 1;     // [13]  复位 VINDPM 阈值：0b-空闲，1b-触发 VINDPM 测量(完成后自动回0)；默认值 0b
        uint16_t RESET_REG         : 1;     // [14]  复位寄存器：0b-空闲，1b-复位所有寄存器(除VINDPM外)为默认值；默认值 0b
        uint16_t EN_HIZ            : 1;     // [15]  器件高阻态模式启用：0b-禁用，1b-启用(最低静态电流)；默认值 0b
    } maps;
} BQ25710_ChargeOption3_t;

/**
 * @brief BQ25710 ProchotOption0 寄存器联合体 (SMBus 地址 33h)
 */
typedef union {
    uint16_t all;                           // 16位原始数值
    uint8_t low_byte;                       // 低4位原始数值
    uint8_t high_byte;                      // 高4位原始数值
    struct {
        // --- 低 8 位 (Low Byte) ---
        uint16_t LOWER_PROCHOT_VDPM : 1;    // [0]   启用VDPM阈值下限：0b-遵循VinDPM设置，1b-遵循位[8]设置；默认值 0b
        uint16_t INOM_DEG           : 1;    // [1]   INOM 抗尖峰脉冲时间：0b-1ms，1b-50ms (INOM阈值比IDPM高10%)；默认值 0b
        uint16_t VSYS_TH2           : 2;    // [3-2] PROCHOT_VSYS 有效阈值：2-4S(00-5.9V, 01-6.2V, 10-6.5V, 11-6.8V)；默认值 01b
        uint16_t VSYS_TH1           : 4;    // [7-4] VAP模式下VBUS放电VSYS阈值：2-4S(0000-5.9V 至 1111-7.4V，阶跃0.1V)；默认值 0110b

        // --- 高 8 位 (High Byte) ---
        uint16_t PROCHOT_VDPM_80_90 : 1;    // [8]   PROCHOT_VDPM 比较器阈值下限：0b-VinDPM的80%，1b-VinDPM的90%；默认值 0b
        uint16_t ICRIT_DEG          : 2;    // [10-9] ICRIT 抗尖峰脉冲时间：00b-15us, 01b-120us, 10b-500us, 11b-1ms；默认值 01b
        uint16_t ILIM2_VTH          : 5;    // [15-11] ILIM2 阈值(IDPM百分比)：00001b~11001b(110%~230%)，11010b~11110b(250%~450%)；默认值 01001b(150%)
    } maps;
} BQ25710_ProchotOption0_t;


/**
 * @brief BQ25710 ProchotOption1 寄存器联合体 (SMBus 地址 34h)
 */
typedef union {
    uint16_t all;                           // 16位原始数值
    uint8_t low_byte;                       // 低4位原始数值
    uint8_t high_byte;                      // 高4位原始数值
    struct {
        // --- 低 8 位 (Low Byte) ---
        uint16_t PP_ACOK           : 1;     // [0]   ACOK 触发：0b-禁用，1b-移除适配器后触发 PROCHOT 脉冲；默认值 0b
        uint16_t PP_BATPRES        : 1;     // [1]   电池存在触发：0b-禁用，1b-取出电池后触发单稳态脉冲；默认值 0b
        uint16_t PP_VSYS           : 1;     // [2]   VSYS 阈值触发：0b-禁用，1b-启用 VSYS 下冲触发 PROCHOT；默认值 0b
        uint16_t PP_IDCHG          : 1;     // [3]   放电过流触发：0b-禁用，1b-启用放电电流超过 IDCHG 阈值触发；默认值 0b
        uint16_t PP_INOM           : 1;     // [4]   INOM 触发：0b-禁用，1b-启用输入电流超过 INOM 阈值触发；默认值 0b
        uint16_t PP_ICRIT          : 1;     // [5]   ICRIT 触发：0b-禁用，1b-启用输入电流超过 ICRIT 阈值触发；默认值 1b
        uint16_t PP_COMP           : 1;     // [6]   独立比较器触发：0b-禁用，1b-启用比较器触发 PROCHOT；默认值 0b
        uint16_t PP_VDPM           : 1;     // [7]   VBUS 电压检测触发：0b-禁用，1b-启用 VBUS 跌至 VDPM 阈值触发；默认值 1b

        // --- 高 8 位 (High Byte) ---
        uint16_t IDCHG_DEG         : 2;     // [9-8]  IDCHG 抗尖峰脉冲时间：00b-2ms, 01b-130us, 10b-8ms, 11b-16ms；默认值 01b
        uint16_t IDCHG_VTH         : 6;     // [15-10] IDCHG 阈值：范围 0~32256mA，阶跃 512mA，偏移量 128mA；默认值 100000b (16384mA)
    } maps;
} BQ25710_ProchotOption1_t;


/**
 * @brief BQ25710 ADCOption 寄存器联合体 (SMBus 地址 35h)
 */
typedef union {
    uint16_t all;                           // 16位原始数值
    uint8_t low_byte;                       // 低4位原始数值
    uint8_t high_byte;                      // 高4位原始数值
    struct {
        // --- 低 8 位 (Low Byte) --- ADC 通道使能控制
        uint16_t EN_ADC_VBAT       : 1;     // [0]   启用 VBAT 转换：0b-禁用，1b-启用；默认值 0b
        uint16_t EN_ADC_VSYS       : 1;     // [1]   启用 VSYS 转换：0b-禁用，1b-启用；默认值 0b
        uint16_t EN_ADC_ICHG       : 1;     // [2]   启用 ICHG 转换：0b-禁用，1b-启用；默认值 0b
        uint16_t EN_ADC_IDCHG      : 1;     // [3]   启用 IDCHG 转换：0b-禁用，1b-启用；默认值 0b
        uint16_t EN_ADC_IIN        : 1;     // [4]   启用 IIN 转换：0b-禁用，1b-启用；默认值 0b
        uint16_t EN_ADC_PSYS       : 1;     // [5]   启用 PSYS 转换：0b-禁用，1b-启用；默认值 0b
        uint16_t EN_ADC_VBUS       : 1;     // [6]   启用 VBUS 转换：0b-禁用，1b-启用；默认值 0b
        uint16_t EN_ADC_CMPIN      : 1;     // [7]   启用 CMPIN 转换：0b-禁用，1b-启用；默认值 0b

        // --- 高 8 位 (High Byte) --- ADC 转换模式控制
        uint16_t RESERVED_12_8     : 5;     // [12-8] 保留位；默认值 00000b
        uint16_t ADC_FULLSCALE     : 1;     // [13]  ADC 输入电压范围：0b-2.04V，1b-3.06V(1S建议0b, 其他建议1b)；默认值 1b
        uint16_t ADC_START         : 1;     // [14]  开始 ADC 转换：0b-无转换，1b-开始转换(单次模式完成后自动回0)；默认值 0b
        uint16_t ADC_CONV          : 1;     // [15]  ADC 转换模式：0b-单次更新(需START手动触发)，1b-持续更新(每1秒周期更新)；默认值 0b
    } maps;
} BQ25710_ADCOption_t;

/**
 * @brief BQ25710 ChargerStatus 寄存器联合体 (SMBus 地址 20h)
 * 注意：此寄存器大部分位为只读(R)，用于反映芯片实时状态和故障锁存。
 */
typedef union {
    uint16_t all;                           // 16位原始数值
    uint8_t low_byte;                       // 低4位原始数值
    uint8_t high_byte;                      // 高4位原始数值
    struct {
        // --- 低 8 位 (Low Byte) --- 故障监测位 (大部分为读后自动清除)
        uint16_t FAULT_OTG_UVP     : 1;     // [0]   OTG 欠压故障：0b-无故障，1b-OTG UVP；默认值 0b
        uint16_t FAULT_OTG_OVP     : 1;     // [1]   OTG 过压故障：0b-无故障，1b-OTG OVP；默认值 0b
        uint16_t FAULT_LATCH       : 1;     // [2]   闭锁故障：0b-无故障，1b-触发了强制关闭(0x30[3])；默认值 0b
        uint16_t FAULT_SYS_SHORT   : 1;     // [3]   系统短路故障：0b-无故障，1b-SYS<2.4V且重启7次失败(需写0清除)；默认值 0b
        uint16_t SYSOVP_STAT       : 1;     // [4]   系统过压状态：0b-正常，1b-处于SYSOVP(需写0或拔适配器清除)；默认值 0b
        uint16_t FAULT_ACOC        : 1;     // [5]   输入过流故障：0b-无故障，1b-ACOC；默认值 0b
        uint16_t FAULT_BATOC       : 1;     // [6]   电池放电过流故障：0b-无故障，1b-BATOC；默认值 0b
        uint16_t FAULT_ACOV        : 1;     // [7]   输入过压故障：0b-无故障，1b-ACOV；默认值 0b

        // --- 高 8 位 (High Byte) --- 实时状态位
        uint16_t IN_OTG            : 1;     // [8]   OTG 模式状态：0b-非OTG，1b-处于OTG模式；默认值 0b
        uint16_t IN_PCHRG          : 1;     // [9]   预充电状态：0b-非预充，1b-处于预充电状态；默认值 0b
        uint16_t IN_FCHRG          : 1;     // [10]  快速充电状态：0b-非快充，1b-处于快速充电状态；默认值 0b
        uint16_t IN_IINDPM         : 1;     // [11]  IINDPM 状态：0b-正常，1b-处于输入电流限制调节状态；默认值 0b
        uint16_t IN_VINDPM         : 1;     // [12]  VINDPM 状态：0b-正常，1b-处于输入电压限制(或OTG稳压)状态；默认值 0b
        uint16_t IN_VAP            : 1;     // [13]  VAP 运行状态：0b-非VAP模式，1b-正在VAP模式下运行；默认值 0b
        uint16_t ICO_DONE          : 1;     // [14]  ICO 状态：0b-未完成，1b-ICO例程成功执行完成；默认值 0b
        uint16_t AC_STAT           : 1;     // [15]  输入源状态(AC_OK)：0b-适配器不存在，1b-适配器已连接；默认值 0b
    } maps;
} BQ25710_ChargerStatus_t;


/**
 * @brief BQ25710 ProchotStatus 寄存器联合体 (SMBus 地址 21h)
 * 该寄存器用于监控 PROCHOT 引脚的具体触发源，并管理脉冲宽度和清除逻辑。
 */
typedef union {
    uint16_t all;                           // 16位原始数值
    uint8_t low_byte;                       // 低4位原始数值
    uint8_t high_byte;                      // 高4位原始数值
    struct {
        // --- 低 8 位 (Low Byte) --- 触发事件实时状态 (除特殊说明外为只读 R)
        uint16_t STAT_Adapter_Removal : 1;  // [0]   适配器移除状态：0b-未触发，1b-触发；默认值 0b
        uint16_t STAT_Battery_Removal : 1;  // [1]   电池移除状态：0b-未触发，1b-触发；默认值 0b
        uint16_t STAT_VSYS            : 1;  // [2]   VSYS 欠压状态：0b-未触发，1b-触发；默认值 0b
        uint16_t STAT_IDCHG           : 1;  // [3]   电池放电过流状态：0b-未触发，1b-触发；默认值 0b
        uint16_t STAT_INOM            : 1;  // [4]   输入平均过流状态：0b-未触发，1b-触发；默认值 0b
        uint16_t STAT_ICRIT           : 1;  // [5]   输入峰值过流状态：0b-未触发，1b-触发；默认值 0b
        uint16_t STAT_COMP            : 1;  // [6]   独立比较器状态：0b-未触发，1b-触发；默认值 0b
        uint16_t STAT_VDPM            : 1;  // [7]   VBUS 欠压状态 (VDPM)：0b-未触发，1b-触发；默认值 0b (R/W)

        // --- 高 8 位 (High Byte) --- 控制与故障锁存
        uint16_t STAT_EXIT_VAP        : 1;  // [8]   退出 VAP 状态：0b-非活动，1b-活动并保持低电平(需写0清除)；默认值 0b
        uint16_t STAT_VAP_FAIL        : 1;  // [9]   VAP 加载失败：0b-正常，1b-连续7次失败并锁存(需写0清除)；默认值 0b
        uint16_t RESERVED_10          : 1;  // [10]  保留位；默认值 0b
        uint16_t PROCHOT_CLEAR        : 1;  // [11]  PROCHOT 脉冲清除：0b-清除脉冲并置高引脚，1b-空闲；默认值 1b
        uint16_t PROCHOT_WIDTH        : 2;  // [13-12] PROCHOT 脉冲宽度：00b-100us, 01b-1ms, 10b-10ms, 11b-5ms；默认值 10b
        uint16_t EN_PROCHOT_EXIT      : 1;  // [14]  脉冲扩展启用：0b-禁用，1b-保持低电平直到手动写位[11]清除；默认值 0b
        uint16_t RESERVED_15          : 1;  // [15]  保留位；默认值 1b
    } maps;
} BQ25710_ProchotStatus_t;

/**
 * @brief BQ25710 ChargeCurrent 寄存器联合体 (SMBus 地址 14h)
 * 采样电阻为 10mOhm 时：步进为 64mA，范围 0mA - 8128mA。
 */
typedef union {
    uint16_t all;                           // 16位原始数值 
    uint8_t low_byte;                       // 低4位原始数值
    uint8_t high_byte;                      // 高4位原始数值
    struct {
        // --- 低 8 位 (Low Byte) ---
        uint16_t RESERVED_5_0      : 6;     // [5-0] 保留位：忽略输入值；默认值 000000b
        uint16_t CURRENT_64        : 1;     // [6]   充电电流 Bit 0：1 = 增加 64mA；默认值 0b
        uint16_t CURRENT_128       : 1;     // [7]   充电电流 Bit 1：1 = 增加 128mA；默认值 0b

        // --- 高 8 位 (High Byte) ---
        uint16_t CURRENT_256       : 1;     // [8]   充电电流 Bit 2：1 = 增加 256mA；默认值 0b
        uint16_t CURRENT_512       : 1;     // [9]   充电电流 Bit 3：1 = 增加 512mA；默认值 0b
        uint16_t CURRENT_1024      : 1;     // [10]  充电电流 Bit 4：1 = 增加 1024mA；默认值 0b
        uint16_t CURRENT_2048      : 1;     // [11]  充电电流 Bit 5：1 = 增加 2048mA；默认值 0b
        uint16_t CURRENT_4096      : 1;     // [12]  充电电流 Bit 6：1 = 增加 4096mA；默认值 0b
        uint16_t RESERVED_15_13    : 3;     // [15-13] 保留位：未使用(1为无效写入)；默认值 000b
    } maps;
} BQ25710_ChargeCurrent_t;


/**
 * @brief BQ25710 MaxChargeVoltage 寄存器联合体 (SMBus 地址 15h)
 * 步进为 8mV，默认值：1S-4200mV、2S-8400mV、3S-12600mV、4S-16800mV。
 */
typedef union {
    uint16_t all;                           // 16位原始数值 
    uint8_t low_byte;                       // 低4位原始数值
    uint8_t high_byte;                      // 高4位原始数值
    struct {
        // --- 低 8 位 (Low Byte) ---
        uint16_t RESERVED_2_0      : 3;     // [2-0] 保留位：未使用，忽略值；默认值 000b
        uint16_t VOLTAGE_8         : 1;     // [3]   最大充电电压 Bit 0：1 = 增加 8mV；默认值 0b
        uint16_t VOLTAGE_16        : 1;     // [4]   最大充电电压 Bit 1：1 = 增加 16mV；默认值 0b
        uint16_t VOLTAGE_32        : 1;     // [5]   最大充电电压 Bit 2：1 = 增加 32mV；默认值 0b
        uint16_t VOLTAGE_64        : 1;     // [6]   最大充电电压 Bit 3：1 = 增加 64mV；默认值 0b
        uint16_t VOLTAGE_128       : 1;     // [7]   最大充电电压 Bit 4：1 = 增加 128mV；默认值 0b

        // --- 高 8 位 (High Byte) ---
        uint16_t VOLTAGE_256       : 1;     // [8]   最大充电电压 Bit 5：1 = 增加 256mV；默认值 0b
        uint16_t VOLTAGE_512       : 1;     // [9]   最大充电电压 Bit 6：1 = 增加 512mV；默认值 0b
        uint16_t VOLTAGE_1024      : 1;     // [10]  最大充电电压 Bit 7：1 = 增加 1024mV；默认值 0b
        uint16_t VOLTAGE_2048      : 1;     // [11]  最大充电电压 Bit 8：1 = 增加 2048mV；默认值 0b
        uint16_t VOLTAGE_4096      : 1;     // [12]  最大充电电压 Bit 9：1 = 增加 4096mV；默认值 0b
        uint16_t VOLTAGE_8192      : 1;     // [13]  最大充电电压 Bit 10：1 = 增加 8192mV；默认值 0b
        uint16_t VOLTAGE_16384     : 1;     // [14]  最大充电电压 Bit 11：1 = 增加 16384mV；默认值 0b
        uint16_t RESERVED_15       : 1;     // [15]  保留位：未使用(1为无效写入)；默认值 0b
    } maps;
} BQ25710_MaxChargeVoltage_t;

/**
 * @brief BQ25710 MinSystemVoltage 寄存器联合体 (SMBus 地址 3Eh)
 * 步进为 256mV，用于设定电池深度放电或缺失时 VSYS 的最小维持电压。
 */
typedef union {
    uint16_t all;                           // 16位原始数值 
    uint8_t low_byte;                       // 低4位原始数值
    uint8_t high_byte;                      // 高4位原始数值
    struct {
        // --- 低 8 位 (Low Byte) ---
        uint16_t RESERVED_7_0      : 8;     // [7-0]  保留位：未使用，忽略值；默认值 00000000b

        // --- 高 8 位 (High Byte) ---
        uint16_t MIN_SYS_V_256     : 1;     // [8]    最小系统电压 Bit 0：1 = 增加 256mV；默认值 0b
        uint16_t MIN_SYS_V_512     : 1;     // [9]    最小系统电压 Bit 1：1 = 增加 512mV；默认值 0b
        uint16_t MIN_SYS_V_1024    : 1;     // [10]   最小系统电压 Bit 2：1 = 增加 1024mV；默认值 0b
        uint16_t MIN_SYS_V_2048    : 1;     // [11]   最小系统电压 Bit 3：1 = 增加 2048mV；默认值 0b
        uint16_t MIN_SYS_V_4096    : 1;     // [12]   最小系统电压 Bit 4：1 = 增加 4096mV；默认值 0b
        uint16_t MIN_SYS_V_8192    : 1;     // [13]   最小系统电压 Bit 5：1 = 增加 8192mV；默认值 0b
        uint16_t RESERVED_15_14    : 2;     // [15-14] 保留位：未使用(1为无效写入)；默认值 00b
    } maps;
} BQ25710_MinSystemVoltage_t;

/**
 * @brief BQ25710 IIN_HOST 寄存器联合体 (SMBus 地址 3Fh)
 * 检测电阻为 10mOhm 时：步进为 50mA，范围 50mA - 6400mA。
 */
typedef union {
    uint16_t all;                           // 16位原始数值 
    uint8_t low_byte;                       // 低4位原始数值
    uint8_t high_byte;                      // 高4位原始数值
    struct {
        // --- 低 8 位 (Low Byte) ---
        uint16_t RESERVED_7_0      : 8;     // [7-0]  保留位：未使用，忽略值；默认值 00000000b

        // --- 高 8 位 (High Byte) ---
        uint16_t IIN_HOST_50       : 1;     // [8]    输入电流 Bit 0：1 = 增加 50mA；默认值 1b
        uint16_t IIN_HOST_100      : 1;     // [9]    输入电流 Bit 1：1 = 增加 100mA；默认值 0b
        uint16_t IIN_HOST_200      : 1;     // [10]   输入电流 Bit 2：1 = 增加 200mA；默认值 0b
        uint16_t IIN_HOST_400      : 1;     // [11]   输入电流 Bit 3：1 = 增加 400mA；默认值 0b
        uint16_t IIN_HOST_800      : 1;     // [12]   输入电流 Bit 4：1 = 增加 800mA；默认值 0b
        uint16_t IIN_HOST_1600     : 1;     // [13]   输入电流 Bit 5：1 = 增加 1600mA；默认值 0b
        uint16_t IIN_HOST_3200     : 1;     // [14]   输入电流 Bit 6：1 = 增加 3200mA；默认值 1b
        uint16_t RESERVED_15       : 1;     // [15]   保留位：未使用(1为无效写入)；默认值 0b
    } maps;
} BQ25710_IN_CURR_HOST_t;


/**
 * @brief BQ25710 IIN_DPM 寄存器联合体 (SMBus 地址 22h)
 * 注意：该寄存器为只读 (R)，反映当前 DPM 环路实际生效的输入电流限制值。
 * 检测电阻为 10mOhm 时：步进为 50mA。
 */
typedef union {
    uint16_t all;                           // 16位原始数值 
    uint8_t low_byte;                       // 低4位原始数值
    uint8_t high_byte;                      // 高4位原始数值
    struct {
        // --- 低 8 位 (Low Byte) ---
        uint16_t RESERVED_7_0      : 8;     // [7-0]  保留位：未使用，忽略值；默认值 00000000b

        // --- 高 8 位 (High Byte) ---
        uint16_t IIN_DPM_50        : 1;     // [8]    DPM 输入电流 Bit 0：1 = 增加 50mA；默认值 0b
        uint16_t IIN_DPM_100       : 1;     // [9]    DPM 输入电流 Bit 1：1 = 增加 100mA；默认值 0b
        uint16_t IIN_DPM_200       : 1;     // [10]   DPM 输入电流 Bit 2：1 = 增加 200mA；默认值 0b
        uint16_t IIN_DPM_400       : 1;     // [11]   DPM 输入电流 Bit 3：1 = 增加 400mA；默认值 0b
        uint16_t IIN_DPM_800       : 1;     // [12]   DPM 输入电流 Bit 4：1 = 增加 800mA；默认值 0b
        uint16_t IIN_DPM_1600      : 1;     // [13]   DPM 输入电流 Bit 5：1 = 增加 1600mA；默认值 0b
        uint16_t IIN_DPM_3200      : 1;     // [14]   DPM 输入电流 Bit 6：1 = 增加 3200mA；默认值 0b
        uint16_t RESERVED_15       : 1;     // [15]   保留位：未使用；默认值 0b
    } maps;
} BQ25710_IN_CURR_DPM_t;

/**
 * @brief BQ25710 InputVoltage 寄存器联合体 (SMBus 地址 3Dh)
 * 步进为 64mV，用于设定 VINDPM 阈值。
 */
typedef union {
    uint16_t all;                           // 16位原始数值 
    uint8_t low_byte;                       // 低4位原始数值
    uint8_t high_byte;                      // 高4位原始数值
    struct {
        // --- 低 8 位 (Low Byte) ---
        uint16_t RESERVED_5_0      : 6;     // [5-0]  保留位：未使用，忽略值；默认值 000000b
        uint16_t VINDPM_64         : 1;     // [6]    输入电压 Bit 0：1 = 增加 64mV；默认值 0b
        uint16_t VINDPM_128        : 1;     // [7]    输入电压 Bit 1：1 = 增加 128mV；默认值 0b

        // --- 高 8 位 (High Byte) ---
        uint16_t VINDPM_256        : 1;     // [8]    输入电压 Bit 2：1 = 增加 256mV；默认值 0b
        uint16_t VINDPM_512        : 1;     // [9]    输入电压 Bit 3：1 = 增加 512mV；默认值 0b
        uint16_t VINDPM_1024       : 1;     // [10]   输入电压 Bit 4：1 = 增加 1024mV；默认值 0b
        uint16_t VINDPM_2048       : 1;     // [11]   输入电压 Bit 5：1 = 增加 2048mV；默认值 0b
        uint16_t VINDPM_4096       : 1;     // [12]   输入电压 Bit 6：1 = 增加 4096mV；默认值 0b
        uint16_t VINDPM_8192       : 1;     // [13]   输入电压 Bit 7：1 = 增加 8192mV；默认值 0b
        uint16_t RESERVED_15_14    : 2;     // [15-14] 保留位：未使用(1为无效写入)；默认值 00b
    } maps;
} BQ25710_InputVoltage_t;

/**
 * @brief BQ25710 OTGVoltage 寄存器联合体 (SMBus 地址 3Bh)
 * 步进约为 8.128mV，用于设定 OTG 模式下的输出电压。
 */
typedef union {
    uint16_t all;                           // 16位原始数值 
    uint8_t low_byte;                       // 低4位原始数值
    uint8_t high_byte;                      // 高4位原始数值
    struct {
        // --- 低 8 位 (Low Byte) ---
        uint16_t RESERVED_1_0      : 2;     // [1-0]  保留位：未使用，忽略值；默认值 00b
        uint16_t OTG_V_8_1         : 1;     // [2]    OTG 电压 Bit 0：1 = 增加 8.1mV；默认值 0b
        uint16_t OTG_V_16          : 1;     // [3]    OTG 电压 Bit 1：1 = 增加 16mV；默认值 0b
        uint16_t OTG_V_33          : 1;     // [4]    OTG 电压 Bit 2：1 = 增加 33mV；默认值 0b
        uint16_t OTG_V_65          : 1;     // [5]    OTG 电压 Bit 3：1 = 增加 65mV；默认值 0b
        uint16_t OTG_V_130         : 1;     // [6]    OTG 电压 Bit 4：1 = 增加 130mV；默认值 0b
        uint16_t OTG_V_260         : 1;     // [7]    OTG 电压 Bit 5：1 = 增加 260mV；默认值 0b

        // --- 高 8 位 (High Byte) ---
        uint16_t OTG_V_521         : 1;     // [8]    OTG 电压 Bit 6：1 = 增加 521mV；默认值 0b
        uint16_t OTG_V_1041        : 1;     // [9]    OTG 电压 Bit 7：1 = 增加 1041mV；默认值 0b
        uint16_t OTG_V_2082        : 1;     // [10]   OTG 电压 Bit 8：1 = 增加 2082mV；默认值 0b
        uint16_t OTG_V_4164        : 1;     // [11]   OTG 电压 Bit 9：1 = 增加 4164mV；默认值 0b
        uint16_t OTG_V_8328        : 1;     // [12]   OTG 电压 Bit 10：1 = 增加 8328mV；默认值 0b
        uint16_t OTG_V_16656       : 1;     // [13]   OTG 电压 Bit 11：1 = 增加 16656mV；默认值 0b
        uint16_t RESERVED_15_14    : 2;     // [15-14] 保留位：未使用(1为无效写入)；默认值 00b
    } maps;
} BQ25710_OTGVoltage_t;


/**
 * @brief BQ25710 OTGCurrent 寄存器联合体 (SMBus 地址 3Ch)
 * 步进为 50mA，用于设定 OTG 模式下的输出电流限制。
 */
typedef union {
    uint16_t all;                           // 16位原始数值 
    uint8_t low_byte;                       // 低4位原始数值
    uint8_t high_byte;                      // 高4位原始数值
    struct {
        // --- 低 8 位 (Low Byte) ---
        uint16_t RESERVED_7_0      : 8;     // [7-0]  保留位：未使用，忽略值；默认值 00000000b

        // --- 高 8 位 (High Byte) ---
        uint16_t OTG_CURR_50       : 1;     // [8]    OTG 电流 Bit 0：1 = 增加 50mA；默认值 0b
        uint16_t OTG_CURR_100      : 1;     // [9]    OTG 电率 Bit 1：1 = 增加 100mA；默认值 0b
        uint16_t OTG_CURR_200      : 1;     // [10]   OTG 电流 Bit 2：1 = 增加 200mA；默认值 0b
        uint16_t OTG_CURR_400      : 1;     // [11]   OTG 电流 Bit 3：1 = 增加 400mA；默认值 0b
        uint16_t OTG_CURR_800      : 1;     // [12]   OTG 电流 Bit 4：1 = 增加 800mA；默认值 0b
        uint16_t OTG_CURR_1600     : 1;     // [13]   OTG 电流 Bit 5：1 = 增加 1600mA；默认值 0b
        uint16_t OTG_CURR_3200     : 1;     // [14]   OTG 电流 Bit 6：1 = 增加 3200mA；默认值 0b
        uint16_t RESERVED_15       : 1;     // [15]   保留位：未使用(1为无效写入)；默认值 0b
    } maps;
} BQ25710_OTGCurrent_t;

/**
 * @brief BQ25710 ADCVBUS/PSYS 寄存器联合体 (SMBus 地址 23h)
 * 该寄存器存储 ADC 转换后的 VBUS 电压和 PSYS 功率数据。
 */
typedef union {
    uint16_t all;                           // 16位原始数值 
    uint8_t low_byte;                       // 低4位原始数值
    uint8_t high_byte;                      // 高4位原始数值
    struct {
        // --- 低 8 位 (Low Byte) ---
        uint16_t ADC_PSYS          : 8;     // [7-0]  PSYS 8位数字输出。LSB: 12mV, 全量程: 3.06V
        
        // --- 高 8 位 (High Byte) ---
        uint16_t ADC_VBUS          : 8;     // [15-8] VBUS 8位数字输出。LSB: 64mV, 基准偏移: 3200mV
    } maps;
} BQ25710_ADCVBUSPSYS_t;


/**
 * @brief BQ25710 ADCIBAT 寄存器联合体 (SMBus 地址 24h)
 * 该寄存器存储 ADC 转换后的电池充电和放电电流数据。
 * 注意：位 15 和位 7 为保留位，数据从位 14-8 和 6-0 中读取。
 */
typedef union {
    uint16_t all;                           // 16位原始数值 
    uint8_t low_byte;                       // 低4位原始数值
    uint8_t high_byte;                      // 高4位原始数值
    struct {
        // --- 低 8 位 (Low Byte) ---
        uint16_t ADC_IDCHG         : 7;     // [6-0]  电池放电电流 7位输出。LSB: 256mA, 全量程: 32.512A
        uint16_t RESERVED_7        : 1;     // [7]    保留位：未使用

        // --- 高 8 位 (High Byte) ---
        uint16_t ADC_ICHG          : 7;     // [14-8] 电池充电电流 7位输出。LSB: 64mA, 全量程: 8.128A
        uint16_t RESERVED_15       : 1;     // [15]   保留位：未使用
    } maps;
} BQ25710_ADCIBAT_t;


/**
 * @brief BQ25710 ADCIINCMPIN 寄存器联合体 (SMBus 地址 25h)
 * 该寄存器存储 ADC 转换后的输入电流 (IIN) 和比较器输入 (CMPIN) 数据。
 */
typedef union {
    uint16_t all;                           // 16位原始数值 
    uint8_t low_byte;                       // 低4位原始数值
    uint8_t high_byte;                      // 高4位原始数值
    struct {
        // --- 低 8 位 (Low Byte) ---
        uint16_t ADC_CMPIN         : 8;     // [7-0]  CMPIN 电压 8位数字输出。LSB: 12mV, 全量程: 3.06V
        
        // --- 高 8 位 (High Byte) ---
        uint16_t ADC_IIN           : 8;     // [15-8] 输入电流 8位数字输出。
                                            // 10mOhm电阻下 LSB: 25mA (注意：此处计算需对应手册10mOhm逻辑)
    } maps;
} BQ25710_ADCIINCMPIN_t;

/**
 * @brief BQ25710 ADCVSYSVBAT 寄存器联合体 (SMBus 地址 26h)
 * 该寄存器存储 ADC 转换后的系统电压 (VSYS) 和电池电压 (VBAT) 数据。
 */
typedef union {
    uint16_t all;                           // 16位原始数值 
    uint8_t low_byte;                       // 低4位原始数值
    uint8_t high_byte;                      // 高4位原始数值
    struct {
        // --- 低 8 位 (Low Byte) ---
        uint16_t ADC_VBAT          : 8;     // [7-0]  电池电压 8位数字输出。LSB: 64mV, 基准偏移: 2880mV
        
        // --- 高 8 位 (High Byte) ---
        uint16_t ADC_VSYS          : 8;     // [15-8] 系统电压 8位数字输出。LSB: 64mV, 基准偏移: 2880mV
    } maps;
} BQ25710_ADCVSYSVBAT_t;

/**
 * @brief BQ25710 厂商 ID 寄存器 (FEh)
 */
typedef union {
    uint16_t all;                           // 16位原始数值 
    uint8_t low_byte;                       // 低4位原始数值
    uint8_t high_byte;                      // 高4位原始数值
    struct {
        uint16_t MANUFACTURE_ID    : 16;    // [15-0] 厂商 ID：固定为 0x0040 (TI 厂商代码)
    } maps;
} BQ25710_ManufactureID_t;

/**
 * @brief BQ25710 器件 ID 寄存器 (FFh)
 */
typedef union {
    uint16_t all;                           // 16位原始数值 
    uint8_t low_byte;                       // 低4位原始数值
    uint8_t high_byte;                      // 高4位原始数值
    struct {
        uint16_t DEVICE_ID         : 8;     // [7-0]  器件 ID：SMBus 模式下固定为 0x89
        uint16_t RESERVED_15_8     : 8;     // [15-8] 保留位
    } maps;
} BQ25710_DeviceID_t;


/**
 * @brief BQ25710 影子寄存器映射根结构体
 * 建议在内存中维护此结构体的一个实例，用于统一管理和批量更新。
 */
typedef struct {
    // --- 配置类寄存器 (决定芯片如何工作) ---
    BQ25710_ChargeOption0_t    Option0;       /**< [0x12] 基础充电选项 */
    BQ25710_ChargeOption1_t    Option1;       /**< [0x30] 增益与硬件参数设置 */
    BQ25710_ChargeOption2_t    Option2;       /**< [0x31] 过流保护与峰值功率控制 */
    BQ25710_ChargeOption3_t    Option3;       /**< [0x32] OTG使能与系统复位 */
    BQ25710_ProchotOption0_t   ProchotOpt0;   /**< [0x33] 报警阈值配置 */
    BQ25710_ProchotOption1_t   ProchotOpt1;   /**< [0x34] 报警源触发屏蔽 */
    BQ25710_ADCOption_t        AdcOpt;        /**< [0x35] ADC 通道与采样配置 */

    // --- 控制类寄存器 (直接影响电压/电流) ---
    BQ25710_ChargeCurrent_t    ChargeCurrent; /**< [0x14] 电池充电电流目标值 */
    BQ25710_MaxChargeVoltage_t MaxChargeVolt; /**< [0x15] 电池充电电压目标 */
    BQ25710_MinSystemVoltage_t MinSysVolt;    /**< [0x3E] VSYS 最小电压维持点 */
    BQ25710_InputVoltage_t     InputVolt;     /**< [0x3D] 输入电压 DPM 保护阈值 */
    BQ25710_IN_CURR_HOST_t     InCurrHost;    /**< [0x3F] 适配器端输入电流限制 */
    BQ25710_OTGVoltage_t       OtgVolt;       /**< [0x3B] OTG 模式 VBUS 输出电压 */
    BQ25710_OTGCurrent_t       OtgCurr;       /**< [0x3C] OTG 模式 VBUS 限流值 */

    // --- 状态类寄存器 (监控实时运行状况) ---
    BQ25710_ChargerStatus_t    Status;        /**< [0x20] 反馈 AC、ICO、故障锁存信息 */
    BQ25710_ProchotStatus_t    ProchotStatus; /**< [0x21] 反馈 PROCHOT 详细触发原因 */
    BQ25710_IN_CURR_DPM_t      InCurrDpm;     /**< [0x22] 反馈实际生效的输入电流限制 */

    // --- ADC 遥测类寄存器 (传感器原始读数) ---
    BQ25710_ADCVBUSPSYS_t      AdcVbusPsys;   /**< [0x23] VBUS 电压及 PSYS 功率读数 */
    BQ25710_ADCIBAT_t          AdcIbat;       /**< [0x24] 电池充/放电电流实时采样 */
    BQ25710_ADCIINCMPIN_t      AdcIinCmpin;   /**< [0x25] 输入电流及比较器输入读数 */
    BQ25710_ADCVSYSVBAT_t      AdcVsysVbat;   /**< [0x26] 系统电压及电池电压实时读数 */

    // --- ID 识别类 ---
    BQ25710_ManufactureID_t    MfrID;         /**< [0xFE] 厂商识别码 (自检用) */
    BQ25710_DeviceID_t         DevID;         /**< [0xFF] 器件识别码 (自检用) */
} BQ25710_RegMap_t;


// clang-format on
#endif /* BQ25710_ABSTRACTION_H */
