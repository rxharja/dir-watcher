#include "dictionary.h"
#include "file.h"
#include "rules.h"
#include <assert.h>
#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/inotify.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdint.h>
#include "app.h"

static void error_show(config_error_t config_err) {
  if (config_err.kind != CONFIG_OK) {
    if (config_err.sys_errno != 0) {
      fprintf(stderr, 
          "An system error occurred while parsing the config: %s",
          strerror(config_err.sys_errno));
    }
    else {
      fprintf(stderr, 
          "An error happened while parsing the config: %s",
          rules_config_strerror(config_err.kind));
    }
  }
}

int app_setup(AppCtx *app) {
  assert(app);

  config_error_t config_err = rules_build(&app->rules, "examples/config.toml"); 
  if (config_err.kind != CONFIG_OK) {
    return -1;
    error_show(config_err);
  }

  if (app_handles_open(&app->rules, &app->handles) != 0) return 1;

  return 0;
}

void app_destroy(AppCtx *app) {
  if (!app) return;
  rules_deinit(&app->rules);
  app_handles_close(&app->handles);
}

static int app_file_setup(CanonicalPath *path, int *fd, dev_t *st_dev) {
  if (file_dir_create(path) != 0) return -1;
  if (file_dir_open(path, fd) != 0) return -1;
  if (file_dir_stat(*fd, st_dev) != 0) return -1;
  return 0;
}

int app_handles_open(const Rules *rules, Handles *h) {
  h->inotify_fd = inotify_init1(IN_CLOEXEC | IN_NONBLOCK);
  if (h->inotify_fd == -1) {
    perror("inotify_init1");
    return -1;
  }

  h->watched_count = rules->watch_path_count;
  h->watched = calloc(h->watched_count, sizeof(*h->watched));
  if (!h->watched) {
    perror("calloc");
    goto fail;
  }

  for (size_t i = 0; i < h->watched_count; i++) {
    h->watched[i].wd = inotify_add_watch(h->inotify_fd, rules->watch_paths[i].s, IN_CLOSE_WRITE | IN_MOVED_TO);
    if (app_file_setup(&rules->watch_paths[i], &h->watched[i].dir.fd, &h->watched[i].dir.st_dev) != 0) goto fail;

    if (h->watched[i].wd == -1) {
      fprintf(stderr, "Cannot watch '%s': %s\n", rules->watch_paths[i].s, strerror(errno));
      goto fail;
    }
  }

  h->dir_count = rules->dir_count;
  h->dirs = calloc(h->dir_count, sizeof(*h->dirs));
  for (size_t i = 0; i < h->dir_count; i++) {
    if (app_file_setup(&rules->dir_paths[i], &h->dirs[i].fd, &h->dirs[i].st_dev) != 0) goto fail;
  }

  return 0;

fail:
  app_handles_close(h);
  return -1;
}

void app_handles_close(Handles *h) {
  for (size_t i = 0; i < h->watched_count; i++) {
    close(h->watched[i].wd);
    close(h->watched[i].dir.fd);
  }

  for (size_t i = 0; i < h->dir_count; i++) {
    close(h->dirs[i].fd); 
  }

  close(h->inotify_fd);
  close(h->signal_fd);

  free(h->watched);
  free(h->dirs);
}

int file_move_handle(const AppCtx *app, WatchedDirHandle *origin, const char *target_path) {
  assert(app);
  assert(origin);
  assert(target_path);
  
  BaseName base_name = {0};

  if (file_basename_build(target_path, &base_name) != 0) {
    perror("file_basename_build");
    return -1;
  }

  void *v = dict_find(app->rules.by_ext, base_name.ext);
  if (v == RULE_EXCLUDE) return 0;
  size_t idx = v ? VAL_TO_IDX(v) : app->rules.fallback_idx;

  return file_move(&origin->dir, &app->handles.dirs[idx], &base_name);
}
