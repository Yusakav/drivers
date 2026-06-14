/**
 * @file sc8701.h
 * @author YusakaV (YusakaVivy@gmail.com)
 * @brief SC8701 同步 Buck-Boost 控制器驱动
 * @version 1.0
 * @date 2026-06-13
 *
 * @note SC8701 是纯模拟控制器，无 I2C/SMBus 接口，无内部寄存器。
 *       驱动通过 MCU 外设 (GPIO/PWM/ADC) 管理芯片的使能、动态调压、动态限流和状态监测。
 *
 * 引脚功能：
 *   /CE  (GPIO OUT)  — 芯片使能（低有效），用于开关芯片
 *   PWM  (TIM  PWM)  — 动态调节输出电压，20kHz~100kHz
 *   IPWM (TIM  PWM)  — 动态调节电流限制，20kHz~100kHz
 *   ITUNE(GPIO OUT)  — 选择 IPWM 控制目标（输入电流 / 输出电流）
 *   PG   (GPIO IN)   — Power Good 指示，VOUT 稳定在 90%~110% 目标值时输出高
 *
 * 关键电参数（硬件电阻配置，驱动仅记录）：
 *   VOUT  = VFB_REF × (1 + RUP / RDOWN), VFB_REF = 1.22V
 *   IIN_LIM   = VREF / RILIM1 × RSS1 / RSNS1, VREF = 1.21V
 *   IOUT_LIM  = VREF / RILIM2 × RSS2 / RSNS2
 *   PWM 调压: VOUT = VOUT_SET × (1/6 + 5/6 × D), D ∈ [0, 1]
 *   IPWM 限流: ILIMx = ILIMx_SET × D, D ∈ [0, 1]
 *
 * @copyright Copyright (c) 2026
 */

#ifndef SC8701_H
#define SC8701_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ==================== 芯片参数常量 ==================== */

#define SC8701_VFB_REF_MV          1220U   /**< 反馈基准电压 (mV) */
#define SC8701_VREF_MV             1210U   /**< ILIM 基准电压 (mV) */
#define SC8701_VIN_MIN_MV          2700U   /**< 最小输入电压 (mV) */
#define SC8701_VIN_MAX_MV          36000U  /**< 最大输入电压 (mV) */
#define SC8701_VOUT_MIN_MV         2000U   /**< 最小输出电压 (mV) */
#define SC8701_VOUT_MAX_MV         36000U  /**< 最大输出电压 (mV) */
#define SC8701_PWM_FREQ_MIN_HZ     20000U  /**< PWM 最低频率 (Hz) */
#define SC8701_PWM_FREQ_MAX_HZ     100000U /**< PWM 最高频率 (Hz) */
#define SC8701_PG_THRESHOLD_LO     90U     /**< PG 下限阈值 (%) */
#define SC8701_PG_THRESHOLD_HI     110U    /**< PG 上限阈值 (%) */
#define SC8701_SOFT_START_MS_MAX   15U     /**< 软启动最大时间 (ms) */
#define SC8701_TSD_CELSIUS         165U    /**< 过热关断温度 (°C) */
#define SC8701_UVLO_RISING_MV      2600U   /**< UVLO 上升阈值 (mV) */
#define SC8701_UVLO_HYST_MV        160U    /**< UVLO 迟滞 (mV) */

/* ==================== 枚举定义 ==================== */

/**
 * @brief SC8701 工作模式
 */
typedef enum {
    SC8701_MODE_BUCK       = 0,  /**< VIN > VOUT, 降压模式 */
    SC8701_MODE_BOOST      = 1,  /**< VIN < VOUT, 升压模式 */
    SC8701_MODE_BUCK_BOOST = 2,  /**< VIN ≈ VOUT, 过渡模式 */
    SC8701_MODE_OFF        = 3,  /**< 芯片未使能 */
    SC8701_MODE_FAULT      = 4,  /**< 故障状态 */
} sc8701_mode_t;

/**
 * @brief SC8701 IPWM 控制目标选择
 */
typedef enum {
    SC8701_ITUNE_INPUT  = 0,  /**< IPWM 控制输入电流限制 (ILIM1) */
    SC8701_ITUNE_OUTPUT = 1,  /**< IPWM 控制输出电流限制 (ILIM2) */
} sc8701_itune_target_t;

/**
 * @brief 开关频率选择 (FREQ 引脚电阻)
 */
typedef enum {
    SC8701_FREQ_200KHZ  = 0,  /**< FREQ → GND, 200kHz */
    SC8701_FREQ_400KHZ  = 1,  /**< FREQ → 68kΩ → GND, 400kHz */
    SC8701_FREQ_600KHZ  = 2,  /**< FREQ → Open, 600kHz */
} sc8701_freq_t;

/**
 * @brief 死区时间选择 (DT 引脚电阻)
 */
typedef enum {
    SC8701_DT_20NS  = 0,  /**< DT → GND, 20ns */
    SC8701_DT_40NS  = 1,  /**< DT → 68kΩ → GND, 40ns */
    SC8701_DT_60NS  = 2,  /**< DT → 270kΩ → GND, 60ns */
    SC8701_DT_80NS  = 3,  /**< DT → Open, 80ns */
} sc8701_dt_t;

/**
 * @brief 驱动返回码
 */
typedef enum {
    SC8701_OK              =  0,  /**< 成功 */
    SC8701_ERR_PARAM       = -1,  /**< 参数错误 */
    SC8701_ERR_NOT_READY   = -2,  /**< 芯片未就绪 */
    SC8701_ERR_PG_TIMEOUT  = -3,  /**< Power Good 超时 */
    SC8701_ERR_FAULT       = -4,  /**< 故障状态 */
    SC8701_ERR_HW          = -5,  /**< 硬件抽象层错误 */
} sc8701_err_t;

/* ==================== 硬件抽象层回调 ==================== */

/**
 * @brief 硬件抽象层操作接口
 *
 * 由上层在初始化前填入实际的 MCU 外设操作函数
 */
typedef struct {
    /* --- GPIO 操作 --- */
    void    (*ce_set)       (uint8_t level);       /**< /CE 引脚输出 (0=使能, 1=关断) */
    void    (*itune_set)    (uint8_t level);       /**< ITUNE 引脚输出 */
    uint8_t (*pg_get)       (void);                /**< PG 引脚输入 (1=Power Good) */

    /* --- PWM 操作 --- */
    int8_t  (*pwm_vout_init)(uint32_t freq_hz);    /**< PWM(VOUT 调压) 初始化, 返回 0 成功 */
    void    (*pwm_vout_set) (float duty);           /**< PWM(VOUT 调压) 设置占空比 [0.0, 1.0] */
    int8_t  (*pwm_ilim_init)(uint32_t freq_hz);    /**< IPWM(限流) 初始化, 返回 0 成功 */
    void    (*pwm_ilim_set) (float duty);           /**< IPWM(限流) 设置占空比 [0.0, 1.0] */

    /* --- ADC 操作 (可选, 用于监测) --- */
    int8_t  (*adc_read_mv)  (uint8_t channel, uint16_t *mv); /**< ADC 读取电压, 返回 0 成功 */

    /* --- 延时 --- */
    void    (*delay_ms)     (uint32_t ms);          /**< 毫秒延时 */
    void    (*delay_us)     (uint32_t us);          /**< 微秒延时 */

    /* --- 时间戳 --- */
    uint32_t (*get_ms)      (void);                 /**< 获取系统毫秒时间戳 */
} sc8701_hal_t;

/* ==================== 硬件配置结构体 ==================== */

/**
 * @brief SC8701 硬件配置 (电阻分压器设定, 上电后不可软件更改)
 *
 * 这些参数由 PCB 上的电阻值决定，驱动初始化时记录，用于计算实际电压/电流值。
 */
typedef struct {
    /* 输出电压设定 */
    uint32_t r_up_ohm;          /**< FB 上拉电阻 (Ω) */
    uint32_t r_down_ohm;        /**< FB 下拉电阻 (Ω) */

    /* 输入电流限制 */
    uint32_t rilim1_ohm;        /**< ILIM1 引脚电阻 (Ω) */
    uint32_t rss1_ohm;          /**< 输入检流电阻 RSNS1 (μΩ → 内部换算为 Ω) */
    uint32_t rsns1_uohm;        /**< 输入检流电阻 RSNS1 (μΩ)，典型 10000 */

    /* 输出电流限制 */
    uint32_t rilim2_ohm;        /**< ILIM2 引脚电阻 (Ω) */
    uint32_t rsns2_uohm;        /**< 输出检流电阻 RSNS2 (μΩ)，典型 10000 */

    /* 频率和死区 */
    sc8701_freq_t freq;         /**< FREQ 引脚配置 */
    sc8701_dt_t   dead_time;    /**< DT 引脚配置 */
} sc8701_hw_config_t;

/* ==================== 驱动上下文结构体 ==================== */

/**
 * @brief SC8701 驱动运行时上下文
 */
typedef struct {
    const sc8701_hal_t      *hal;           /**< 硬件抽象层接口 */
    const sc8701_hw_config_t *hw;           /**< 硬件配置 (电阻设定) */

    /* --- 计算得到的硬件参数 --- */
    uint32_t vout_set_mv;                   /**< FB 电阻分压器对应的设定输出电压 (mV) */
    uint32_t iin_lim_ma;                    /**< ILIM1 电阻对应的输入电流限制 (mA) */
    uint32_t iout_lim_ma;                   /**< ILIM2 电阻对应的输出电流限制 (mA) */

    /* --- 动态调节参数 --- */
    uint32_t vout_target_mv;                /**< 当前动态调压目标值 (mV) */
    uint32_t ilim_target_ma;                /**< 当前动态限流目标值 (mA) */
    sc8701_itune_target_t itune_target;     /**< 当前 IPWM 控制目标 */

    /* --- 运行状态 --- */
    uint8_t  enabled;                       /**< 芯片是否已使能 */
    uint8_t  pg_ok;                         /**< Power Good 状态 */
    uint8_t  fault;                         /**< 故障标志 */
    uint32_t enable_time_ms;                /**< 使能时刻 (ms) */

    /* --- PWM 参数 --- */
    uint32_t pwm_vout_freq_hz;              /**< PWM 调压频率 (Hz) */
    uint32_t pwm_ilim_freq_hz;              /**< IPWM 限流频率 (Hz) */
} sc8701_ctx_t;

/* ==================== ADC 通道枚举 (可选) ==================== */

/**
 * @brief ADC 监测通道定义
 *
 * 这些通道需要外部 ADC 或 MCU 内置 ADC 实现。
 * 索引 0~7 由用户在 hal->adc_read_mv() 中映射到实际 ADC 通道。
 */
typedef enum {
    SC8701_ADC_VIN    = 0,   /**< 输入电压 */
    SC8701_ADC_VOUT   = 1,   /**< 输出电压 */
    SC8701_ADC_IIN    = 2,   /**< 输入电流 (通过检流电阻电压换算) */
    SC8701_ADC_IOUT   = 3,   /**< 输出电流 (通过检流电阻电压换算) */
    SC8701_ADC_VCC    = 4,   /**< 内部 VCC (10V LDO) */
    SC8701_ADC_TEMP   = 5,   /**< 温度 (NTC 分压, 可选) */
    SC8701_ADC_CH_MAX = 6,
} sc8701_adc_channel_t;

/* ==================== 公开 API ==================== */

/**
 * @brief 初始化 SC8701 驱动
 *
 * 根据硬件配置计算设定电压/电流值，初始化 PWM 外设。
 * 调用后芯片仍处于关断状态，需调用 sc8701_enable() 使能。
 *
 * @param ctx   驱动上下文指针
 * @param hal   硬件抽象层接口 (非空)
 * @param hw    硬件配置 (非空)
 * @return      SC8701_OK 成功，其他为错误码
 */
int8_t sc8701_init (sc8701_ctx_t *ctx,
                    const sc8701_hal_t *hal,
                    const sc8701_hw_config_t *hw);

/**
 * @brief 使能 SC8701 (/CE 拉低)
 *
 * 拉低 /CE 引脚，等待软启动完成后检查 PG 状态。
 * 若指定超时时间内 PG 未拉高，返回 SC8701_ERR_PG_TIMEOUT。
 *
 * @param ctx           驱动上下文
 * @param timeout_ms    等待 PG 的超时时间 (ms)，0 表示不等待
 * @return              SC8701_OK 成功
 */
int8_t sc8701_enable (sc8701_ctx_t *ctx, uint32_t timeout_ms);

/**
 * @brief 关断 SC8701 (/CE 拉高)
 *
 * @param ctx 驱动上下文
 * @return    SC8701_OK
 */
int8_t sc8701_disable (sc8701_ctx_t *ctx);

/**
 * @brief 动态调节输出电压 (通过 PWM 引脚)
 *
 * 输出电压 = VOUT_SET × (1/6 + 5/6 × duty)
 * duty ∈ [0.0, 1.0], 实际范围受限于芯片 VOUT 范围 (2V~36V)
 *
 * @param ctx       驱动上下文
 * @param target_mv 目标输出电压 (mV)
 * @return          SC8701_OK 成功
 */
int8_t sc8701_set_vout (sc8701_ctx_t *ctx, uint32_t target_mv);

/**
 * @brief 动态调节电流限制 (通过 IPWM 引脚)
 *
 * 限流值 = ILIMx_SET × duty
 * 控制目标由 ITUNE 引脚决定 (通过 sc8701_set_itune_target 设置)
 *
 * @param ctx       驱动上下文
 * @param target_ma 目标限流值 (mA)
 * @return          SC8701_OK 成功
 */
int8_t sc8701_set_ilim (sc8701_ctx_t *ctx, uint32_t target_ma);

/**
 * @brief 设置 IPWM 控制目标 (输入电流 / 输出电流)
 *
 * @param ctx    驱动上下文
 * @param target 控制目标
 * @return       SC8701_OK
 */
int8_t sc8701_set_itune_target (sc8701_ctx_t *ctx, sc8701_itune_target_t target);

/**
 * @brief 获取 Power Good 状态
 *
 * @param ctx 驱动上下文
 * @return    1 = PG OK (VOUT 在 90%~110% 目标值范围内)
 *            0 = PG 未就绪
 */
uint8_t sc8701_get_pg (sc8701_ctx_t *ctx);

/**
 * @brief 获取当前工作模式
 *
 * 根据使能状态和 PG 状态推断当前模式。
 * 精确判断需要外部 ADC 读取 VIN/VOUT 进行比较。
 *
 * @param ctx 驱动上下文
 * @return    工作模式枚举
 */
sc8701_mode_t sc8701_get_mode (sc8701_ctx_t *ctx);

/**
 * @brief 获取芯片是否已使能
 */
static inline uint8_t sc8701_is_enabled (sc8701_ctx_t *ctx) {
    return ctx->enabled;
}

/**
 * @brief 获取 FB 分压器对应的硬件设定输出电压 (mV)
 */
static inline uint32_t sc8701_get_vout_set (sc8701_ctx_t *ctx) {
    return ctx->vout_set_mv;
}

/**
 * @brief 获取当前动态调压目标值 (mV)
 */
static inline uint32_t sc8701_get_vout_target (sc8701_ctx_t *ctx) {
    return ctx->vout_target_mv;
}

#ifdef __cplusplus
}
#endif

#endif /* SC8701_H */
