#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "file.h"

static size_t file_dir(const char *file_name, size_t len, char *out_dir) {
  const char *dot = strrchr(file_name, '.');

  if (!dot || dot == file_name) return 0;

  if (strcmp(".pdf", dot) == 0) {
    snprintf(out_dir, sizeof(pdfs), pdfs); 
    return sizeof(pdfs);
  }

  if (strcmp(".zip", dot) == 0) {
    snprintf(out_dir, sizeof(zips), zips); 
    return sizeof(zips);
  } 

  return 0;
}

static void file_move(const char *dir, size_t dir_len, const char *file_name, size_t len) {
  char original_path[256];
  char final_dir[256];
  char final_path[256];

  size_t final_dir_len = file_dir(file_name, len, final_dir);

  snprintf(original_path, dir_len + len + 1, "%s%s", dir, file_name);
  snprintf(final_path, final_dir_len + len + 1, "%s/%s", final_dir, file_name);

  printf("Moving to %s\n", final_path);
  rename(original_path, final_path);
}
