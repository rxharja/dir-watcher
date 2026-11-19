#include "file.h"
#include "rules.h"
#include <assert.h>
#include <bits/time.h>
#include <err.h>
#include <errno.h>
#include <linux/limits.h>
#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/signalfd.h>
#include <sys/timerfd.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <stdint.h>
#include <dirent.h>
#include "app.h"

static AppStatus app_status_from_config(ConfigStatus config) {
  if (config.kind == CONFIG_OK) return (AppStatus) {.kind = APP_OK };
  return (AppStatus) { .kind = APP_CONFIG, .u.config = config };
}

static AppStatus app_status_from_file(FileStatus file) {
  if (file.kind == FILE_OK) return (AppStatus) {.kind = APP_OK };
  return (AppStatus) { .kind = APP_FILE, .u.file = file };
}

static AppStatus app_status_from_errno(int err_no, char *context) {
  return (AppStatus) { .kind = APP_SYSTEM, .sys_errno = err_no, .context = context };
}

static AppStatus app_status_ok(void) { return (AppStatus) {.kind = APP_OK }; }

void app_status_show(AppStatus s) {
  switch (s.kind) {
    case APP_OK: break;
    case APP_CONFIG:                     
     config_status_show(s.u.config); 
     break;
    case APP_FILE:                           
     file_status_show(s.u.file); 
     break;
    case APP_SYSTEM: 
     fprintf(stderr, "%s: %s\n", s.context, strerror(s.sys_errno));
     break;
  }
}

static AppStatus app_signal_setup(int *fd) {
  sigset_t mask;
  sigemptyset(&mask);
  sigaddset(&mask, SIGINT);
  sigaddset(&mask, SIGTERM);
  if (sigprocmask(SIG_BLOCK, &mask, NULL) == -1) return app_status_from_errno(errno, "sigprocmask failed");

  *fd = signalfd(-1, &mask, SFD_CLOEXEC | SFD_NONBLOCK);
  if (*fd == -1) return app_status_from_errno(errno, "no signal file descriptor");

  return app_status_ok();
}

static void app_handles_reset(Handles *h) {
  h->inotify_fd = -1;
  h->signal_fd = -1;
  h->watched = NULL; h->watched_count = 0;
  h->dirs    = NULL; h->dir_count = 0;
}


static void app_try_close(const int *fd) { if (*fd >= 0) close(*fd); }
static void app_handles_close(Handles *h) {
  if (h->watched) {
    for (size_t i = 0; i < h->watched_count; i++) {
      app_try_close(&h->watched[i].td);
      app_try_close(&h->watched[i].dir.fd);
    }
  }

  if (h->dirs) {
    for (size_t i = 0; i < h->dir_count; i++) app_try_close(&h->dirs[i].fd); 
  }

  app_try_close(&h->inotify_fd);
  app_try_close(&h->signal_fd);

  free(h->watched);
  free(h->dirs);

  app_handles_reset(h);
}

static AppStatus app_watch_timer_create(int *fd) {
  *fd = -1;
  *fd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
  if (*fd == -1) return app_status_from_errno(errno, "creating the timer");
  return app_status_ok();
}

static AppStatus app_handles_open_watched(int inotify_fd, const WatchedPath *watch_path, WatchedDirHandle *watched) {
  assert(watch_path && watched);
  uint32_t flags = IN_CLOSE_WRITE | IN_MOVED_TO | IN_MODIFY;

  watched->dir.fd = -1; 
  watched->wd = -1; 
  watched->td = -1; 

  FileStatus file_status = file_dir_ensure_open(&watch_path->path, &watched->dir.fd, &watched->dir.st_dev);
  if (file_status.kind != FILE_OK) return app_status_from_file(file_status);

  watched->wd = inotify_add_watch(inotify_fd, watch_path->path.s, flags);
  if (watched->wd == -1) return app_status_from_errno(errno, "add watch"); 

  return app_watch_timer_create(&watched->td);
}

static AppStatus app_handles_open(const Rules *rules, Handles *h) {
  assert(rules && h);

  AppStatus signal_res = app_signal_setup(&h->signal_fd);
  if (signal_res.kind != APP_OK) return signal_res;

  h->inotify_fd = inotify_init1(IN_CLOEXEC | IN_NONBLOCK);
  if (h->inotify_fd == -1) return app_status_from_errno(errno, "start file watcher"); 

  h->watched = calloc(rules->watch_path_count, sizeof(*h->watched));
  if (!h->watched) return app_status_from_errno(errno, "allocate watch list"); 
  h->watched_count = rules->watch_path_count;

  for (size_t i = 0; i < h->watched_count; i++) {
    h->watched[i].wd = -1; 
    h->watched[i].td = -1; 
    h->watched[i].dir.fd = -1; 
  }

  for (size_t i = 0; i < h->watched_count; i++) {
    AppStatus res = app_handles_open_watched(h->inotify_fd, &rules->watch_paths[i], &h->watched[i]);
    if (res.kind != APP_OK) return res;
  }

  h->dirs = calloc(rules->dir_count, sizeof(*h->dirs));
  if (!h->dirs) return app_status_from_errno(errno, "allocate dir list"); 
  h->dir_count = rules->dir_count;

  for (size_t i = 0; i < h->dir_count; i++) h->dirs[i].fd = -1;
  for (size_t i = 0; i < h->dir_count; i++) {
    FileStatus file_res = file_dir_ensure_open(&rules->dir_paths[i], &h->dirs[i].fd, &h->dirs[i].st_dev);
    if (file_res.kind != FILE_OK) return app_status_from_file(file_res);
  }

  return app_status_ok();
}

void app_destroy(AppCtx *app) {
  if (!app) return;
  rules_deinit(&app->rules);
  app_handles_close(&app->handles);
}

AppStatus app_setup(AppCtx *app) {
  assert(app);

  app_handles_reset(&app->handles);
  CanonicalPath config = {0};
  AppStatus status = app_status_from_file(file_config_path_get(&config));
  if (status.kind == APP_OK) status = app_status_from_config(rules_build(&app->rules, config.s));
  if (status.kind == APP_OK) status = app_handles_open(&app->rules, &app->handles);
  if (status.kind != APP_OK) app_destroy(app);
  return status;
}

AppStatus app_file_move(const AppCtx *app, const WatchedDirHandle *origin, const char *fname) {
  assert(app && origin && fname);

  ssize_t idx = rules_set_path_find_idx(&app->rules, fname);
  if (idx == CONFIG_IGNORE_IDX) return app_status_ok();

  return app_status_from_file(file_move(&origin->dir, &app->handles.dirs[idx], fname));
}

void app_watched_dirs_show(const AppCtx *app) {
  assert(app && app->rules.watch_paths);

  for (size_t i = 0; i < app->rules.watch_path_count; i++) {
    printf("Watching: %s/\n", app->rules.watch_paths[i].path.s);
  }
}

AppStatus app_watch_timer_arm(const AppCtx *ctx, size_t idx) {
  assert(ctx);

  struct itimerspec timer_spec = {0};
  timer_spec.it_value.tv_sec = ctx->rules.watch_paths[idx].settle_sec;

  int res = timerfd_settime(ctx->handles.watched[idx].td, 0, &timer_spec, NULL);
  if (res == -1) return app_status_from_errno(errno, "arming the timer");

  return app_status_ok();
}

AppStatus app_watch_timer_handle(const AppCtx *app, size_t idx) {
  uint64_t expirations;
  ssize_t s = read(app->handles.watched[idx].td, &expirations, sizeof(expirations));
  if (s == -1 && errno == EAGAIN) return app_status_ok();
  if (s == -1) return app_status_from_errno(errno, "reading from the timer file descriptor");

  assert(s == sizeof(expirations));

  struct dirent **namelist;
  ssize_t entries = scandirat(app->handles.watched[idx].dir.fd, ".", &namelist, NULL, alphasort); 
  if (entries == -1) return app_status_from_errno(errno, "generating entries with scandirat");

  for (ssize_t i = 0; i < entries; i++) {
    char *fname = namelist[i]->d_name;
    if (strcmp(fname, ".") == 0 || strcmp(fname, "..") == 0) {
      free(namelist[i]);
      continue;
    }

    AppStatus res = app_file_move(app, &app->handles.watched[idx], fname);

    if (res.kind == APP_FILE && res.u.file.kind == FILE_ERR_NOT_FOUND) {/* nothing to do */}
    else if (res.kind == APP_FILE && res.u.file.kind == FILE_ERR_EXISTS) {
      app_status_show(res);
    }
    else if (res.kind != APP_OK) app_status_show(res);
    free(namelist[i]);
  }

  free(namelist);

  return app_status_ok();
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
            break;
          }
        }
      }
    }
  }
}
