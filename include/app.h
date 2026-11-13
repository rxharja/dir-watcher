#ifndef APP_H
#define APP_H
#include "dictionary.h"
#include "file.h"
#include "rules.h"

typedef struct {
  int inotify_fd;
  int signal_fd;

  WatchedDirHandle *watched; 
  size_t watched_count;

  DirHandle *dirs;
  size_t dir_count;
} Handles;

typedef struct { 
  Rules rules;
  Handles handles; 
} AppCtx;

int app_handles_open(const Rules* rules, Handles *out);

void app_handles_close(Handles *h);

int app_setup(AppCtx *app);

void app_destroy(AppCtx *app);

int file_move_handle(const AppCtx *app, WatchedDirHandle *origin, const char *target_path);

#endif
