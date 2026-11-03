#include <stddef.h>
#include <stdio.h>
#include <string.h>


static const char pdfs[] = "/home/redonxharja/projects/dir-watcher/pdfs";

static const char zips[] = "/home/redonxharja/projects/dir-watcher/zips";

static const char default_dir[] = "/home/redonxharja/projects/dir-watcher/output";

static size_t file_dir(const char *file_name, size_t len, char *out_dir);

static void file_move(const char *dir, size_t dir_len, const char *file_name, size_t len);
