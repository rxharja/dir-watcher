#include "file.h"
#include "rules.h"
#include <assert.h>
#include <bits/time.h>
#include <err.h>
#include <errno.h>
#include <fnmatch.h>
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

static AppStatus app_status_from_sys(int err_no, char *context) {
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
  if (sigprocmask(SIG_BLOCK, &mask, NULL) == -1) return app_status_from_sys(errno, "sigprocmask failed");

  *fd = signalfd(-1, &mask, SFD_CLOEXEC | SFD_NONBLOCK);
  if (*fd == -1) return app_status_from_sys(errno, "no signal file descriptor");

  return app_status_ok();
}

static void app_handles_reset(Handles *h) {
  h->inotify_fd = -1;
  h->signal_fd = -1;
  h->watches = NULL; h->watch_count = 0;
  h->dirs    = NULL; h->dir_count = 0;
}


static void app_try_close(const int *fd) { if (*fd >= 0) close(*fd); }
static void app_handles_close(Handles *h) {
  if (h->watches) {
    for (size_t i = 0; i < h->watch_count; i++) app_try_close(&h->watches[i].td);
  }

  if (h->dirs) {
    for (size_t i = 0; i < h->dir_count; i++) app_try_close(&h->dirs[i].fd); 
  }

  app_try_close(&h->inotify_fd);
  app_try_close(&h->signal_fd);

  free(h->watches);
  free(h->dirs);
  app_handles_reset(h);
}

static AppStatus app_watch_timer_create(int *fd) {
  *fd = -1;
  *fd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
  if (*fd == -1) return app_status_from_sys(errno, "creating the timer");
  return app_status_ok();
}

static AppStatus app_handles_open_watched(int inotify_fd, const Config *cfg, const Watch *w, WatchHandle *h) {
  assert(cfg && w && h);

  uint32_t flags = IN_CLOSE_WRITE | IN_MOVED_TO | IN_MODIFY;
  *h = (WatchHandle){.wd = -1, .td = -1};
  const CanonicalPath *path = config_watch_path(cfg, w);

  h->wd = inotify_add_watch(inotify_fd, path->s, flags);
  if (h->wd == -1) return app_status_from_sys(errno, "add watch"); 

  return app_watch_timer_create(&h->td);
}

static AppStatus app_handles_open(const Config *cfg, Handles *h) {
  assert(cfg && h);

  AppStatus signal_res = app_signal_setup(&h->signal_fd);
  if (signal_res.kind != APP_OK) return signal_res;

  h->inotify_fd = inotify_init1(IN_CLOEXEC | IN_NONBLOCK);
  if (h->inotify_fd == -1) return app_status_from_sys(errno, "start file watcher"); 

  h->dirs = calloc(cfg->path_count, sizeof(*h->dirs));
  if (!h->dirs) return app_status_from_sys(errno, "allocate dir list"); 
  h->dir_count = cfg->path_count;

  for (size_t i = 0; i < h->dir_count; i++) h->dirs[i].fd = -1;
  for (size_t i = 0; i < h->dir_count; i++) {
    FileStatus file_res = file_dir_ensure_open(&cfg->paths[i], &h->dirs[i].fd, &h->dirs[i].st_dev);
    if (file_res.kind != FILE_OK) return app_status_from_file(file_res);
  }

  h->watches = calloc(cfg->watch_count, sizeof(*h->watches));
  if (!h->watches) return app_status_from_sys(errno, "allocate watch list"); 
  h->watch_count = cfg->watch_count;

  for (size_t i = 0; i < h->watch_count; i++) h->watches[i] = (WatchHandle){.td = -1, .wd = -1 };
  for (size_t i = 0; i < h->watch_count; i++) {
    AppStatus res = app_handles_open_watched(h->inotify_fd, cfg, &cfg->watches[i], &h->watches[i]);
    if (res.kind != APP_OK) return res;
  }

  return app_status_ok();
}

void app_destroy(AppCtx *app) {
  if (!app) return;
  config_destroy(&app->config);
  app_handles_close(&app->handles);
}

AppStatus app_setup(AppCtx *app) {
  assert(app);

  app_handles_reset(&app->handles);
  CanonicalPath config_path = {0};
  AppStatus status = app_status_from_file(file_config_path_get(&config_path));
  if (status.kind == APP_OK) status = app_status_from_config(config_build(&app->config, &config_path));
  if (status.kind == APP_OK) status = app_handles_open(&app->config, &app->handles);
  if (status.kind != APP_OK) app_destroy(app);
  return status;
}

AppStatus app_watch_rule_apply(const AppCtx *app, size_t watch_idx, const char *fname) {
  assert(app && fname);
  assert(watch_idx < app->config.watch_count);

  Watch *w = &app->config.watches[watch_idx];
  const Rule *r = config_watch_rule_match(&app->config, w, fname);
  if (!r) return app_status_ok(); // no rule applies, we are done

  FileStatus e;
  switch (r->action) {
    case MOVE:
      e = file_move(&app->handles.dirs[w->path_idx], &app->handles.dirs[r->destination_idx], fname);
      break;

    case DELETE:
      e = file_delete(&app->handles.dirs[w->path_idx], fname);
      break;
  }

  return app_status_from_file(e);
}

void app_watched_dirs_show(const AppCtx *app) {
  assert(app && app->config.watches);

  for (size_t i = 0; i < app->config.watch_count; i++) {
    printf("Watching: %s/\n", app->config.paths[app->config.watches[i].path_idx].s);
  }
}

AppStatus app_watch_timer_arm(const AppCtx *ctx, size_t idx) {
  assert(ctx);

  struct itimerspec timer_spec = {0};
  timer_spec.it_value.tv_sec = ctx->config.watches[idx].settle_sec;

  int res = timerfd_settime(ctx->handles.watches[idx].td, 0, &timer_spec, NULL);
  if (res == -1) return app_status_from_sys(errno, "arming the timer");

  return app_status_ok();
}

AppStatus app_watch_timer_handle(const AppCtx *app, size_t idx) {
  uint64_t expirations;
  ssize_t s = read(app->handles.watches[idx].td, &expirations, sizeof(expirations));
  if (s == -1 && errno == EAGAIN) return app_status_ok();
  if (s == -1) return app_status_from_sys(errno, "reading from the timer file descriptor");

  assert(s == sizeof(expirations));

  struct dirent **namelist;

  DirHandle *h = &app->handles.dirs[app->config.watches[idx].path_idx];

  ssize_t entries = scandirat(h->fd, ".", &namelist, NULL, alphasort); 
  if (entries == -1) return app_status_from_sys(errno, "generating entries with scandirat");

  for (ssize_t i = 0; i < entries; i++) {
    char *fname = namelist[i]->d_name;
    if (strcmp(fname, ".") == 0 || strcmp(fname, "..") == 0) {
      free(namelist[i]);
      continue;
    }

    AppStatus res = app_watch_rule_apply(app, idx, fname);

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

    for (size_t i = 0; i < app->config.watch_count; ++i) {
      if (app->handles.watches[i].wd == event->wd) {
        const CanonicalPath *path = config_watch_path(&app->config, &app->config.watches[i]);
        printf("%s/", path->s);
        break;
      }
    }

    if (event->len) printf("%s", event->name);
    if (event->mask & IN_ISDIR) printf(" [directory]\n");
    else printf(" [file]\n");

    for (char *ptr = buf; ptr < buf + size; ptr += sizeof(struct inotify_event) + event->len) {
      event = (const struct inotify_event*)ptr;
      if (event->mask & (IN_MODIFY | IN_CLOSE_WRITE | IN_MOVED_TO)) {
        for (size_t i = 0; i < app->config.watch_count; ++i) {
          if (app->handles.watches[i].wd == event->wd && event->len) {
            app_watch_timer_arm(app, i); // arm the timer on IN_MODIFY | IN_CLOSE_WRITE | IN_MOVED_TO
            break;
          }
        }
      }
    }
  }
}
