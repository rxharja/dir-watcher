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

DestDir* file_dir_open(const char *dir_path);

char* file_dir(const Dictionary *dict, const char *file_name);

void file_move(const Dictionary *dict, const char *dir, const char *file_name);
