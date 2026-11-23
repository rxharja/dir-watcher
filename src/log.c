
#include "log.h"
#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

LogLevel log_threshold = LOG_LEVEL_INFO;
static int log_to_journal = 0;

static const char *const level_names[] = {
  [LOG_LEVEL_ERROR] = "ERROR",
  [LOG_LEVEL_WARN]  = "WARN",
  [LOG_LEVEL_INFO]  = "INFO",
  [LOG_LEVEL_DEBUG] = "DEBUG",
};

static const char journal_prio[] = {
  [LOG_LEVEL_ERROR] = '3',
  [LOG_LEVEL_WARN]  = '4',
  [LOG_LEVEL_INFO]  = '6',
  [LOG_LEVEL_DEBUG] = '7',
};

int log_init(void) {
  log_to_journal = !isatty(STDERR_FILENO);
  const char *env = getenv("DIR_WATCHER_LOG");
  if (!env || !*env) return 0;

  for (size_t i = 0; i < sizeof (level_names) / sizeof(*level_names); i++) {
    if (strcasecmp(env, level_names[i]) == 0) {
      log_threshold = (LogLevel)i;
      return 0;
    } 
  }

  return -1;
}

void log_msg(LogLevel level, const char *fmt, ...) {
  assert(level <= LOG_LEVEL_DEBUG);
  if (level > log_threshold) return;

  int saved_errno = errno;

  if (log_to_journal) fprintf(stderr, "<%c>", journal_prio[level]);
  else                fprintf(stderr, "[%s]%*s ", level_names[level], 
                              (int)(5 - strlen(level_names[level])), "");

  va_list(args);
  va_start(args, fmt);
  vfprintf(stderr, fmt, args);
  va_end(args);

  fputc('\n', stderr);

  errno = saved_errno;
}
