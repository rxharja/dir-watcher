#ifndef FILE_H
#define FILE_H

#include <linux/limits.h>
#include <stddef.h>
#include <sys/types.h>

// ensures the path is absolute 
typedef struct { char s[PATH_MAX]; } CanonicalPath;

typedef struct { int fd; dev_t st_dev; } DirHandle;

typedef struct { DirHandle dir; int wd; } WatchedDirHandle;

typedef struct {
  const char *name;
  const char *ext;
} BaseName;

// normalize file path -> build a Dir from the file path
int file_normalize(const char *dir_path, char *out);

int file_normalize(const char *dir_path, char *out);

int file_dir_create(const CanonicalPath *dir_path);

int file_dir_open(const CanonicalPath *dir_path, int *dir_fd);

int file_dir_stat(const int dir_fd, dev_t *st_dev);

int file_move(const DirHandle *origin, const DirHandle* destination, const char *fname);

int file_config_dir_get(CanonicalPath *out);

#endif // !FILE_H
