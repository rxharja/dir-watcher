#ifndef APP_H
#define APP_H

#include "file.h"
#include "log.h"
#include "rules.h"

typedef enum {
  APP_OK = 0,
  APP_CONFIG,
  APP_FILE,
  APP_SYSTEM
} AppStatusType;

typedef struct {
  AppStatusType kind;
  int sys_errno;
  union {
    ConfigStatus config;
    FileStatus file;
  } u;
  const char *context;
} AppStatus;

typedef struct {
  int inotify_fd;
  int signal_fd;

  WatchHandle *watches; 
  size_t watch_count;

  DirHandle *dirs;
  size_t dir_count;
} Handles;

typedef struct { 
  Config config;
  Handles handles; 
} AppCtx;

typedef struct { 
  size_t watch_idx;
  const Rule *rule;
  const char *fname; // borrowed from the namelist
} Effect;

typedef enum { 
  RECEIPT_UNSET = 0, 
  RECEIPT_MOVED, 
  RECEIPT_RENAMED,
  RECEIPT_DELETED,
  RECEIPT_SKIPPED,
  RECEIPT_VANISHED,
  RECEIPT_REPLACED,
} ReceiptKind;

typedef struct { 
  ReceiptKind kind;
  Effect effect;
  char final_name[NAME_MAX + 1];
} Receipt;

AppStatus app_effect_run(const AppCtx *app, Effect *e, Receipt *out);

void app_effect_describe(const AppCtx *app, Effect effect, LogLevel level);

AppStatus app_setup(AppCtx *app);

void app_destroy(AppCtx *app);

void app_watches_log(const Config *cfg);

AppStatus app_events_handle(const AppCtx *app);

AppStatus app_watch_timer_handle(const AppCtx *app, size_t idx);

AppStatus app_watch_timer_arm(const AppCtx *ctx, size_t idx);

void app_status_show(AppStatus s);
#endif
