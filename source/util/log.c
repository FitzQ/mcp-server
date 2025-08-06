
#include <stdio.h>
#include "log.h"
#include <stdarg.h>
#include <string.h>
#include <switch/services/time.h>
#include <time.h>
#include <switch.h>


static Mutex log_mutex = 0;
#define LOG_FILE_PATH "/atmosphere/logs/mcp-server.log"
static FILE *log_file = NULL;

static char *cur_time() {
    u64 timestamp = 0;
    timeGetCurrentTime(TimeType_LocalSystemClock, &timestamp);
    // Convert to UTC+8 by adding 8*3600 seconds
    time_t t = (time_t)timestamp + 8 * 3600;
    struct tm *tm_ptr = gmtime(&t);
    int hours = tm_ptr->tm_hour;
    int minutes = tm_ptr->tm_min;
    int seconds = tm_ptr->tm_sec;
    int day = tm_ptr->tm_mday;
    int month = tm_ptr->tm_mon;
    int year = tm_ptr->tm_year + 1900;
    // Format time as "YYYY-MM-DD HH:MM:SS"
    static char timebuf[64];
    snprintf(timebuf, sizeof(timebuf), "%04i-%02i-%02i %02i:%02i:%02i",
             year, month + 1, day, hours, minutes, seconds);
    return timebuf;
}

static void log_write(const char *level, const char *file, int line, const char *fmt, va_list args) {
    mutexLock(&log_mutex);
    if (!log_file) {
        log_file = fopen(LOG_FILE_PATH, "a");
        if (!log_file) {
            mutexUnlock(&log_mutex);
            return;
        }
    }
    // 只打印file名最后20个字符
    const char *short_file = file;
    size_t file_len = strlen(file);
    if (file_len > 20) {
        short_file = file + file_len - 20;
    }
    fprintf(log_file, "%s [%s:%d] [%s] ", cur_time(), short_file, line, level);
    vfprintf(log_file, fmt, args);
    fprintf(log_file, "\n");
    fflush(log_file);
    mutexUnlock(&log_mutex);
}



void log_info_impl(const char *file, int line, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    log_write("INFO", file, line, fmt, args);
    va_end(args);
}

void log_warning_impl(const char *file, int line, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    log_write("WARNING", file, line, fmt, args);
    va_end(args);
}

void log_error_impl(const char *file, int line, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    log_write("ERROR", file, line, fmt, args);
    va_end(args);
}

void log_debug_impl(const char *file, int line, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    log_write("DEBUG", file, line, fmt, args);
    va_end(args);
}