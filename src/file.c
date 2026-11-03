#include <linux/limits.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "file.h"
#include "dictionary.h"

static char* file_dir(const Dictionary *dict, const char *file_name) {
  const char *dot = strrchr(file_name, '.');
  if (!dot || dot == file_name) return NULL;

  char *path = dict_find(dict, dot);
  if (path == NULL) return default_dir;

  return path;
}

static void file_move(const Dictionary *dict, const char *dir, size_t dir_len, const char *file_name, size_t len) {
  char original_path[PATH_MAX];
  char full_new_path[PATH_MAX];

  char* path = file_dir(dict, file_name);
  if (path == NULL) return; // todo: handle later

  snprintf(original_path, PATH_MAX, "%s/%s", dir, file_name);
  snprintf(full_new_path, PATH_MAX, "%s/%s", path, file_name);
  printf("Moving %s to %s\n", original_path, full_new_path);
  rename(original_path, full_new_path);
}
