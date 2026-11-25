#include <libgen.h>
#include <assert.h>
#include <unistd.h>
#include <asm-generic/errno-base.h>
#include <asm-generic/errno.h>
#include <linux/limits.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "file.h"
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <pwd.h>

static FileStatus file_ok(void) {
  return (FileStatus) { .kind = FILE_OK };
}

static FileStatus file_fail(FileStatusType kind, const char *path) {
  return (FileStatus) { .kind = kind, .path = path };
}

static FileStatus file_from_errno(int e, const char *path) {
  FileStatusType kind = e == ENOENT ? FILE_ERR_NOT_FOUND
                : e == EEXIST ? FILE_ERR_EXISTS
                :               FILE_ERR_SYSTEM;

  return (FileStatus) { .kind = kind, .sys_errno = e, .path = path };
}

const char *file_streerror(const FileStatusType kind) {
  switch (kind) {
    case FILE_OK: return "ok";
    case FILE_ERR_PATH_EMPTY: return "path is empty";
    case FILE_ERR_PATH_MALFORMED: return "path is malformed";
    case FILE_ERR_PATH_TOO_LONG: return "path exceeds PATH_MAX";
    case FILE_ERR_NO_HOME: return "could not determine home directory";
    case FILE_ERR_NOT_FOUND: return "not found";
    case FILE_ERR_EXISTS: return "already exists";
    case FILE_ERR_SYSTEM: return "system error";
    default: return "unknown";
  }
}

static char *file_status_kind_show(FileStatusType kind) {
  switch (kind) {
    case FILE_OK: break;
    case FILE_ERR_PATH_EMPTY: return "Empty Path";
    case FILE_ERR_PATH_MALFORMED: return "Malformed Path";
    case FILE_ERR_PATH_TOO_LONG: return "Path too long";
    case FILE_ERR_NO_HOME: return "No Home Directory";
    case FILE_ERR_NOT_FOUND: return "Not Found";
    case FILE_ERR_EXISTS: return "File Exists";
    case FILE_ERR_SYSTEM: return "System Error";
  }
  return "";
}

void file_status_show(FileStatus s) {
  fprintf(stderr, "%s: %s", s.path, file_status_kind_show(s.kind));
  s.sys_errno != 0 ? fprintf(stderr, " %s\n", strerror(s.sys_errno)) : fprintf(stderr, "\n");
}

FileStatus file_path_cat(const char *part1, const char *part2, char* out) {
  assert(part1 && part2 && out);

  size_t len = strlen(part1);
  const char *sep = (len >  0 && part1[len -1] == '/') ? "" : "/";

  int n = snprintf(out, PATH_MAX, "%s%s%s", part1, sep, part2);
  assert( n >= 0);
  if ((size_t)n >= PATH_MAX) return file_fail(FILE_ERR_PATH_TOO_LONG, NULL);

  return file_ok();
}

FileStatus file_name_cat(const char *og_name, const char *append, char* out) {
  assert(og_name && append && out);
  int n;

  const char *period = strrchr(og_name, '.');
  if (period) {
    char fname[PATH_MAX];
    int written = snprintf(fname, PATH_MAX, "%s", og_name);
    assert(written >= 0);

    fname[period - og_name] = '\0';
    n = snprintf(out, PATH_MAX, "%s%s%s", fname, append, period);
  }
  else {
    n = snprintf(out, PATH_MAX, "%s%s", og_name, append);
    assert( n >= 0);
  }

  if ((size_t)n >= PATH_MAX) return file_fail(FILE_ERR_PATH_TOO_LONG, NULL);
  return file_ok();
}

static FileStatus file_home_dir_get(const char **out) {
  const char *home = getenv("HOME");
  if (home && home[0] == '/') {
    *out = home;
    return file_ok();
  }

  errno = 0;
  struct passwd *pw = getpwuid(getuid());
  if (!pw || !pw->pw_dir || pw->pw_dir[0] != '/') {
    return (FileStatus){ .kind = FILE_ERR_NO_HOME, .sys_errno = errno };
  }

  *out = pw->pw_dir;
  return file_ok();
}


static FileStatus file_tilde_expand(const char *dir_path, char *out) {
  assert(out && dir_path && dir_path[0] == '~');

  // assert the path starts with ~/ otherwise it's malformed
  if (dir_path[1] != '\0' && dir_path[1] != '/') return file_fail(FILE_ERR_PATH_MALFORMED, dir_path);

  const char *home;
  FileStatus err = file_home_dir_get(&home);
  if (err.kind == FILE_OK) err = file_path_cat(home, dir_path + 1, out);
  if (err.kind != FILE_OK) err.path = dir_path;

  return err;
}

FileStatus file_config_dir_get(CanonicalPath *out) {
  assert(out);

  char app_dir[PATH_MAX];
  FileStatus err = {0};

  const char *xdg = getenv("XDG_CONFIG_HOME");
  if (xdg && xdg[0] == '/') err = file_path_cat(xdg, "dir-watcher", app_dir);
  else {
    const char *home;
    err = file_home_dir_get(&home);
    if (err.kind == FILE_OK) err = file_path_cat(home, ".config/dir-watcher", app_dir);
  }

  if (err.kind == FILE_OK) err = file_normalize(app_dir, out);
  if (err.kind != FILE_OK) err.path = "config directory";
  return err;
}

static FileStatus file_relative_expand(const char *dir_path, char *out) {
  assert(dir_path && out);
  assert(dir_path[0] != '/' && dir_path[0] != '~');

  CanonicalPath config_path = {0};
  FileStatus err = file_config_dir_get(&config_path);
  if (err.kind == FILE_OK) err = file_path_cat(config_path.s, dir_path, out);
  else                     err.path = dir_path;

  return err;
}

__attribute__((warn_unused_result))
FileStatus file_normalize(const char *dir_path, CanonicalPath *out) {
  assert(dir_path && out);

  if (dir_path[0] == '\0') return file_fail(FILE_ERR_PATH_EMPTY, dir_path);

  char path_raw[sizeof(out->s)];
  FileStatus err = {0};
  const char *to_resolve = dir_path;

  if (dir_path[0] == '/') {/* Nothing to do */ }
  if (dir_path[0] == '~') {
    err = file_tilde_expand(dir_path, path_raw);
    to_resolve = path_raw;
  } 
  else if (dir_path[0] != '/') {
    err = file_relative_expand(dir_path, path_raw);
    to_resolve = path_raw;
  }

  if (err.kind != FILE_OK) return err;

  if (realpath(to_resolve, out->s) != NULL) return file_ok();

  int e = errno;
  if (e != ENOENT) return file_from_errno(e, dir_path);

  size_t len = strlen(to_resolve);
  if (len >= sizeof(out->s)) return file_fail(FILE_ERR_PATH_TOO_LONG, dir_path);
  memcpy(out, to_resolve, len + 1);
  return file_ok();
}

void file_path_slash_collapse(char *s) {
  assert(s);

  size_t write = 0;
  for(size_t read = 0; s[read] != '\0'; read++) {
    if (s[read] == '/' && write > 0 && s[write - 1] == '/') continue;
    s[write++] = s[read];
  }

  s[write] = '\0';
}

FileStatus file_dir_create(const CanonicalPath *dir_path) {
  assert_canonical(dir_path);

  char path_to_create[PATH_MAX];
  sprintf(path_to_create, "%s", dir_path->s);

  char *slash = strchr(path_to_create + 1, '/');

  while (slash != NULL) {
    *slash = '\0';
    int mkdir_res = mkdir(path_to_create, 0755);
    int e = errno;

    *slash = '/';
    if (mkdir_res != 0 && e != EEXIST) return file_from_errno(e, dir_path->s);
    slash = strchr(slash + 1, '/');
  }

  if (mkdir(path_to_create, 0755) != 0 && errno != EEXIST) {
    return file_from_errno(errno, dir_path->s);
  }

  return file_ok();
}

FileStatus file_dir_open(const CanonicalPath *dir_path, int *dir_fd) {
  assert_canonical(dir_path);
  assert(dir_fd);

  *dir_fd = open(dir_path->s, O_PATH | O_CLOEXEC | O_DIRECTORY);
  if (*dir_fd == -1) return file_from_errno(errno, dir_path->s);
  return file_ok();
}

FileStatus file_dir_stat(const int dir_fd, dev_t *st_dev) {
  struct stat stat_buf;

  int res = fstat(dir_fd, &stat_buf);
  if (res != 0) return file_from_errno(errno, NULL);

  *st_dev = stat_buf.st_dev;
  return file_ok();
}


FileStatus file_move(const DirHandle *origin, const DirHandle* destination, const char *og_name, const char *new_name, int replace) {
  errno = 0;
  int res = renameat2(origin->fd, og_name, destination->fd, new_name, replace ? 0 : RENAME_NOREPLACE);
  if (errno == EEXIST) return file_fail(FILE_ERR_EXISTS, og_name);
  if (errno == ENOENT) return file_fail(FILE_ERR_NOT_FOUND, og_name);
  if (res == 0) return file_ok();
  return file_from_errno(errno, og_name);
}

FileStatus file_delete(const DirHandle *origin, const char *fname) {
  errno = 0;
  int res = unlinkat(origin->fd, fname, 0);
  if (errno == ENOENT) return file_fail(FILE_ERR_NOT_FOUND, fname);
  if (res == 0) return file_ok();
  return file_from_errno(errno, fname);
}


FileStatus file_dir_ensure_open(const CanonicalPath *path, int *fd, dev_t *st_dev) {
  assert_canonical(path);
  FileStatus err = file_dir_create(path);
  if (err.kind == FILE_OK) err = file_dir_open(path, fd);
  if (err.kind == FILE_OK) err = file_dir_stat(*fd, st_dev);
  return err;
}

static FileStatus file_config_assert_exists(CanonicalPath *path) {
  assert_canonical(path);
  FILE *f = fopen(path->s, "r");
  if (!f)             return file_from_errno(errno, path->s);
  if (fclose(f) != 0) return file_from_errno(errno, path->s);
  return file_ok();
}

FileStatus file_config_write(CanonicalPath *path, const char *config, const size_t s) {
  assert_canonical(path);
  assert(config);
  assert(s > 0);

  FILE *f = fopen(path->s, "wx");
  if (!f) return file_from_errno(errno, path->s);

  errno = 0;
  const size_t written = fwrite(config, sizeof(char), s, f);
  const int errno_fwrite = errno;

  const int close_result = fclose(f);
  const int close_errno = errno;

  if (written != s) {
    (void)unlink(path->s);
    return file_from_errno(errno_fwrite!= 0 ? errno_fwrite: EIO, path->s);
  }

  if (close_result != 0) {
    (void)unlink(path->s);
    return file_from_errno(close_errno, path->s);
  }

  return file_ok();
}

FileStatus file_config_path_get(CanonicalPath *out, const char *config, int *created) {
  assert(out);
  assert(config);

  CanonicalPath config_dir;
  FileStatus status = file_config_dir_get(&config_dir);
  if (status.kind == FILE_OK) status = file_path_cat(config_dir.s, "config.toml", out->s);
  if (status.kind == FILE_OK) status = file_config_assert_exists(out);
  if (status.kind == FILE_OK) return status; // exit here if already exists
  if (status.kind == FILE_ERR_NOT_FOUND) status = file_dir_create(&config_dir);
  if (status.kind == FILE_OK) status = file_config_write(out, config, strlen(config));
  if (status.kind == FILE_OK) *created = 1;
  else status.path = "config directory"; // file_dir_create tries populating a local
  return status;
}
