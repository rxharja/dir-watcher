//
// Created by redonxharja on 11/12/26.
//

#ifndef DIR_WATCHER_RULES_H
#define DIR_WATCHER_RULES_H
#include <linux/limits.h>
#include <tomlc17.h>
#include "config.h"
#include "file.h"

// +1 so index 0 doesn't encode to NULL (dict_find's "absent" value)

typedef struct {
  ssize_t target_dir_idx; // -1 means exclude
  char match[1024];
} Rule;

typedef struct {
  uint64_t settle_sec;
  CanonicalPath path;
} WatchedPath;

typedef struct {
  size_t dir_count;
  size_t watch_path_count;
  size_t rule_set_count;

  Rule rule_set[MAX_RULES]; 

  CanonicalPath *dir_paths; // set of target dirs
  WatchedPath *watch_paths;
} Rules;

typedef enum {
  CONFIG_NOT_PROCESSED = -1,
  CONFIG_OK = 0,
  CONFIG_MISSING,
  CONFIG_WRONG_TYPE
} ConfigStatusType;

typedef struct {
  ConfigStatusType kind;
  int sys_errno;
  const char *key;
} ConfigStatus;

ConfigStatus rules_build(Rules *out, const char *config_path);

void rules_deinit(Rules *out);

ssize_t rules_set_path_find_idx(const Rules *rules, const char *string);

const char* rules_config_strerror(ConfigStatusType err);

#endif //DIR_WATCHER_RULES_H
