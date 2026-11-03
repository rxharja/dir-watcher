#define _GNU_SOURCE
#include <asm-generic/errno-base.h>
#include <asm-generic/errno.h>
#include <linux/limits.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "file.h"
#include "dictionary.h"
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/stat.h>

void file_err_show(int errsv, const char * dir_path) {
  switch (errsv) {
    case ENOTDIR: // config mistake, dir isn't a dir
      printf("%s is not a dir.\n", dir_path);
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

int file_dir_create(const char * dir_path) {
  char path_to_create[PATH_MAX];
  snprintf(path_to_create, PATH_MAX, "%s", dir_path);
  // todo: walk the path, split on '/', create it segment by segment.
  // treat EEXIST as success
}

DestDir* file_dir_open(const char *dir_path) {
  int dir_fd = open(dir_path, O_PATH | O_CLOEXEC | O_DIRECTORY);

  if (dir_fd == -1) {
    int errsv = errno;
    if (errsv == ENOENT && mkdir(dir_path, 0755) != -1) return file_dir_open(dir_path);
    strerror(errsv);
    file_err_show(errsv, dir_path);
    return NULL;
  }

  // do some statting here and close dir_fd on failure.

  DestDir *watched_file = malloc(sizeof(DestDir));
  watched_file->fd = dir_fd;
  watched_file->st_dev = 0; // replace with st_dev
  snprintf(watched_file->path, PATH_MAX, "%s", dir_path);
  return watched_file;
}

char* file_dir(const Dictionary *dict, const char *file_name) {
  const char *dot = strrchr(file_name, '.');
  if (!dot || dot == file_name) return NULL;

  char *path = dict_find(dict, dot);
  if (path == NULL) return default_dir;

  return path;
}

void file_move(const Dictionary *dict, const char *dir, const char *file_name) {
  char original_path[PATH_MAX];
  char full_new_path[PATH_MAX];

  char* path = file_dir(dict, file_name);
  if (path == NULL) return; // todo: handle later

  snprintf(original_path, PATH_MAX, "%s/%s", dir, file_name);
  snprintf(full_new_path, PATH_MAX, "%s/%s", path, file_name);

  printf("Moving %s to %s\n", original_path, full_new_path);

  rename(original_path, full_new_path);
}
