#ifndef FILE_H
#define FILE_H

#include "dictionary.h"
#include <linux/limits.h>
#include <stddef.h>
#include <sys/types.h>

typedef struct { char s[PATH_MAX]; } CanonicalPath;

typedef struct { int fd; dev_t st_dev; } DirHandle;

typedef struct { DirHandle dir; int wd; } WatchedDirHandle;

typedef struct {
  const char *name;
  const char *ext;
} BaseName;

static const char default_dir[] = "/home/redonxharja/projects/dir-watcher/output";

// normalize file path -> build a Dir from the file path
int file_normalize(const char *dir_path, char *out);

char* file_dir(const Dictionary *dict, const char *file_name);

int file_move(const DirHandle *origin, const DirHandle* destination, const BaseName *bname);

int file_normalize(const char *dir_path, char *out);

int file_dir_create(const CanonicalPath *dir_path);

int file_dir_open(const CanonicalPath *dir_path, int *dir_fd);

int file_dir_stat(const int dir_fd, dev_t *st_dev);

int file_basename_build(const char * fpath, BaseName *b);

int file_move(const DirHandle *origin, const DirHandle* destination, const BaseName *bname);

DirHandle file_dir_build(int fd, dev_t st_dev, char *path_raw); 

#endif // !FILE_H
