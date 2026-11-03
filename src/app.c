#include "file.h"
#include <stdio.h>
#include <unistd.h>

// temporary function to reason about file set up
DestDir* setup(const char *base_dir, const char *dest_dir_path) {
  char qualified_path[PATH_MAX];
  if (file_normalize(base_dir, dest_dir_path, qualified_path) != 0) {
    perror("file_normalize");
    return NULL;
  }

  printf("Qualified path: %s\n", qualified_path);

  if (file_dir_create(qualified_path) != 0) {
    perror("file_dir_create");
    return NULL;
  }

  int dir_fd = file_dir_open(qualified_path);
  if (dir_fd < 0) return NULL;

  int st_dev = file_dir_stat(dir_fd);
  if (st_dev < 0) {
    close(dir_fd);
    return NULL;
  }

  DestDir *watched_file = malloc(sizeof(DestDir));

  if (!watched_file) {
    perror("malloc");
    close(dir_fd);
    return NULL;
  }

  watched_file->fd = dir_fd;
  watched_file->st_dev = st_dev; 
  snprintf(watched_file->path, PATH_MAX, "%s", qualified_path);
  return watched_file;
}
