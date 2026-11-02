#include <stddef.h>
#include <stdio.h>
#include <string.h>


static size_t file_dir(const char *file_name, size_t len, char *out_dir) {
  const char pdfs[] = "/home/redonxharja/pdfs";
  const char zips[] = "/home/redonxharja/zips";

  const char *dot = strrchr(file_name, '.');

  if (!dot || dot == file_name) return 0;

  if (strcmp(".pdf", dot) == 0) {
    snprintf(out_dir, 256, pdfs); 
    return sizeof(pdfs);
  }

  if (strcmp(".zip", dot) == 0) {
    snprintf(out_dir, 256, zips); 
    return sizeof(zips);
  } 

  return 0;
}

static void file_move(const char *dir, size_t dir_len, const char *file_name, size_t len) {
  static char out_path[] = "/home/redonxharja";
  char original_path[256];
  char final_path[256];

  snprintf(original_path, dir_len + len + 1, "%s%s", dir, file_name);
  snprintf(final_path, sizeof(out_path) + len + 1, "%s/%s", out_path, file_name);

  printf("Moving to %s\n", final_path);
  rename(original_path, final_path);
}
