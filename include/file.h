#include "dictionary.h"
#include <stddef.h>
#include <stdio.h>
#include <string.h>

static const char default_dir[] = "/home/redonxharja/projects/dir-watcher/output";

static char* file_dir(const Dictionary *dict, const char *file_name);

static void file_move(const Dictionary *dict, const char *dir, size_t dir_len, const char *file_name, size_t len);
