/**
 * @file    zw101_fp_protocol.h
 * @brief   ZW101 指纹模块通信协议层 — 数据包定义、指令码、状态码
 * @note    基于官方 fp_syno_protocol.h 重构，移除平台依赖，纯 C99 标准
 * @version 1.0
 * @date    2026-06-13
 *
 * 数据包结构 (参考《指纹模组产品用户手册 V1.5.1》§3.1)：
 *   命令包: 0xEF01 | DevAddr(4B) | 0x01 | Len(2B) | Cmd(1B)  | Param(N) | Checksum(2B)
 *   数据包: 0xEF01 | DevAddr(4B) | 0x02 | Len(2B) | Data(N)  | Checksum(2B)
 *   结束包: 0xEF01 | DevAddr(4B) | 0x08 | Len(2B) | Data(N)  | Checksum(2B)
 *   应答包: 0xEF01 | DevAddr(4B) | 0x07 | Len(2B) | ACK(1B)  | Ret(N)   | Checksum(2B)
 */

#ifndef ZW101_FP_PROTOCOL_H
#define ZW101_FP_PROTOCOL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* ==================== 常量定义 ==================== */

/** @brief 包头第一/第二字节 (大端: 0xEF01) */
#define ZW101_HEADER_FIRST             0xEFu
#define ZW101_HEADER_SECOND            0x01u

/** @brief 数据包包标识 */
#define ZW101_PKG_CMD                  0x01u   /**< 命令包 */
#define ZW101_PKG_DATA                 0x02u   /**< 数据包 (有后续) */
#define ZW101_PKG_ACK                  0x07u   /**< 应答包 */
#define ZW101_PKG_END                  0x08u   /**< 结束包 (最后一个数据包) */

/** @brief 默认设备地址 (出厂设置) */
#define ZW101_DEFAULT_DEV_ADDR         0xFFFFFFFFu

/** @brief 默认波特率 */
#define ZW101_DEFAULT_BAUD             57600u

/** @brief 协议帧固定偏移量 */
#define ZW101_CALC_SUM_OFFSET          6u      /**< 校验和计算起始偏移 */
#define ZW101_CMD_CODE_OFFSET          9u      /**< 指令码所在偏移 */
#define ZW101_VARIABLE_FIELD_OFFSET    10u     /**< 可变参数字段起始偏移 */

/** @brief 命令帧最小长度 (包头2 + 地址4 + 标识1 + 长度2 + 校验2 = 11) */
#define ZW101_PKG_MIN_LEN              11u

/** @brief 接收缓冲区最大长度 */
#define ZW101_RCV_BUF_MAX              256u

/** @brief 指纹库最大容量 (ZW101 = 50枚) */
#define ZW101_MAX_FINGERPRINT_COUNT    50u

/** @brief 默认注册次数 (小面积传感器: 5~10次) */
#define ZW101_DEFAULT_ENROLL_COUNT     6u

/* ==================== 指令码枚举 ==================== */

typedef enum {
    /* --- 通用类指令 --- */
    ZW101_CMD_GET_IMAGE              = 0x01, /**< 验证用获取图像 */
    ZW101_CMD_GEN_CHAR               = 0x02, /**< 生成特征 (从图像缓冲区) */
    ZW101_CMD_MATCH                  = 0x03, /**< 精确比对两枚指纹特征 (1:1) */
    ZW101_CMD_SEARCH                 = 0x04, /**< 搜索指纹库 (1:N) */
    ZW101_CMD_REG_MODEL              = 0x05, /**< 合并特征生成模板 */
    ZW101_CMD_STORE_CHAR             = 0x06, /**< 存储模板到 Flash 指纹库 */
    ZW101_CMD_LOAD_CHAR              = 0x07, /**< 从 Flash 指纹库读出模板 */
    ZW101_CMD_UP_CHAR                = 0x08, /**< 上传模板到主控 */
    ZW101_CMD_DOWN_CHAR              = 0x09, /**< 下载模板到模组 */
    ZW101_CMD_UP_IMAGE               = 0x0A, /**< 上传图像 */
    ZW101_CMD_DOWN_IMAGE             = 0x0B, /**< 下载图像 */
    ZW101_CMD_DEL_CHAR               = 0x0C, /**< 删除指定模板 */
    ZW101_CMD_EMPTY                  = 0x0D, /**< 清空指纹库 */
    ZW101_CMD_WRITE_REG              = 0x0E, /**< 写系统寄存器 */
    ZW101_CMD_READ_SYS_PARA          = 0x0F, /**< 读模组基本参数 */
    ZW101_CMD_READ_INFO_PAGE         = 0x16, /**< 读 Flash 信息页 */
    ZW101_CMD_VALID_TEMPLATE_NUM     = 0x1D, /**< 读有效模板个数 */
    ZW101_CMD_READ_INDEX_TABLE       = 0x1F, /**< 读索引表 */
    ZW101_CMD_GET_ENROLL_IMAGE       = 0x29, /**< 注册用获取图像 */
    ZW101_CMD_READ_ADD_PARA          = 0x62, /**< 获取模组附加参数 */

    /* --- 模块指令集 --- */
    ZW101_CMD_CANCEL                 = 0x30, /**< 取消自动注册/验证 */
    ZW101_CMD_AUTO_ENROLL            = 0x31, /**< 自动注册模板 (一站式) */
    ZW101_CMD_AUTO_IDENTIFY          = 0x32, /**< 自动验证指纹 (一站式) */
    ZW101_CMD_SLEEP                  = 0x33, /**< 进入休眠模式 */

    /* --- 维护类指令 --- */
    ZW101_CMD_HANDSHAKE              = 0x35, /**< 握手指令 */
    ZW101_CMD_CHECK_SENSOR           = 0x36, /**< 校验传感器 */
    ZW101_CMD_RGB_CTRL               = 0x3C, /**< RGB 灯控 */
    ZW101_CMD_SET_PWD                = 0x12, /**< 设置口令 */
    ZW101_CMD_VFY_PWD                = 0x13, /**< 验证口令 */
    ZW101_CMD_GET_CHIP_SN            = 0x37, /**< 获取芯片唯一序列号 */
    ZW101_CMD_CHECK_FINGER           = 0x9D, /**< 检测手指 (活体检测) */

    /* --- 自定义扩展 --- */
    ZW101_CMD_READ_ENROLL_PARA       = 0x63, /**< V5 版本读注册比对参数 */
    ZW101_CMD_WRITE_EM_PARA          = 0x63, /**< V5 版本写注册比对参数 */
} zw101_cmd_t;

/* ==================== 确认码 / 错误码枚举 ==================== */

typedef enum {
    ZW101_ACK_SUCCESS                = 0x00, /**< 指令执行成功 */
    ZW101_ACK_ERR_PKG_RCV            = 0x01, /**< 数据包接收错误 */
    ZW101_ACK_ERR_NO_FINGER          = 0x02, /**< 传感器上无手指 */
    ZW101_ACK_ERR_GET_IMG            = 0x03, /**< 录入指纹图像失败 */
    ZW101_ACK_ERR_FP_TOO_DRY         = 0x04, /**< 指纹图像太干/太淡 */
    ZW101_ACK_ERR_FP_TOO_WET         = 0x05, /**< 指纹图像太湿/太糊 */
    ZW101_ACK_ERR_FP_DISORDER        = 0x06, /**< 指纹图像太乱 */
    ZW101_ACK_ERR_LITTLE_FEATURE     = 0x07, /**< 特征点太少 (或面积太小) */
    ZW101_ACK_ERR_NOT_MATCH          = 0x08, /**< 指纹不匹配 */
    ZW101_ACK_ERR_NOT_SEARCHED       = 0x09, /**< 未搜索到指纹 */
    ZW101_ACK_ERR_MERGE              = 0x0A, /**< 特征合并失败 */
    ZW101_ACK_ERR_ADDRESS_OVER       = 0x0B, /**< 地址序号超出指纹库范围 */
    ZW101_ACK_ERR_READ_TEMPLATE      = 0x0C, /**< 读模板出错或无效 */
    ZW101_ACK_ERR_UP_TEMP            = 0x0D, /**< 上传特征失败 */
    ZW101_ACK_ERR_RECV               = 0x0E, /**< 不能接收后续数据包 */
    ZW101_ACK_ERR_UP_IMG             = 0x0F, /**< 上传图像失败 */
    ZW101_ACK_ERR_DEL_TEMP           = 0x10, /**< 删除模板失败 */
    ZW101_ACK_ERR_CLEAR_TEMP         = 0x11, /**< 清空指纹库失败 */
    ZW101_ACK_ERR_SLEEP              = 0x12, /**< 不能进入低功耗状态 */
    ZW101_ACK_ERR_INVALID_PASSWORD   = 0x13, /**< 口令不正确 */
    ZW101_ACK_ERR_RESET              = 0x14, /**< 系统复位失败 */
    ZW101_ACK_ERR_INVALID_IMAGE      = 0x15, /**< 缓冲区内没有有效原始图 */
    ZW101_ACK_ERR_HANGOVER           = 0x17, /**< 残留指纹 / 手指未移动过 */
    ZW101_ACK_ERR_FLASH              = 0x18, /**< 读写 Flash 出错 */
    ZW101_ACK_ERR_INVALID_REG        = 0x1A, /**< 无效寄存器号 */
    ZW101_ACK_ERR_REG_SET            = 0x1B, /**< 寄存器设定内容错误 */
    ZW101_ACK_ERR_NOTEPAD_PAGE       = 0x1C, /**< 记事本页码指定错误 */
    ZW101_ACK_ERR_ENROLL             = 0x1E, /**< 自动注册失败 */
    ZW101_ACK_ERR_LIB_FULL           = 0x1F, /**< 指纹库已满 */
    ZW101_ACK_ERR_DEVICE_ADDR        = 0x20, /**< 设备地址错误 */
    ZW101_ACK_ERR_MUST_VERIFY_PWD    = 0x21, /**< 密码有误 */
    ZW101_ACK_ERR_TMPL_NOT_EMPTY     = 0x22, /**< 指纹模板非空 */
    ZW101_ACK_ERR_TMPL_EMPTY         = 0x23, /**< 指纹模板为空 */
    ZW101_ACK_ERR_LIB_EMPTY          = 0x24, /**< 指纹库为空 */
    ZW101_ACK_ERR_TMPL_NUM           = 0x25, /**< 录入次数设置错误 */
    ZW101_ACK_ERR_TIME_OUT           = 0x26, /**< 超时 */
    ZW101_ACK_ERR_FP_DUPLICATION     = 0x27, /**< 指纹已存在 */
    ZW101_ACK_ERR_TMP_RELATION       = 0x28, /**< 指纹模板有关联 */
    ZW101_ACK_ERR_CHECK_SENSOR       = 0x29, /**< 传感器初始化失败 */
    ZW101_ACK_ERR_MOD_INF_NOT_EMPTY  = 0x2A, /**< 模组信息非空 */
    ZW101_ACK_ERR_MOD_INF_EMPTY      = 0x2B, /**< 模组信息为空 */
    ZW101_ACK_ERR_ENROLL_CANCEL      = 0x2C, /**< 取消注册 */
    ZW101_ACK_ERR_IMAGE_SMALL        = 0x33, /**< 图像面积太小 */
    ZW101_ACK_ERR_IMAGE_UNAVAILABLE  = 0x34, /**< 图像不可用 */
    ZW101_ACK_ERR_ILLEGAL_DATA       = 0x35, /**< 非法数据 */
    ZW101_ACK_ERR_ENROLL_TIMES       = 0x40, /**< 注册次数不够 */
} zw101_ack_t;

/* ==================== RGB 灯控枚举 ==================== */

/** @brief RGB 灯效功能码 */
typedef enum {
    ZW101_RGB_BREATH          = 0x01, /**< 呼吸灯 */
    ZW101_RGB_FLICK           = 0x02, /**< 闪烁 */
    ZW101_RGB_OPEN            = 0x03, /**< 常亮 */
    ZW101_RGB_OFF             = 0x04, /**< 关闭 */
    ZW101_RGB_GRADUAL_OPEN    = 0x05, /**< 渐亮 */
    ZW101_RGB_GRADUAL_OFF     = 0x06, /**< 渐灭 */
    ZW101_RGB_HORSE           = 0x07, /**< 跑马灯 */
} zw101_rgb_func_t;

/** @brief RGB 颜色组合 */
typedef enum {
    ZW101_RGB_COLOR_OFF       = 0x00, /**< 全灭 */
    ZW101_RGB_COLOR_B         = 0x01, /**< 蓝色 */
    ZW101_RGB_COLOR_G         = 0x02, /**< 绿色 */
    ZW101_RGB_COLOR_GB        = 0x03, /**< 青 (绿+蓝) */
    ZW101_RGB_COLOR_R         = 0x04, /**< 红色 */
    ZW101_RGB_COLOR_RB        = 0x05, /**< 紫 (红+蓝) */
    ZW101_RGB_COLOR_RG        = 0x06, /**< 黄 (红+绿) */
    ZW101_RGB_COLOR_RGB       = 0x07, /**< 白 (红+绿+蓝) */
    ZW101_RGB_COLOR_RG_LOOP   = 0x16, /**< 红绿循环 */
    ZW101_RGB_COLOR_RB_LOOP   = 0x15, /**< 红蓝循环 */
    ZW101_RGB_COLOR_GB_LOOP   = 0x13, /**< 绿蓝循环 */
    ZW101_RGB_COLOR_RGB_LOOP  = 0x17, /**< 七彩循环 */
    ZW101_RGB_COLOR_RG_ALTER  = 0x26, /**< 红绿交替 */
    ZW101_RGB_COLOR_RB_ALTER  = 0x25, /**< 红蓝交替 */
    ZW101_RGB_COLOR_GB_ALTER  = 0x23, /**< 绿蓝交替 */
} zw101_rgb_color_t;

/** @brief RGB 占空比 (亮度) */
typedef enum {
    ZW101_RGB_DUTY_SUCCESS    = 0x91, /**< 成功提示: 9:1 亮灭比 */
    ZW101_RGB_DUTY_NORMAL     = 0x11, /**< 正常提示: 1:1 亮灭比 */
} zw101_rgb_duty_t;

/* ==================== 接收状态机 ==================== */

typedef enum {
    ZW101_RCV_STATE_HEADER1,      /**< 等待包头第一字节 0xEF */
    ZW101_RCV_STATE_HEADER2,      /**< 等待包头第二字节 0x01 */
    ZW101_RCV_STATE_HEADER_REST,  /**< 接收剩余固定头 (地址4 + 标识1 + 长度2) */
    ZW101_RCV_STATE_PAYLOAD,      /**< 接收负载数据 */
} zw101_rcv_state_t;

/* ==================== 数据包结构体 ==================== */

#pragma pack(push, 1)

/**
 * @brief 指纹模组通用数据包包头 (可变长度)
 *
 * 内存布局:
 *   [0-1]  header     — 固定 0xEF01
 *   [2-5]  dev_addr   — 设备地址 (大端)
 *   [6]    pkg_type   — 包标识 (0x01 命令, 0x02 数据, 0x07 应答, 0x08 结束)
 *   [7-8]  data_len   — 负载长度 (大端, 不含自身, 包含校验和)
 *   [9+]   payload    — 可变: 命令包为 [cmd|params...],
 *                        应答包为 [ack|ret...],
 *                        数据包/结束包为 [data...]
 *   末尾 2 字节        — 校验和 (从 pkg_type 开始累加到 checksum 前)
 */
typedef struct {
    uint16_t header;            /**< 包头 0xEF01 */
    uint32_t dev_addr;          /**< 设备地址, 默认 0xFFFFFFFF */
    uint8_t  pkg_type;          /**< 包标识: 0x01/0x02/0x07/0x08 */
    uint16_t data_len;          /**< 负载长度 (含校验和) */
    uint8_t  payload[1];        /**< 柔性数组: 可变长度负载 */
} zw101_pkg_t;

#pragma pack(pop)

/** @brief 命令包固定部分大小 (包头到指令码之前) */
#define ZW101_CMD_PKG_FIXED_SIZE  (sizeof(zw101_pkg_t) - 1)

/**
 * @brief 计算数据包最大总长度
 * @param payload_len 负载长度 (不含校验和)
 * @return 总字节数 = 固定头(9) + payload_len + 校验和(2)
 */
#define ZW101_PKG_TOTAL_LEN(payload_len)  ((payload_len) + ZW101_CMD_PKG_FIXED_SIZE + 2u)

/* ==================== 系统参数表结构体 ==================== */

/**
 * @brief 模组基本参数 (对应 PS_ReadSysPara 返回的 16 字节)
 *
 * 参考《用户手册》表 3-34
 */
#pragma pack(push, 1)
typedef struct {
    uint16_t enroll_times;      /**< 注册次数 */
    uint16_t temp_size;         /**< 指纹模板大小 (字节) */
    uint16_t db_size;           /**< 指纹库容量 */
    uint16_t score_level;       /**< 分数等级 (1~5) */
    uint32_t device_addr;       /**< 设备地址 */
    uint16_t pkt_size_code;     /**< 数据包大小代码 (2 = 128bytes) */
    uint16_t baud_rate_n;       /**< 波特率系数 (波特率 = N * 9600) */
} zw101_sys_para_t;
#pragma pack(pop)

/**
 * @brief 模组附加参数 (对应 PS_ReadAddPara 返回)
 *
 * 参考《用户手册》表 3-47
 */
#pragma pack(push, 1)
typedef struct {
    uint16_t sensor_width;      /**< 传感器图像宽 */
    uint16_t sensor_height;     /**< 传感器图像高 */
    uint16_t led_type;          /**< LED 类型: 0=无, 1=RGB, 2=蓝, 3=红, 4=绿... */
    uint16_t tmp_per_fp;        /**< 每枚指纹的模板数 */
} zw101_add_para_t;
#pragma pack(pop)

/* ==================== 公开 API ==================== */

/**
 * @brief 计算协议校验和 (从包标识到校验和前一个字节)
 *
 * 校验和 = 从 pkg_type 偏移开始, 累加 data_len+1 个字节.
 * 结果取低 16 位。
 *
 * @param  data 完整数据包首地址
 * @param  total_len 数据包总长度
 * @return 16 位校验和
 */
uint16_t zw101_calc_checksum(const uint8_t *data, uint16_t total_len);

/**
 * @brief 构建命令包 (仅填充固定字段, 调用方负责发送)
 *
 * @param  buf        输出缓冲区 (至少 cmd_payload_len + 11 字节)
 * @param  dev_addr   设备地址
 * @param  cmd        指令码
 * @param  params     参数字节数组 (可为 NULL)
 * @param  param_len  参数长度 (字节)
 * @return 命令包总长度
 */
uint16_t zw101_build_cmd_pkg(uint8_t *buf, uint32_t dev_addr,
                             uint8_t cmd, const uint8_t *params, uint16_t param_len);

/**
 * @brief 读取系统参数表 (16 字节) — 从应答负载中解析
 *
 * @param  ack_payload  应答包负载首地址 (即 ack 码后的第一个字节)
 * @param  payload_len  负载长度
 * @param  para_out     输出参数表
 * @return true=解析成功, false=长度不符
 */
bool zw101_parse_sys_para(const uint8_t *ack_payload, uint16_t payload_len,
                          zw101_sys_para_t *para_out);

/**
 * @brief 解析读有效模板个数应答
 *
 * @param  ack_payload  应答负载
 * @param  payload_len  负载长度
 * @param  count_out    有效模板数量
 * @return true=成功
 */
bool zw101_parse_valid_count(const uint8_t *ack_payload, uint16_t payload_len,
                             uint16_t *count_out);

/**
 * @brief 解析搜索指纹 (1:N) 应答
 *
 * @param  ack_payload  应答负载
 * @param  payload_len  负载长度
 * @param  page_id_out  匹配到的 PageID
 * @param  score_out    匹配得分
 * @return true=搜索到, false=未搜索到
 */
bool zw101_parse_search_result(const uint8_t *ack_payload, uint16_t payload_len,
                               uint16_t *page_id_out, uint16_t *score_out);

/**
 * @brief 获取确认码的可读描述字符串
 * @param  ack 确认码
 * @return 描述字符串 (静态)
 */
const char *zw101_ack_to_string(uint8_t ack);

/**
 * @brief 将错误码转换为可读字符串
 * @param  err 确认码
 * @return 英文/中文描述
 */
const char *zw101_err_to_string(uint8_t err);

#ifdef __cplusplus
}
#endif

#endif /* ZW101_FP_PROTOCOL_H */
