#include "file.h"
#include "rules.h"
#include <assert.h>
#include <bits/time.h>
#include <errno.h>
#include <linux/limits.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/timerfd.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <stdint.h>
#include <dirent.h>
#include "app.h"

static void error_show(const config_error_t config_err) {
  if (config_err.kind != CONFIG_OK) {
    if (config_err.sys_errno != 0) {
      fprintf(stderr, 
          "A system error occurred while parsing the config: %s\n",
          strerror(config_err.sys_errno));
    }
    else {
      fprintf(stderr, 
          "An error happened while parsing the config: %s\n",
          rules_config_strerror(config_err.kind));
    }
  }
}

int app_setup(AppCtx *app) {
  assert(app);

  CanonicalPath config_dir = {0};
  if (file_config_dir_get(&config_dir) != 0) return -1;

  CanonicalPath config = {0};
  if (snprintf(config.s, PATH_MAX, "%s/config.toml", config_dir.s) == -1) return -1;

  config_error_t config_err = rules_build(&app->rules, config.s); 
  if (config_err.kind != CONFIG_OK) {
    error_show(config_err);
    return -1;
  }

  if (app_handles_open(&app->rules, &app->handles) != 0) return 1;

  return 0;
}

void app_destroy(AppCtx *app) {
  if (!app) return;
  rules_deinit(&app->rules);
  app_handles_close(&app->handles);
}

static int app_file_setup(const CanonicalPath *path, int *fd, dev_t *st_dev) {
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

  uint32_t flags = IN_CLOSE_WRITE | IN_MOVED_TO | IN_MODIFY;

  for (size_t i = 0; i < h->watched_count; i++) {
    h->watched[i].wd = inotify_add_watch(h->inotify_fd, rules->watch_paths[i].path.s, flags);
    if (app_file_setup(&rules->watch_paths[i].path, &h->watched[i].dir.fd, &h->watched[i].dir.st_dev) != 0) goto fail;

    if (h->watched[i].wd == -1) {
      fprintf(stderr, "Cannot watch '%s': %s\n", rules->watch_paths[i].path.s, strerror(errno));
      goto fail;
    }

    h->watched[i].td = app_watch_timer_create(rules->watch_paths[i].settle_sec);

    if (h->watched[i].td == -1) {
      fprintf(stderr, "Cannot set up timer for '%s': %s\n", rules->watch_paths[i].path.s, strerror(errno));
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

void app_handles_close(const Handles *h) {
  for (size_t i = 0; i < h->watched_count; i++) {
    close(h->watched[i].td);
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

int app_file_move(const AppCtx *app, const WatchedDirHandle *origin, const char *fname) {
  assert(app);
  assert(origin);
  assert(fname);

  ssize_t idx = rules_set_path_find_idx(&app->rules, fname);
  if (idx == CONFIG_IGNORE_IDX) return 0;
  return file_move(&origin->dir, &app->handles.dirs[idx], fname);
}

void app_watched_dirs_show(const AppCtx *app) {
  assert(app);
  assert(app->rules.watch_paths);

  for (size_t i = 0; i < app->rules.watch_path_count; i++) {
    printf("Watching: %s/\n", app->rules.watch_paths[i].path.s);
  }
}

int app_watch_timer_arm(const AppCtx *ctx, size_t idx) {
  struct itimerspec timer_spec = {0};
  timer_spec.it_value.tv_sec = ctx->rules.watch_paths[idx].settle_sec;

  if (timerfd_settime(ctx->handles.watched[idx].td, 0, &timer_spec, NULL) == -1) {
    perror("timerfd_settime");
    return -1;
  } 

  return 0;
}

int app_watch_timer_handle(const AppCtx *app, size_t idx) {
  uint64_t exp;
  ssize_t s = read(app->handles.watched[idx].td, &exp, sizeof(exp));

  if (s != sizeof(exp)) {
    perror("read");
    return -1;
  }

  struct dirent **namelist;
  ssize_t entries = scandirat(app->handles.watched[idx].dir.fd, ".", &namelist, NULL, alphasort); 
  if (entries == -1) {
    perror("scandir");
    return -1;
  }

  while (entries--) {
    app_file_move(app, &app->handles.watched[idx], namelist[entries]->d_name);
    free(namelist[entries]);
  }

  free(namelist);

  return 0;
}

void app_events_handle(const AppCtx *app) {
  char buf[sizeof(struct inotify_event) + NAME_MAX + 1] __attribute__((aligned(__alignof__(struct inotify_event))));

  const struct inotify_event *event; 

  ssize_t size;

  for (;;) {
    size = read(app->handles.inotify_fd, buf, sizeof buf);

    if (size == -1 && errno != EAGAIN) {
      perror("read");
      exit(EXIT_FAILURE);
    }

    if (size <= 0) break;

    for (char *ptr = buf; ptr < buf + size; ptr += sizeof(struct inotify_event) + event->len) {
      event = (const struct inotify_event*)ptr;
      if (event->mask & IN_CLOSE_WRITE) printf("IN_CLOSE_WRITE: ");
      if (event->mask & IN_MOVED_TO) printf("IN_MOVED_TO: ");
    }

    for (size_t i = 0; i < app->rules.watch_path_count; ++i) {
      if (app->handles.watched[i].wd == event->wd) {
        printf("%s/", app->rules.watch_paths[i].path.s);
        break;
      }
    }

    if (event->len) printf("%s", event->name);
    if (event->mask & IN_ISDIR) printf(" [directory]\n");
    else printf(" [file]\n");

    for (char *ptr = buf; ptr < buf + size; ptr += sizeof(struct inotify_event) + event->len) {
      event = (const struct inotify_event*)ptr;
      if (event->mask & (IN_MODIFY | IN_CLOSE_WRITE | IN_MOVED_TO)) {
        for (size_t i = 0; i < app->rules.watch_path_count; ++i) {
          if (app->handles.watched[i].wd == event->wd && event->len) {
            app_watch_timer_arm(app, i); // arm the timer on IN_MODIFY | IN_CLOSE_WRITE | IN_MOVED_TO
            // app_file_move(app, &app->handles.watched[i], event->name);
            break;
          }
        }
      }
    }
  }
}

int app_watch_timer_create() {
  int fd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
  if (fd == -1) {
    perror("timerfd_create");
    return -1;
  }

  return fd;
}
