//
// Created by redonxharja on 11/12/26.
//

#ifndef DIR_WATCHER_RULES_H
#define DIR_WATCHER_RULES_H
#include <linux/limits.h>
#include <tomlc17.h>
#include "config.h"
#include "file.h"

#define CONFIG_VERSION "version"
#define CONFIG_DRY_RUN "dry_run"
#define CONFIG_RULE "rule"
#define CONFIG_RULE_ACTION "action"
#define CONFIG_RULE_CONFLICT "conflict"
#define CONFIG_RULE_DEST "destination"
#define CONFIG_RULE_MATCH "match"
#define CONFIG_WATCH "watch"
#define CONFIG_WATCH_PATH "path"
#define CONFIG_WATCH_SETTLE_SEC "settle_seconds"
#define CONFIG_DRY_RUN "dry_run"

typedef enum { NONE = -1, RENAME = 0, SKIP, REPLACE } ConflictAction;

typedef enum { MOVE = 0, IGNORE, DELETE } RuleAction;

typedef struct {
  RuleAction action; // config error if missing
  ConflictAction conflict; // resolved at parse, RENAME if unset
  ssize_t destination_idx; // -1 means no destination for delete
  size_t pattern_start;
  size_t pattern_count;
} Rule;

typedef struct {
  uint64_t settle_sec;
  size_t path_idx;
  size_t rule_start;
  size_t rule_count;
} Watch;

typedef struct {
  uint64_t version;
  int dry_run;

  size_t path_count;
  CanonicalPath *paths; 

  size_t total_rule_count; // encompasses the global rules + rules for individual watches
  size_t global_rule_count; // global rules start at 0 and end with this value
  Rule *rules; 

  size_t watch_count;
  Watch *watches; 

  size_t pattern_count;
  char **patterns;
} Config;

typedef struct { 
  const char *name;
  const int value;
} ParsePair;

typedef enum {
  CONFIG_NOT_PROCESSED = -1,
  CONFIG_OK = 0,
  CONFIG_MISSING,
  CONFIG_WRONG_TYPE,
  CONFIG_INVALID_VAL,
  CONFIG_MAX_RULES,
  CONFIG_PATTERN_LENGTH,
  CONFIG_PARSE,
  CONFIG_FILE,
  CONFIG_SYSTEM_ERR,
} ConfigStatusType;

typedef struct {
  ConfigStatusType kind;
  int sys_errno;
  const char *key;
  char err_msg[256];
  FileStatus file;
} ConfigStatus;

ConfigStatus config_build(Config *cfg, const CanonicalPath *config_path);

void config_destroy(Config *out);

const char* config_type_show(ConfigStatusType err);

void config_status_show(ConfigStatus s);

const CanonicalPath *config_watch_path(const Config *cfg, const Watch *watch);

const CanonicalPath *config_rule_dest(const Config *cfg, const Rule *rule);

const char *config_pattern_by_idx(const Config *cfg, size_t pattern_idx);

#endif //DIR_WATCHER_RULES_H
