/* SPDX-License-Identifier: MIT */
#ifndef PK_LOG_H
#define PK_LOG_H

typedef enum pk_log_level
{
    PK_LOG_DEBUG,
    PK_LOG_INFO,
    PK_LOG_WARN,
    PK_LOG_ERROR
} pk_log_level;

void pk_log_set_level(pk_log_level level);
void pk_log(pk_log_level level, const char *fmt, ...);

#define PK_DEBUG(...) pk_log(PK_LOG_DEBUG, __VA_ARGS__)
#define PK_INFO(...)  pk_log(PK_LOG_INFO, __VA_ARGS__)
#define PK_WARN(...)  pk_log(PK_LOG_WARN, __VA_ARGS__)
#define PK_ERROR(...) pk_log(PK_LOG_ERROR, __VA_ARGS__)

#endif
