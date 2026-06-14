/**
 * @file logger.h
 * @author nyarukov (luckychaoyue1@gmail.com)
 * @brief 
 * @version 0.1
 * @date 2026-06-03
 * 
 * @copyright Copyright (c) 2026
 * 
 */

#ifndef LOGGER_H
#define LOGGER_H

#include <stdio.h>

#include <stdio.h>

// 日志等级定义
#define LOG_LVL_NONE  0
#define LOG_LVL_ERROR 1
#define LOG_LVL_WARN  2
#define LOG_LVL_INFO  3
#define LOG_LVL_DEBUG 4

#ifndef LOG_LEVEL
    #define LOG_LEVEL LOG_LVL_DEBUG
#endif

#ifndef LOG_TAG
    #define LOG_TAG "LOG"
#endif

#define LOG_PRINT(lvl_char, fmt, ...) \
    printf("[%c][%s] " fmt "\n", lvl_char, LOG_TAG, ##__VA_ARGS__)

#if LOG_LEVEL >= LOG_LVL_ERROR
    #define LOG_E(fmt, ...) LOG_PRINT('E', fmt, ##__VA_ARGS__)
#else
    #define LOG_E(fmt, ...) ((void)0)
#endif

#if LOG_LEVEL >= LOG_LVL_WARN
    #define LOG_W(fmt, ...) LOG_PRINT('W', fmt, ##__VA_ARGS__)
#else
    #define LOG_W(fmt, ...) ((void)0)
#endif

#if LOG_LEVEL >= LOG_LVL_INFO
    #define LOG_I(fmt, ...) LOG_PRINT('I', fmt, ##__VA_ARGS__)
#else
    #define LOG_I(fmt, ...) ((void)0)
#endif

#if LOG_LEVEL >= LOG_LVL_DEBUG
    #define LOG_D(fmt, ...) LOG_PRINT('D', fmt, ##__VA_ARGS__)
#else
    #define LOG_D(fmt, ...) ((void)0)
#endif

#endif /* LOGGER_H */