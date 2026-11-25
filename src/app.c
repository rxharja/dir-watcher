#include "file.h"
#include "log.h"
#include "rules.h"
#include <asm-generic/errno.h>
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

static const char default_config[] = 
  "version = 1\n"
  "\n"
  "dry_run = true\n"
  "\n"
  "# global exclude across all watched directories \n"
  "# exclude = [\"*.pdf\"]\n"
  "\n"
  "# delete :Zone.Identifier files on WSL\n"
  "[[rule]]\n"
  "action = \"delete\"\n"
  "match = [\"*:Zone.Identifier\"]\n"
  "\n"
  "# move scattered notes from all watched dirs into ~/notes\n"
  "# [[rule]]\n"
  "# action = \"move\"\n"
  "# destination = \"~/notes\"\n"
  "# match = [\"*.md\", \"*.txt\"]\n"
  "\n"
  "# [[watch]]\n"
  "# path = \"~/\"\n"
  "# settle_seconds = 5\n"
  "# # local exclude to the watched directory\n"
  "# exclude = [\"*.env\"]\n"
  "\n"
  "[[watch]]\n"
  "path = \"~/Downloads\"\n"
  "\n"
  "  [[watch.rule]]\n"
  "  action = \"move\"\n"
  "  destination = \"~/Documents\"\n"
  "  match = [\"*.pdf\", \"*.docx\", \"*.odt\"]\n"
  "  # local exclude to the watched rule\n"
  "  # exclude = [\"invoice-*.pdf\"]\n"
  "\n"
  "  [[watch.rule]]\n"
  "  action = \"move\"\n"
  "  destination = \"~/archives\"\n"
  "  match = [\"*.zip\", \"*.tar.gz\"]\n";

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

AppStatus app_setup(AppCtx *app, int *created) {
  assert(app);

  app_handles_reset(&app->handles);
  CanonicalPath config_path = {0};
  AppStatus status = app_status_from_file(file_config_path_get(&config_path, default_config, created));
  if (status.kind == APP_OK && created) {
    log_info("Config created at %s. Edit the config and run again", config_path.s);
    return status;
  }

  if (status.kind == APP_OK) status = app_status_from_config(config_build(&app->config, &config_path));
  if (status.kind == APP_OK) status = app_handles_open(&app->config, &app->handles);
  if (status.kind != APP_OK) app_destroy(app);
  return status;
}

AppStatus app_file_move_with_rename(DirHandle *origin, DirHandle *dest, const char *fname, char *new_name_out) {
  size_t copy = 1;
  char copy_buf[256];
  FileStatus f = file_move(origin, dest, fname, fname, 0);

  if (f.kind == FILE_OK) {
    int written = snprintf(new_name_out, NAME_MAX + 1, "%s", fname);
    assert(written > 0);
    if (written >= NAME_MAX) return app_status_from_sys(EOVERFLOW, "writing to new_name_out buf");
    return app_status_from_file(f);
  }

  while (f.kind == FILE_ERR_EXISTS) {
    int written = snprintf(copy_buf, sizeof(copy_buf), " (%lu)", copy++);
    assert(written >= 0);
    if ((size_t)written >= sizeof(copy_buf)) return app_status_from_sys(EOVERFLOW, strerror(EOVERFLOW));
    if (f.kind == FILE_ERR_EXISTS) f = file_name_cat(fname, copy_buf, new_name_out);
    if (f.kind == FILE_OK) f = file_move(origin, dest, fname, new_name_out, 0);
  }

  return app_status_from_file(f);
}

AppStatus app_file_move_with_replace(DirHandle *origin, DirHandle *dest, const char *fname, ReceiptKind *kind) {
  FileStatus f = file_move(origin, dest, fname, fname, 0);

  if (f.kind == FILE_OK) {
    *kind = RECEIPT_MOVED;
    return app_status_from_file(f);
  }
  if (f.kind == FILE_ERR_EXISTS) {
    f = file_move(origin, dest, fname, fname, 1);
    if (f.kind == FILE_OK) *kind = RECEIPT_REPLACED;
  }


  return app_status_from_file(f);
}

void app_watches_log(const Config *cfg) {
  assert(cfg && cfg->watches);

  if (cfg->dry_run) log_warn("dry run: no files will be changed");
  for (size_t i = 0; i < cfg->watch_count; i++) {
    log_info("Watching %s", config_watch_path(cfg, &cfg->watches[i])->s);
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

static Effect app_effect_plan(const AppCtx *app, size_t watch_idx, const char *fname) {
  assert(app && fname);
  assert(watch_idx < app->config.watch_count);

  return (Effect) {
    .rule = config_watch_rule_match(&app->config, &app->config.watches[watch_idx], fname),
    .watch_idx = watch_idx,
    .fname = fname
  };
}

void app_effect_describe(const AppCtx *app, Effect effect, LogLevel level) {
  assert(app);

  const CanonicalPath *origin = config_watch_path(&app->config, &app->config.watches[effect.watch_idx]);
  assert(origin);

  if (!effect.rule) {
    log_debug("no rule matched for %s/%s", origin->s, effect.fname);
    return;
  }

  const CanonicalPath *dest = config_rule_dest(&app->config, effect.rule);
  assert(dest);

  switch (effect.rule->action) {
    case MOVE:
      switch (effect.rule->conflict) {
        case RENAME: {
          char new_fname[NAME_MAX + 1];
          FileStatus f = file_name_cat(effect.fname, " (N)", new_fname);
          if (f.kind != FILE_OK) {
            log_error("could not process new fname for description");
            break;
          }

          LOG_AT(level, "moving %s/%s to %s/%s. File will be renamed to '%s/%s' on conflict", 
              origin->s, effect.fname, 
              dest->s, effect.fname, 
              dest->s, new_fname);
          break;
        }
        case REPLACE:
          LOG_AT(level, "moving %s/%s to %s/%s. File will be replaced on conflict", origin->s, effect.fname, dest->s, effect.fname);
          break;
        case NONE:
        case SKIP:
          LOG_AT(level, "moving %s/%s to %s/%s. File will be skipped on conflict", origin->s, effect.fname, dest->s, effect.fname);
          break;
        default: break;
      }
      break;
    case DELETE: LOG_AT(level, "deleting %s/%s", origin->s, effect.fname); break;
    default: break;
  }
}

AppStatus receipt_from_file(AppStatus status, Receipt *out, const Effect *e, ReceiptKind k, const char *name) {
  out->effect = *e;
  snprintf(out->final_name, sizeof(out->final_name), "%s", name);
  switch (status.kind) {
    case APP_OK: out->kind = k; break;
    case APP_FILE: 
      switch (status.u.file.kind) {
        case FILE_ERR_NOT_FOUND: {
          out->kind = RECEIPT_VANISHED;
          return app_status_ok();
        }

        case FILE_OK:
        case FILE_ERR_PATH_EMPTY:
        case FILE_ERR_PATH_MALFORMED:
        case FILE_ERR_PATH_TOO_LONG:
        case FILE_ERR_NO_HOME:
        case FILE_ERR_EXISTS:
        case FILE_ERR_SYSTEM:
          break;
      }
      break;
    case APP_CONFIG:
    case APP_SYSTEM:
      break;
  }

  return status;
}

AppStatus app_effect_run(const AppCtx *app, Effect *e, Receipt *out) {
  assert(app);

  const CanonicalPath *o_path = config_watch_path(&app->config, &app->config.watches[e->watch_idx]);
  assert(o_path);

  if (!e->rule) return app_status_ok();

  const CanonicalPath *dest = config_rule_dest(&app->config, e->rule);
  DirHandle *o_handle = &app->handles.dirs[app->config.watches[e->watch_idx].path_idx];
  DirHandle *dest_handle = &app->handles.dirs[e->rule->destination_idx];
  assert(dest && o_handle && dest_handle);

  switch (e->rule->action) {
    case DELETE: return receipt_from_file(
            app_status_from_file(file_delete(o_handle, e->fname)), out, e, RECEIPT_DELETED, "");
    case MOVE:
      switch (e->rule->conflict) {
        case RENAME: {
          char new_name[NAME_MAX + 1];
          AppStatus s = app_file_move_with_rename(o_handle, dest_handle, e->fname, new_name); 
          ReceiptKind k = strcmp(e->fname, new_name) == 0 ? RECEIPT_MOVED : RECEIPT_RENAMED;
          return receipt_from_file(s, out, e, k, new_name);
        }
        case REPLACE: {
          ReceiptKind k;
          AppStatus s = app_file_move_with_replace(o_handle, dest_handle, e->fname, &k);
          return receipt_from_file(s, out, e, k, e->fname);
        }
        case NONE: case SKIP: return receipt_from_file(app_status_ok(), out, e, RECEIPT_SKIPPED, e->fname);
        default: return app_status_from_sys(ENOTSUP, "the conflict is not supported");
      }
    default: return app_status_from_sys(ENOTSUP, "the action is not supported");
  }
} 

static void app_effect_report(const AppCtx *app, const Receipt *r, AppStatus s) {
  if (s.kind != APP_OK) { app_status_show(s); return; }
  const CanonicalPath *o_path = config_watch_path(&app->config, &app->config.watches[r->effect.watch_idx]);
  assert(o_path);

  if (!r->effect.rule) {
    log_debug("no rules applied to %s/%s", o_path->s, r->effect.fname);
    return;
  }

  const CanonicalPath *dest = config_rule_dest(&app->config, r->effect.rule);
  assert(dest);

  switch (r->kind) {
    case RECEIPT_UNSET:
      log_error("something unexpected happened"); 
      break;
    case RECEIPT_MOVED: 
      log_info("%s/%s was moved to %s/%s", o_path->s,r->effect.fname, dest->s, r->final_name); 
      break;
    case RECEIPT_RENAMED:
      log_info("%s/%s was renamed to %s/%s", o_path->s,r->effect.fname, dest->s, r->final_name); 
      break;
    case RECEIPT_DELETED:
      log_info("%s/%s was deleted", o_path->s,r->effect.fname); 
      break;
    case RECEIPT_SKIPPED:
      log_info("%s/%s was excluded", o_path->s,r->effect.fname); 
      break;
    case RECEIPT_VANISHED:
      log_info("%s/%s no longer exists", o_path->s,r->effect.fname); 
      break;
    case RECEIPT_REPLACED:
      log_info("%s/%s replaced %s/%s", o_path->s,r->effect.fname, dest->s, r->final_name); 
      break;
  }
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

    AppStatus res;
    Receipt receipt = {0};
    Effect effect = app_effect_plan(app, idx, fname);
    app_effect_describe(app, effect, app->config.dry_run ? LOG_LEVEL_INFO : LOG_LEVEL_DEBUG);
    
    if (app->config.dry_run) res = app_status_ok();
    else {
      res = app_effect_run(app, &effect, &receipt);
      app_effect_report(app, &receipt, res);
    }

    free(namelist[i]);
  }

  free(namelist);

  return app_status_ok();
}

AppStatus app_events_handle(const AppCtx *app) {
  assert(app);
  __attribute__((aligned(__alignof__(struct inotify_event)))) char buf[4096];
  const struct inotify_event *event; 
  ssize_t size;

  for (;;) {
    size = read(app->handles.inotify_fd, buf, sizeof buf);
    if (size == -1 && errno == EAGAIN) return app_status_ok();
    if (size == -1 && errno != EAGAIN) return app_status_from_sys(errno, "read from inotify fd");
    assert(size > 0);

    for (char *ptr = buf; ptr < buf + size; ptr += sizeof(struct inotify_event) + event->len) {
      event = (const struct inotify_event*)ptr;
      if (event->mask & (IN_MODIFY | IN_CLOSE_WRITE | IN_MOVED_TO)) {
        for (size_t i = 0; i < app->config.watch_count; ++i) {
          if (app->handles.watches[i].wd == event->wd && event->len) {
            const CanonicalPath *path = config_watch_path(&app->config, &app->config.watches[i]);
            assert(path);

            log_debug("inotify: %s%s%s on %s/%s%s",
                      (event->mask & IN_MODIFY) ? "IN_MODIFY " : "",
                      (event->mask & IN_CLOSE_WRITE) ? "IN_CLOSE_WRITE " : "",
                      (event->mask & IN_MOVED_TO) ? "IN_MOVED_TO " : "", path->s,
                      event->name, (event->mask & IN_ISDIR) ? " [dir]" : "");

            AppStatus res = app_watch_timer_arm(app, i);
            if (res.kind != APP_OK) app_status_show(res);
            break;
          }
        }
      }
    }
  }
}
