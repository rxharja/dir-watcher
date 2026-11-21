#ifndef APP_H
#define APP_H

#include "file.h"
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

AppStatus app_setup(AppCtx *app);

void app_destroy(AppCtx *app);

AppStatus app_watch_rule_apply(const AppCtx *app, size_t watch_idx, const char *fname);

void app_watched_dirs_show(const AppCtx *app);

void app_events_handle(const AppCtx *app);

AppStatus app_watch_timer_handle(const AppCtx *app, size_t idx);

AppStatus app_watch_timer_arm(const AppCtx *ctx, size_t idx);

void app_status_show(AppStatus s);
#endif
