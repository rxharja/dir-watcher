#include "dictionary.h"
#include <stddef.h>

static const char default_dir[] = "/home/redonxharja/projects/dir-watcher/output";

static char* file_dir(const Dictionary *dict, const char *file_name);

static void file_move(const Dictionary *dict, const char *dir, const char *file_name);
