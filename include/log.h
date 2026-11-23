#ifndef LOG_H
#define LOG_H

typedef enum { 
  LOG_LEVEL_ERROR = 0,
  LOG_LEVEL_WARN,
  LOG_LEVEL_INFO,
  LOG_LEVEL_DEBUG 
} LogLevel;

extern LogLevel log_threshold;

int log_init(void);

__attribute__((format(printf, 2, 3)))
void log_msg(LogLevel level, const char *fmt, ...);

// using a macro so that the args don't get evaluated when the level is off
#define LOG_AT(level, ...) \
  do { if ((level) <= log_threshold) log_msg((level), __VA_ARGS__); } while (0)

#define log_error(...) LOG_AT(LOG_LEVEL_ERROR, __VA_ARGS__)
#define log_warn(...)  LOG_AT(LOG_LEVEL_WARN,  __VA_ARGS__)
#define log_info(...)  LOG_AT(LOG_LEVEL_INFO,  __VA_ARGS__)
#define log_debug(...) LOG_AT(LOG_LEVEL_DEBUG, __VA_ARGS__)

#endif // !LOG_H
