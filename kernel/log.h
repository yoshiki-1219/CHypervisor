#pragma once
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "serial.h"

#ifdef __cplusplus
extern "C" { 
#endif

typedef enum {
    KLOG_DEBUG = 0,
    KLOG_INFO  = 1,
    KLOG_WARN  = 2,
    KLOG_ERROR = 3,
} klog_level_t;

typedef struct {
    klog_level_t level;      /* これ未満のレベルは出力しない */
} klog_options_t;

/* 初期化：必ずシリアル初期化（serial_init）が先 */
void klog_init(serial_device_t* dev, klog_options_t opt);
/* ランタイムでログレベルを変える場合 */
void klog_set_level(klog_level_t level);

/* 低レベル API（printf 互換）。scope は任意（NULL 可） */
void klog_logf(klog_level_t level, const char* scope, const char* fmt, ...);
void klog_vlogf(klog_level_t level, const char* scope, const char* fmt, va_list ap);

/* 使いやすいマクロ（Zig の std.log.* 相当） */
#define KLOG_DEBUG(scope, fmt, ...) klog_logf(KLOG_DEBUG, (scope), (fmt), ##__VA_ARGS__)
#define KLOG_INFO(scope,  fmt, ...) klog_logf(KLOG_INFO,  (scope), (fmt), ##__VA_ARGS__)
#define KLOG_WARN(scope,  fmt, ...) klog_logf(KLOG_WARN,  (scope), (fmt), ##__VA_ARGS__)
#define KLOG_ERROR(scope, fmt, ...) klog_logf(KLOG_ERROR, (scope), (fmt), ##__VA_ARGS__)

#ifdef __cplusplus
}
#endif
