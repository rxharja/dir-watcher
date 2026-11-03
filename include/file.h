#ifndef FILE_H
#define FILE_H

#include "dictionary.h"
#include <linux/limits.h>
#include <stddef.h>
#include <sys/types.h>

typedef struct {
  int fd;
  dev_t st_dev;
  char path[PATH_MAX];
} DestDir;

static const char default_dir[] = "/home/redonxharja/projects/dir-watcher/output";

int file_normalize(const char *base_dir, const char *dir_path, char *out);

char* file_dir(const Dictionary *dict, const char *file_name);

void file_move(const Dictionary *dict, const char *dir, const char *file_name);

int file_normalize(const char *base_dir, const char *dir_path, char *out);

int file_dir_create(const char *dir_path);

int file_dir_open(const char *dir_path);

int file_dir_stat(const int dir_fd);

#endif // !FILE_H
