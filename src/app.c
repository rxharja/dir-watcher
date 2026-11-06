#include "dictionary.h"
#include "file.h"
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

int app_setup(AppCtx *app, int argc, char **argv) {
  assert(app);

  if (app_rules_build(&app->rules, argc, argv) != 0) return -1;
  if (app_handles_open(&app->rules, &app->handles) != 0) return 1;

  return 0;
}

void app_destoy(AppCtx *app) {
  if (!app) return;
  app_rules_deinit(&app->rules);
  app_handles_close(&app->handles);
}

int app_rules_build(Rules *out, int argc, char **argv) {
  assert(out);

  // 3 test dirs and 1 default
  out->dir_count = 3 + 1;
  out->dir_paths = calloc(out->dir_count, sizeof(CanonicalPath));
  if(!out->dir_paths) goto fail;

  out->watch_path_count = 1;
  out->watch_paths = calloc(out->watch_path_count, sizeof(CanonicalPath));
  if(!out->watch_paths) goto fail;

  out->by_ext = dict_init();
  if (!out->by_ext) goto fail;

  // default
  out->fallback_idx = 0;
  out->dir_paths[out->fallback_idx] = (CanonicalPath){ .s = "/home/redonxharja/projects/dir-watcher/output" };

  // todo: normalize dirs properly here
  char *exts[4] = {"", ".pdf", ".zip", ".txt" };
  char *dirs[4] = {"", "./pdfs", "./zips", "./configs" };
  
  for (size_t i = 1; i < out->dir_count; i++) {
    // convert relative to absolute, ENOENT is not handled here
    if (!realpath(dirs[i], out->dir_paths[i].s) && errno != ENOENT) goto fail;
    if(dict_insert(out->by_ext, exts[i], IDX_TO_VAL(i)) != 1) goto fail;
  }

  // set up watch paths
  out->watch_path_count = argc - 1;
  for (size_t i = 0; i < out->watch_path_count; i++) {
    if (!realpath(argv[i+1], out->watch_paths[i].s) && errno != ENOENT) goto fail;
  }

  // test dict_insert for exclusion
  dict_insert(out->by_ext, ".part", RULE_EXCLUDE);

  return 0;

fail:
  app_rules_deinit(out);
  return -1;
}

void kvp_key_free(void *p) {
  Kvp *kvp = p;
  free((void*)kvp->key); 
}

void app_rules_deinit(Rules *out) {
  if (!out) return;
  free(out->dir_paths);
  free(out->watch_paths);
  if(out->by_ext) dict_free(out->by_ext, kvp_key_free);
}


int app_file_setup(CanonicalPath *path, int *fd, dev_t *st_dev) {
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

int file_move_handle(const AppCtx *app, DirHandle *origin, const char *target_path) {
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

  return file_move(origin, &app->handles.dirs[idx], &base_name);
}
