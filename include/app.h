#ifndef APP_H
#define APP_H
#include "dictionary.h"
#include "file.h"

typedef struct {
  Dictionary *by_ext;

  CanonicalPath *dir_paths; 
  size_t dir_count;

  CanonicalPath *watch_paths;
  size_t watch_path_count;

  size_t fallback_idx;
} Rules;

typedef struct {
  int inotify_fd;
  int signal_fd;

  WatchedDirHandle *watched; 
  size_t watched_count;

  DirHandle *dirs;
  size_t dir_count;
} Handles;

typedef struct { Rules rules; Handles handles; } AppCtx;

int app_rules_build(Rules *out, int argc, char **argv);

void app_rules_deinit(Rules *r);

int app_handles_open(const Rules* rules, Handles *out);

void app_handles_close(Handles *h);

int app_setup(AppCtx *app, int argc, char **argv);

void app_destoy(AppCtx *app);

int file_move_handle(const AppCtx *app, DirHandle *origin, const char *target_path);

#endif
