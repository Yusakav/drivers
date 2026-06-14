/**
 * @file    zw101_fp_protocol.c
 * @brief   ZW101 指纹模块通信协议层 — 校验和、组包、解析
 * @note    纯数据运算，无平台依赖，无阻塞 I/O
 * @version 1.0
 * @date    2026-06-13
 */

#include "zw101_fp_protocol.h"
#include <string.h>

/* ==================== 校验和计算 ==================== */

/**
 * @brief 计算协议校验和
 *
 * 校验和范围: 从 pkg_type (offset 6) 到 checksum 前一个字节，
 * 共 data_len + 1 个字节。
 * 累加后取低 16 位。
 *
 * @param  data       完整数据包首地址
 * @param  total_len  数据包总长度
 * @return uint16_t   16 位无符号校验和
 */
uint16_t zw101_calc_checksum(const uint8_t *data, uint16_t total_len)
{
    uint16_t sum = 0;
    /* 校验范围: offset 6 到 offset (total_len - 3), 共 total_len - 8 字节 */
    uint16_t calc_len = total_len - 8u;
    for (uint16_t i = ZW101_CALC_SUM_OFFSET; i < ZW101_CALC_SUM_OFFSET + calc_len; i++) {
        sum += data[i];
    }
    return sum;
}

/* ==================== 命令包构建 ==================== */

/**
 * @brief 构建完整的 ZW101 命令包
 *
 * 格式: 0xEF01 | DevAddr(4) | 0x01 | Len(2) | Cmd(1) | Params(N) | Check(2)
 * 其中 Len = 1(cmd) + N(params) + 2(checksum) = N + 3
 *
 * @param  buf        输出缓冲区
 * @param  dev_addr   设备地址 (大端)
 * @param  cmd        指令码
 * @param  params     参数字节 (可为 NULL)
 * @param  param_len  参数长度
 * @return uint16_t   命令包总长度
 */
uint16_t zw101_build_cmd_pkg(uint8_t *buf, uint32_t dev_addr,
                             uint8_t cmd, const uint8_t *params, uint16_t param_len)
{
    uint16_t offset = 0;
    uint16_t data_len = 1u + param_len + 2u; /* cmd(1) + params + checksum(2) */

    /* 包头 */
    buf[offset++] = ZW101_HEADER_FIRST;  /* 0xEF */
    buf[offset++] = ZW101_HEADER_SECOND; /* 0x01 */

    /* 设备地址 (4 字节, 大端) */
    buf[offset++] = (uint8_t)(dev_addr >> 24);
    buf[offset++] = (uint8_t)(dev_addr >> 16);
    buf[offset++] = (uint8_t)(dev_addr >> 8);
    buf[offset++] = (uint8_t)(dev_addr);

    /* 包标识: 命令包 */
    buf[offset++] = ZW101_PKG_CMD;

    /* 包长度 (大端) */
    buf[offset++] = (uint8_t)(data_len >> 8);
    buf[offset++] = (uint8_t)(data_len);

    /* 指令码 */
    buf[offset++] = cmd;

    /* 参数 */
    if (params != NULL && param_len > 0) {
        memcpy(&buf[offset], params, param_len);
        offset += param_len;
    }

    /* 校验和 (2 字节, 大端) */
    uint16_t total_len = offset + 2u;
    uint16_t sum = zw101_calc_checksum(buf, total_len);
    buf[offset++] = (uint8_t)(sum >> 8);
    buf[offset++] = (uint8_t)(sum);

    return offset;
}

/* ==================== 应答解析 ==================== */

/**
 * @brief 解析系统参数表
 *
 * @param  ack_payload  应答负载 (ack 码之后)
 * @param  payload_len  负载总长度
 * @param  para_out     输出参数结构体
 * @return bool         解析是否成功
 */
bool zw101_parse_sys_para(const uint8_t *ack_payload, uint16_t payload_len,
                          zw101_sys_para_t *para_out)
{
    if (ack_payload == NULL || para_out == NULL || payload_len < 17u) {
        return false;
    }
    /* payload 格式: ack(1) + 16bytes 系统参数 + checksum(2) */
    /* 传入的 ack_payload 已跳过 ack 码, 直接是系统参数 */
    para_out->enroll_times  = ((uint16_t)ack_payload[0] << 8)  | ack_payload[1];
    para_out->temp_size     = ((uint16_t)ack_payload[2] << 8)  | ack_payload[3];
    para_out->db_size       = ((uint16_t)ack_payload[4] << 8)  | ack_payload[5];
    para_out->score_level   = ((uint16_t)ack_payload[6] << 8)  | ack_payload[7];
    para_out->device_addr   = ((uint32_t)ack_payload[8] << 24)  |
                              ((uint32_t)ack_payload[9] << 16)  |
                              ((uint32_t)ack_payload[10] << 8)  |
                               (uint32_t)ack_payload[11];
    para_out->pkt_size_code = ((uint16_t)ack_payload[12] << 8) | ack_payload[13];
    para_out->baud_rate_n   = ((uint16_t)ack_payload[14] << 8) | ack_payload[15];
    return true;
}

/**
 * @brief 解析有效模板个数
 *
 * @param  ack_payload  应答负载 (ack 码之后)
 * @param  payload_len  负载总长度
 * @param  count_out    有效模板数量
 * @return bool         解析是否成功
 */
bool zw101_parse_valid_count(const uint8_t *ack_payload, uint16_t payload_len,
                             uint16_t *count_out)
{
    if (ack_payload == NULL || count_out == NULL || payload_len < 3u) {
        return false;
    }
    /* 格式: ack(1) + count(2) + checksum(2) */
    *count_out = ((uint16_t)ack_payload[0] << 8) | ack_payload[1];
    return true;
}

/**
 * @brief 解析搜索指纹 (1:N) 结果
 *
 * @param  ack_payload  应答负载 (ack 码之后)
 * @param  payload_len  负载总长度
 * @param  page_id_out  匹配到的 PageID
 * @param  score_out    匹配得分
 * @return bool         是否搜索到
 */
bool zw101_parse_search_result(const uint8_t *ack_payload, uint16_t payload_len,
                               uint16_t *page_id_out, uint16_t *score_out)
{
    if (ack_payload == NULL || page_id_out == NULL || score_out == NULL || payload_len < 5u) {
        return false;
    }
    /* 格式: ack(1) + page_id(2) + score(2) + checksum(2) */
    *page_id_out = ((uint16_t)ack_payload[0] << 8) | ack_payload[1];
    *score_out   = ((uint16_t)ack_payload[2] << 8) | ack_payload[3];
    return true;
}

/* ==================== 预构建的固定命令包 ==================== */

/**
 * @brief 获取预构建的固定命令包数据 (只读, 不可修改)
 */
const uint8_t *zw101_get_fixed_cmd_get_image(void)
{
    static const uint8_t cmd[] = {
        0xEF, 0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0x01, 0x00, 0x03, 0x01, 0x00, 0x05
    };
    return cmd;
}

const uint8_t *zw101_get_fixed_cmd_enroll_image(void)
{
    static const uint8_t cmd[] = {
        0xEF, 0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0x01, 0x00, 0x03, 0x29, 0x00, 0x2D
    };
    return cmd;
}

const uint8_t *zw101_get_fixed_cmd_reg_model(void)
{
    static const uint8_t cmd[] = {
        0xEF, 0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0x01, 0x00, 0x03, 0x05, 0x00, 0x09
    };
    return cmd;
}

const uint8_t *zw101_get_fixed_cmd_empty(void)
{
    static const uint8_t cmd[] = {
        0xEF, 0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0x01, 0x00, 0x03, 0x0D, 0x00, 0x11
    };
    return cmd;
}

const uint8_t *zw101_get_fixed_cmd_sleep(void)
{
    static const uint8_t cmd[] = {
        0xEF, 0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0x01, 0x00, 0x03, 0x33, 0x00, 0x37
    };
    return cmd;
}

const uint8_t *zw101_get_fixed_cmd_handshake(void)
{
    static const uint8_t cmd[] = {
        0xEF, 0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0x01, 0x00, 0x03, 0x35, 0x00, 0x39
    };
    return cmd;
}

const uint8_t *zw101_get_fixed_cmd_check_sensor(void)
{
    static const uint8_t cmd[] = {
        0xEF, 0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0x01, 0x00, 0x03, 0x36, 0x00, 0x3A
    };
    return cmd;
}

/* ==================== 预构建命令包长度表 ==================== */

uint16_t zw101_get_fixed_cmd_len_get_image(void)    { return 12u; }
uint16_t zw101_get_fixed_cmd_len_enroll_image(void) { return 12u; }
uint16_t zw101_get_fixed_cmd_len_reg_model(void)    { return 12u; }
uint16_t zw101_get_fixed_cmd_len_empty(void)        { return 12u; }
uint16_t zw101_get_fixed_cmd_len_sleep(void)        { return 12u; }
uint16_t zw101_get_fixed_cmd_len_handshake(void)    { return 12u; }
uint16_t zw101_get_fixed_cmd_len_check_sensor(void) { return 12u; }

/* ==================== 错误码描述 ==================== */

/**
 * @brief 确认码转可读字符串
 */
const char *zw101_ack_to_string(uint8_t ack)
{
    switch (ack) {
    case ZW101_ACK_SUCCESS:             return "OK";
    case ZW101_ACK_ERR_PKG_RCV:         return "PKG_RCV_ERR";
    case ZW101_ACK_ERR_NO_FINGER:       return "NO_FINGER";
    case ZW101_ACK_ERR_GET_IMG:         return "GET_IMG_ERR";
    case ZW101_ACK_ERR_FP_TOO_DRY:      return "TOO_DRY";
    case ZW101_ACK_ERR_FP_TOO_WET:      return "TOO_WET";
    case ZW101_ACK_ERR_FP_DISORDER:     return "DISORDER";
    case ZW101_ACK_ERR_LITTLE_FEATURE:  return "FEW_FEATURE";
    case ZW101_ACK_ERR_NOT_MATCH:       return "NOT_MATCH";
    case ZW101_ACK_ERR_NOT_SEARCHED:    return "NOT_FOUND";
    case ZW101_ACK_ERR_MERGE:           return "MERGE_FAIL";
    case ZW101_ACK_ERR_ADDRESS_OVER:    return "ADDR_OVERFLOW";
    case ZW101_ACK_ERR_READ_TEMPLATE:   return "READ_TMPL_ERR";
    case ZW101_ACK_ERR_UP_TEMP:         return "UPLOAD_ERR";
    case ZW101_ACK_ERR_RECV:            return "RECV_ERR";
    case ZW101_ACK_ERR_UP_IMG:          return "UP_IMG_ERR";
    case ZW101_ACK_ERR_DEL_TEMP:        return "DEL_ERR";
    case ZW101_ACK_ERR_CLEAR_TEMP:      return "CLEAR_ERR";
    case ZW101_ACK_ERR_SLEEP:           return "SLEEP_ERR";
    case ZW101_ACK_ERR_INVALID_PASSWORD:return "BAD_PWD";
    case ZW101_ACK_ERR_RESET:           return "RESET_ERR";
    case ZW101_ACK_ERR_INVALID_IMAGE:   return "NO_VALID_IMG";
    case ZW101_ACK_ERR_HANGOVER:        return "FINGER_NOT_MOVED";
    case ZW101_ACK_ERR_FLASH:           return "FLASH_ERR";
    case ZW101_ACK_ERR_LIB_FULL:        return "LIB_FULL";
    case ZW101_ACK_ERR_TMPL_NOT_EMPTY:  return "TMPL_EXISTS";
    case ZW101_ACK_ERR_TMPL_EMPTY:      return "TMPL_EMPTY";
    case ZW101_ACK_ERR_LIB_EMPTY:       return "LIB_EMPTY";
    case ZW101_ACK_ERR_TIME_OUT:        return "TIMEOUT";
    case ZW101_ACK_ERR_FP_DUPLICATION:  return "DUPLICATE";
    case ZW101_ACK_ERR_CHECK_SENSOR:    return "SENSOR_FAIL";
    case ZW101_ACK_ERR_ENROLL_CANCEL:   return "ENROLL_CANCEL";
    case ZW101_ACK_ERR_IMAGE_SMALL:     return "IMG_TOO_SMALL";
    case ZW101_ACK_ERR_IMAGE_UNAVAILABLE: return "IMG_UNAVAILABLE";
    case ZW101_ACK_ERR_ENROLL_TIMES:    return "ENROLL_TIMES_INSF";
    default:                            return "UNKNOWN";
    }
}

/**
 * @brief 错误码转中文描述
 * @param err 确认码
 * @return const char* 中文描述
 */
const char *zw101_err_to_string(uint8_t err)
{
    switch (err) {
    case ZW101_ACK_SUCCESS:             return "成功";
    case ZW101_ACK_ERR_PKG_RCV:         return "数据包接收错误";
    case ZW101_ACK_ERR_NO_FINGER:       return "传感器上无手指";
    case ZW101_ACK_ERR_GET_IMG:         return "录入图像失败";
    case ZW101_ACK_ERR_FP_TOO_DRY:      return "指纹太干/太淡";
    case ZW101_ACK_ERR_FP_TOO_WET:      return "指纹太湿/太糊";
    case ZW101_ACK_ERR_FP_DISORDER:     return "指纹图像太乱";
    case ZW101_ACK_ERR_LITTLE_FEATURE:  return "特征点太少(面积太小)";
    case ZW101_ACK_ERR_NOT_MATCH:       return "指纹不匹配";
    case ZW101_ACK_ERR_NOT_SEARCHED:    return "未搜索到指纹";
    case ZW101_ACK_ERR_MERGE:           return "特征合并失败";
    case ZW101_ACK_ERR_ADDRESS_OVER:    return "地址超出指纹库范围";
    case ZW101_ACK_ERR_READ_TEMPLATE:   return "读模板出错或无效";
    case ZW101_ACK_ERR_UP_TEMP:         return "上传特征失败";
    case ZW101_ACK_ERR_RECV:            return "不能接收后续数据包";
    case ZW101_ACK_ERR_UP_IMG:          return "上传图像失败";
    case ZW101_ACK_ERR_DEL_TEMP:        return "删除模板失败";
    case ZW101_ACK_ERR_CLEAR_TEMP:      return "清空指纹库失败";
    case ZW101_ACK_ERR_SLEEP:           return "不能进入低功耗状态";
    case ZW101_ACK_ERR_INVALID_PASSWORD:return "口令不正确";
    case ZW101_ACK_ERR_RESET:           return "系统复位失败";
    case ZW101_ACK_ERR_INVALID_IMAGE:   return "无有效原始图";
    case ZW101_ACK_ERR_HANGOVER:        return "残留指纹/手指未移动";
    case ZW101_ACK_ERR_FLASH:           return "Flash读写错误";
    case ZW101_ACK_ERR_LIB_FULL:        return "指纹库已满";
    case ZW101_ACK_ERR_TMPL_NOT_EMPTY:  return "指纹模板非空";
    case ZW101_ACK_ERR_TMPL_EMPTY:      return "指纹模板为空";
    case ZW101_ACK_ERR_LIB_EMPTY:       return "指纹库为空";
    case ZW101_ACK_ERR_TIME_OUT:        return "超时";
    case ZW101_ACK_ERR_FP_DUPLICATION:  return "指纹已存在";
    case ZW101_ACK_ERR_CHECK_SENSOR:    return "传感器初始化失败";
    case ZW101_ACK_ERR_ENROLL_CANCEL:   return "注册被取消";
    case ZW101_ACK_ERR_IMAGE_SMALL:     return "图像面积太小";
    case ZW101_ACK_ERR_IMAGE_UNAVAILABLE: return "图像不可用";
    case ZW101_ACK_ERR_ENROLL_TIMES:    return "注册次数不够";
    default:                            return "未知错误";
    }
}
