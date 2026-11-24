/* SPDX-License-Identifier: MIT */
#include "core/pk_log.h"

#include <SDL3/SDL.h>
#include <stdarg.h>

static pk_log_level g_level = PK_LOG_INFO;

void pk_log_set_level(pk_log_level level)
{
    g_level = level;
    SDL_SetLogPriority(SDL_LOG_CATEGORY_APPLICATION,
                       level == PK_LOG_DEBUG ? SDL_LOG_PRIORITY_DEBUG : SDL_LOG_PRIORITY_INFO);
}

void pk_log(pk_log_level level, const char *fmt, ...)
{
    char buf[1024];
    va_list ap;
    SDL_LogPriority prio;

    if (level < g_level) {
        return;
    }
    va_start(ap, fmt);
    SDL_vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    switch (level) {
    case PK_LOG_DEBUG:
        prio = SDL_LOG_PRIORITY_DEBUG;
        break;
    case PK_LOG_INFO:
        prio = SDL_LOG_PRIORITY_INFO;
        break;
    case PK_LOG_WARN:
        prio = SDL_LOG_PRIORITY_WARN;
        break;
    default:
        prio = SDL_LOG_PRIORITY_ERROR;
        break;
    }
    SDL_LogMessage(SDL_LOG_CATEGORY_APPLICATION, prio, "%s", buf);
}
