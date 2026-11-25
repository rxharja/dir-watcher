#ifndef FILE_H
#define FILE_H

#include <linux/limits.h>
#include <stddef.h>
#include <sys/types.h>

// ensures the path is absolute 
typedef struct { char s[PATH_MAX]; } CanonicalPath;

// a canonical path is an absolute path starting with '/'
#define assert_canonical(p)   \
  do {                        \
    assert((p) != NULL);      \
    assert((p)->s[0] == '/'); \
  } while (0)

typedef struct { int fd; dev_t st_dev; } DirHandle;

typedef struct { 
  int wd; // watched fd
  int td; // timer fd
} WatchHandle;

typedef enum {
  FILE_OK = 0,

  // bad input: user config is wrong
  FILE_ERR_PATH_EMPTY,
  FILE_ERR_PATH_MALFORMED,
  FILE_ERR_PATH_TOO_LONG,

  // environment
  FILE_ERR_NO_HOME,

  // filesystem states
  FILE_ERR_NOT_FOUND,
  FILE_ERR_EXISTS,

  // syscall failure, details in errno
  FILE_ERR_SYSTEM
} FileStatusType;

typedef struct {
  FileStatusType kind;
  int sys_errno;
  const char *path; // borrowed, valid only whiel caller's buffer is
} FileStatus;

__attribute__((warn_unused_result))
const char *file_streerror(const FileStatusType kind);

// normalize file path -> build a Dir from the file path
__attribute__((warn_unused_result))
FileStatus file_normalize(const char *dir_path, CanonicalPath *out);

__attribute__((warn_unused_result))
FileStatus file_dir_create(const CanonicalPath *dir_path);

__attribute__((warn_unused_result))
FileStatus file_dir_open(const CanonicalPath *dir_path, int *dir_fd);

__attribute__((warn_unused_result))
FileStatus file_dir_stat(const int dir_fd, dev_t *st_dev);

FileStatus file_move(const DirHandle *origin, const DirHandle* destination, const char *og_name, const char *new_name, int replace);

__attribute__((warn_unused_result))
FileStatus file_delete(const DirHandle *origin, const char *fname);

__attribute__((warn_unused_result))
FileStatus file_config_dir_get(CanonicalPath *out);

__attribute__((warn_unused_result))
FileStatus file_dir_ensure_open(const CanonicalPath *path, int *fd, dev_t *st_dev);

__attribute__((warn_unused_result))
FileStatus file_path_cat(const char *part1, const char *part2, char* out);

__attribute__((warn_unused_result))
FileStatus file_name_cat(const char *part1, const char *part2, char* out);

FileStatus file_config_path_get(CanonicalPath *out, const char *config, int *created);

void file_status_show(FileStatus s);


#endif // !FILE_H
