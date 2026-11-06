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

void file_err_show(int errsv, const char * dir_path) {
  switch (errsv) {
    case ENOTDIR: // config mistake, dir isn't a dir
      printf("%s is not a directory.\n", dir_path);
      break;
    case EACCES: // no search (execute) permissions on path
      printf("Dir Watcher does not have access to %s.\n", dir_path);
      break;
    case ELOOP: // symlink loop in path
      printf("There is a symlink loop in the path %s.\n", dir_path);
      break;
    case ENAMETOOLONG: // path name exceeds 4096
      printf("%s exceeds the 4096 character limit for filepaths.\n", dir_path);
      break;
    case EMFILE: // out of fd's 
      printf("%s could not be added because the system is out of file descriptors.\n", dir_path);
  }
}

static int file_write_path_to_buf(const char *part1, const char *part2, char* out) {
  assert(part1);
  assert(part2);
  assert(out);

  int n = snprintf(out, PATH_MAX, "%s/%s", part1, part2);

  if (n < 0) return -1;

  if ((size_t)n >= PATH_MAX) {
    errno = ENAMETOOLONG;
    return -1;
  }

  return 0;
}

static int file_tilde_expand(const char *dir_path, char *out) {
  assert(out != NULL);
  assert(dir_path);
  assert(dir_path[0] == '~');

  if (dir_path[1] != '/') return -1;

  const char *home_dir = getenv("HOME");
  if (home_dir != NULL && home_dir[0] != '\0') {
    return file_write_path_to_buf(home_dir, dir_path + 1, out);
  }

  struct passwd *pw = getpwuid(getuid());
  if (pw == NULL) return -1;
  if (pw->pw_dir == NULL) return -1;

  return file_write_path_to_buf(pw->pw_dir, dir_path + 1, out);
}

static int file_relative_expand(const char *dir_path, char *out) {
  char real_path[PATH_MAX];

  if (realpath(dir_path, real_path) == NULL && errno != ENOENT) {
    perror("realpath");
    return -1;
  }

  // const char *rel_dir_path = dir_path[0] == '.' ? dir_path + 1 : dir_path;

  if (file_write_path_to_buf(real_path, "", out) != 0) {
    printf("Cannot expand relative path for %s.\n", dir_path);
    return -1;
  }

  return 0;
}

int file_normalize(const char *dir_path, char *out) {
  assert(out != NULL);
  const char *path_ptr = dir_path;

  if(dir_path[0] == '\0') {
    printf("The supplied directory was empty.\n");
    return -1;
  }

  if (dir_path[0] == '~' && file_tilde_expand(path_ptr, out) < 0) {
    printf("Cannot expand '~' for path %s.\n", dir_path);
    return -1;
  }
  else if (dir_path[0] == '.' || dir_path[0] != '/') {
    return file_relative_expand(dir_path, out);
  }

  dirname(out);

  return 0;
}

// invariant: fully qualified path
int file_dir_create(const CanonicalPath *dir_path) {
  assert(dir_path);

  char path_to_create[PATH_MAX];
  // todo: actually handle the length provided by the dir_path
  snprintf(path_to_create, PATH_MAX - 1, "%s/", dir_path->s);

  char *slash = strchr(path_to_create + 1, '/');

  while (slash != NULL) {
    *slash = '\0';
    int mkdir_res = mkdir(path_to_create, 0755);
    *slash = '/';

    if (mkdir_res != 0 && errno != EEXIST) {
      perror("mkdir");
      return -1;
    }

    slash = strchr(slash + 1, '/');
  }

  return 0;
}

int file_dir_open(const CanonicalPath *dir_path, int *dir_fd) {
  *dir_fd = open(dir_path->s, O_PATH | O_CLOEXEC | O_DIRECTORY);

  if (*dir_fd == -1) {
    int errsv = errno;
    printf("Error opening %s: ", dir_path->s);
    strerror(errsv);
    printf("%s\n", strerror(errsv));
    file_err_show(errsv, dir_path->s);
    return -1;
  }

  return 0;
}

int file_dir_stat(const int dir_fd, dev_t *st_dev) {
  struct stat stat_buf;
  int res = fstat(dir_fd, &stat_buf);

  if (res != 0) {
    perror("fstat");
    return res;
  }

  *st_dev = stat_buf.st_dev;
  return 0;
}

// a
int file_basename_build(const char * fpath, BaseName *b) {
  assert(fpath);
  assert(b);

  b->name = fpath;

  const char *dot = strrchr(b->name, '.');
  if (dot) b->ext  = dot;
  if (dot == b->name) return -1;

  return 0;
}

int file_move(const DirHandle *origin, const DirHandle* destination, const BaseName *bname) {
  int res = renameat2(origin->fd, bname->name, destination->fd, bname->name, RENAME_NOREPLACE);
  if (res != 0) {
    perror("renameat2");
    return -1;
  }

  return 0;
}

