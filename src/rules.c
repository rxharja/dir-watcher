//
// Created by redonxharja on 11/12/26.
//

#include "rules.h"
#include "file.h"
#include <inttypes.h>
#include <linux/limits.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <tomlc17.h>
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <fnmatch.h>
#include <config.h>
#include <stdarg.h>

static ParsePair action_pairs[] = {{"move", MOVE}, {"delete", DELETE}, {NULL, 0}};

static ParsePair conflict_pairs[] = {{"rename", RENAME}, {"skip", SKIP}, {"replace", REPLACE}, {NULL, 0}};

static ConfigStatus config_status_ok(void) { return (ConfigStatus) { .kind = CONFIG_OK }; }

static ConfigStatus config_status_from_sys(ConfigStatusType kind, int sys_errno, const char *key) {
  return (ConfigStatus) { .kind = kind, .key = key, .sys_errno = sys_errno, .err_msg = "" };
}

__attribute__((format(printf, 3, 4))) 
static ConfigStatus config_status_failf(ConfigStatusType kind, const char *key, const char *fmt, ...) {
  ConfigStatus status = (ConfigStatus) { .kind = kind, .key = key };
  va_list args;
  va_start(args, fmt);
  vsnprintf(status.err_msg, sizeof (status.err_msg), fmt, args); 
  va_end(args);
  return status;
}

static ConfigStatus config_status_from_file(FileStatus file, const char *key) {
  return (ConfigStatus) { .kind = file.kind == FILE_OK ? CONFIG_OK : CONFIG_FILE, .file = file, .key = key, };
}

static ConfigStatus config_type_check(const toml_type_t type, const toml_type_t expected, const char *key) {
  if (type  == TOML_UNKNOWN) return (ConfigStatus){CONFIG_MISSING, 0, key, "", {0}};
  if (type != expected) return (ConfigStatus){CONFIG_WRONG_TYPE, 0, key, "", {0}};
  return (ConfigStatus){CONFIG_OK, 0, NULL, "", {0}};
}

static ConfigStatus config_get(const toml_datum_t datum, const char *key, const toml_type_t expected, toml_datum_t *out) {
  const toml_datum_t d = toml_seek(datum, key);
  ConfigStatus err = config_type_check(d.type, expected, key); 
  if (err.kind == CONFIG_OK) *out = d;
  return err;
}

static ConfigStatus config_get_int(toml_datum_t table, const char *key, uint64_t *out) {
  toml_datum_t val;
  ConfigStatus status = config_get(table, key, TOML_INT64, &val);
  if (status.kind != CONFIG_OK) return status;
  *out = val.u.int64;
  return config_status_ok();
}

static ConfigStatus config_get_bool_or(toml_datum_t table, const char *key, int *out, int or) {
  toml_datum_t val;
  ConfigStatus status = config_get(table, key, TOML_BOOLEAN, &val);
  if (status.kind != CONFIG_OK && status.kind != CONFIG_MISSING) return status;
  if (status.kind == CONFIG_MISSING) *out = or;
  else *out = val.u.boolean;
  return config_status_ok();
}

static ConfigStatus config_get_string(toml_datum_t table, const char *key, const char **out) {
  toml_datum_t val;
  ConfigStatus status = config_get(table, key, TOML_STRING, &val);
  if (status.kind != CONFIG_OK) return status;
  *out = val.u.s;
  return config_status_ok();
}

const CanonicalPath *config_watch_path(const Config *cfg, const Watch *watch) {
  assert(cfg);
  assert(cfg->paths);
  assert(watch);
  assert(watch->path_idx < cfg->path_count);
  return &cfg->paths[watch->path_idx];
}

const CanonicalPath *config_rule_dest(const Config *cfg, const Rule *rule) {
  assert(cfg);
  assert(cfg->paths);
  assert(rule);
  assert(rule->destination_idx < (ssize_t)cfg->path_count);
  return &cfg->paths[rule->destination_idx];
}

const char *config_pattern_by_idx(const Config *cfg, size_t pattern_idx) {
  assert(cfg);
  assert(cfg->patterns);
  assert(pattern_idx < cfg->path_count);
  return cfg->patterns[pattern_idx];
}

const char* config_type_show(const ConfigStatusType err) {
  switch (err) {
    case CONFIG_OK:             return "ok";
    case CONFIG_MISSING:        return "missing";
    case CONFIG_WRONG_TYPE:     return "wrong type";
    case CONFIG_NOT_PROCESSED:  return "config not processed";
    case CONFIG_MAX_RULES:      return "Maximum rules exceeded";
    case CONFIG_PATTERN_LENGTH: return "config pattern too long";
    case CONFIG_SYSTEM_ERR:     return "system error";
    case CONFIG_INVALID_VAL:    return "invalid value in key";
    case CONFIG_PARSE:          return "unable to parse";
    case CONFIG_FILE:           return "file system error";
    default:                    return "unkown error";
  }
}

void config_status_show(ConfigStatus s) {
  fprintf(stderr, "config: %s: %s", s.key, config_type_show(s.kind));
  s.sys_errno != 0 ? fprintf(stderr, " %s\n", strerror(s.sys_errno)) : fprintf(stderr, "\n");
}

static ConfigStatus config_enum_parse(toml_datum_t table, const char *key, const ParsePair *names, int *out) {
  const char *s;
  ConfigStatus err = config_get_string(table, key, &s);
  if (err.kind != CONFIG_OK) return err;

  const ParsePair *p = names;
  while (p->name != NULL) {
    if (strcmp(s, p->name) == 0) {
      *out = p->value;
      return config_status_ok();
    }

    p++;
  }

  return config_status_failf(CONFIG_INVALID_VAL, key, "'%s'", s);
}

static ConfigStatus config_enum_or(toml_datum_t table, const char *key, const ParsePair *names, int *out, int or) {
  ConfigStatus status = config_enum_parse(table, key, names, out);
  if (status.kind == CONFIG_MISSING) {
    *out = or; 
    return config_status_ok();
  }
  return status;
}

static ssize_t config_rule_pattern_match(const Config *cfg, const Rule *r, const char *fname) {
  for (size_t i = r->pattern_start; i < r->pattern_start + r->pattern_count; i++) {
    if (fnmatch(cfg->patterns[i], fname, FNM_PERIOD) == 0) return i;
  }

  return -1;
}

static Rule *config_rule_match(const Config *cfg, size_t start, size_t count, const char *fname) {
  for (size_t i = start; i < start + count; i++) {
    Rule *r = &cfg->rules[i];
    if (config_rule_pattern_match(cfg, r, fname) != -1) return r;
  }

  return NULL;
}

static int config_exclude(const Config *cfg, const size_t start, const size_t count, const char *fname) {
  for (size_t i = start; i < start + count; i++) {
    if (fnmatch(cfg->patterns[i], fname, 0) == 0) return 0;
  }

  return 1;
}

const Rule *config_watch_rule_match(const Config *cfg, const Watch *w, const char *fname) {
  // global exclude check
  if (config_exclude(cfg, 0, cfg->exclude_count, fname) == 0) return NULL;

  // scoped watch exclude check
  if (config_exclude(cfg, w->exclude_start, w->exclude_count, fname) == 0) return NULL;

  // retrieve a matched rule from a watch and if it is not excluded, return it
  Rule *r = config_rule_match(cfg, w->rule_start, w->rule_count, fname);
  if (r && config_exclude(cfg, r->exclude_start, r->exclude_count, fname) != 0) return r; 

  // retrieve a matched rule from the global rules and if it is not excluded, return it
  r = config_rule_match(cfg, 0, cfg->global_rule_count, fname);
  if (r && config_exclude(cfg, r->exclude_start, r->exclude_count, fname) != 0) return r; 

  return NULL;
}

static void *config_realloc(void *arr, size_t new_count, size_t elem_size) {
  assert(elem_size > 0);

  if (new_count > SIZE_MAX / elem_size) {
    errno = ENOMEM;
    return NULL;
  }

  return realloc(arr, new_count * elem_size);
}

static ConfigStatus config_path_intern(Config *cfg, const char *key, const char *raw, ssize_t *idx) {
  assert(cfg && key && raw && idx);
  CanonicalPath path = {0};

  ConfigStatus status = config_status_from_file(file_normalize(raw, &path), key);
  if (status.kind != CONFIG_OK) return status;

  for (size_t i = 0; i < cfg->path_count; i++) {
    if (strcmp(cfg->paths[i].s, path.s) == 0) {
      *idx = i;
      return config_status_ok();
    }
  }

  void *new_arr = config_realloc(cfg->paths, cfg->path_count + 1, sizeof(path));
  if (!new_arr) return config_status_from_sys(CONFIG_SYSTEM_ERR, errno, key);

  cfg->paths = new_arr;
  cfg->paths[cfg->path_count] = path;
  *idx = cfg->path_count;
  cfg->path_count++;
  return config_status_ok();
}

static ConfigStatus config_pattern_append(Config *cfg, const char *pattern) {
  char *duped_pattern = strdup(pattern);
  if (!duped_pattern) return config_status_from_sys(CONFIG_SYSTEM_ERR, errno, CONFIG_RULE_MATCH);

  void *new_arr = config_realloc(cfg->patterns, cfg->pattern_count + 1, sizeof(*cfg->patterns));
  if (!new_arr) return config_status_from_sys(CONFIG_SYSTEM_ERR, errno, CONFIG_RULE_MATCH);

  cfg->patterns = new_arr;
  cfg->patterns[cfg->pattern_count] = duped_pattern;
  cfg->pattern_count++;
  return config_status_ok();
}

static ConfigStatus config_rule_append(Config *cfg, const Rule *rule) {
  void *new_arr = config_realloc(cfg->rules, cfg->total_rule_count + 1, sizeof(*cfg->rules));
  if (!new_arr) return config_status_from_sys(CONFIG_SYSTEM_ERR, errno, CONFIG_RULE);

  cfg->rules = new_arr;
  cfg->rules[cfg->total_rule_count] = *rule;
  cfg->total_rule_count++;
  return config_status_ok();
}

static ConfigStatus config_watch_append(Config *cfg, const Watch *watch) {
  void *new_arr = config_realloc(cfg->watches, cfg->watch_count + 1, sizeof(*cfg->watches));
  if (!new_arr) return config_status_from_sys(CONFIG_SYSTEM_ERR, errno, CONFIG_WATCH);

  cfg->watches = new_arr;
  cfg->watches[cfg->watch_count] = *watch;
  cfg->watch_count++;
  return config_status_ok();
}

static ConfigStatus config_exclude_parse(const toml_datum_t tab, Config *cfg, size_t *count) {
  toml_datum_t ignore_list;
  ConfigStatus status = config_get(tab, CONFIG_EXCLUDE, TOML_ARRAY, &ignore_list);
  if (status.kind == CONFIG_MISSING) return config_status_ok();
  else if (status.kind != CONFIG_OK) return status;
  size_t pattern_start = cfg->pattern_count;
  
  for (int i = 0; i < ignore_list.u.arr.size; i++) {
    toml_datum_t ignore = ignore_list.u.arr.elem[i];
    if (ignore.type != TOML_STRING) {
      return config_status_failf(CONFIG_INVALID_VAL, CONFIG_EXCLUDE, "'%s'", ignore.u.s);
    }

    status = config_pattern_append(cfg, ignore.u.s);
    if (status.kind != CONFIG_OK) return status;
  }

  *count = cfg->pattern_count - pattern_start;
  return config_status_ok();
}


static ConfigStatus config_rules_parse(Config *cfg, toml_datum_t parent) {
  assert(cfg);

  ConfigStatus status = config_status_ok();

  for (int i = 0; i < parent.u.arr.size; i++) {
    Rule rule = {0};
    int action, conflict;
    toml_datum_t matches;
    const char *path;
    toml_datum_t table = parent.u.arr.elem[i];

    rule.exclude_start = cfg->pattern_count;
    if (status.kind == CONFIG_OK) status = config_exclude_parse(table, cfg, &rule.exclude_count);

    if (status.kind == CONFIG_OK) status = config_enum_parse(table, CONFIG_RULE_ACTION, action_pairs, &action);
    if (status.kind != CONFIG_OK) return status;
    rule.action = action;

    if(rule.action == DELETE) {
      rule.conflict = -1;
      rule.destination_idx = -1;
    }
    else {
      status = config_enum_or(table, CONFIG_RULE_CONFLICT, conflict_pairs, &conflict, RENAME);
      if (status.kind != CONFIG_OK) return status;
      rule.conflict = conflict;

      ConfigStatus err = config_get_string(table, CONFIG_RULE_DEST, &path);
      if (err.kind == CONFIG_OK) status = config_path_intern(cfg, CONFIG_RULE_DEST, path, &rule.destination_idx);
      if (status.kind != CONFIG_OK) return status;
    }

    status = config_get(table, CONFIG_RULE_MATCH, TOML_ARRAY, &matches);
    if (status.kind != CONFIG_OK) return status;

    // loop through matches
    rule.pattern_start = cfg->pattern_count;

    for (int i = 0; i < matches.u.arr.size; i++) {
      toml_datum_t match = matches.u.arr.elem[i];
      if (match.type != TOML_STRING) {
        return config_status_failf(CONFIG_INVALID_VAL, CONFIG_RULE_MATCH, "'%s'", match.u.s);
      }

      status = config_pattern_append(cfg, match.u.s);
      if (status.kind == CONFIG_OK) rule.pattern_count++;
      else return status;
    }

    status = config_rule_append(cfg, &rule);
    if (status.kind != CONFIG_OK) return status;
  }

  return status;
}

static ConfigStatus config_watches_parse(Config *cfg, toml_datum_t watches) {
  assert(cfg);
  Watch watch = {0};
  toml_datum_t rules;
  const char *path;
  ConfigStatus status = config_status_ok();

  for (int i = 0; i < watches.u.arr.size; i++) {
    toml_datum_t table = watches.u.arr.elem[i];
    // watch rules are optional
    if (status.kind == CONFIG_OK) status = config_get_string(table,  CONFIG_WATCH_PATH, &path);
    if (status.kind == CONFIG_OK) status = config_path_intern(cfg, CONFIG_WATCH_PATH, path, &watch.path_idx);
    if (status.kind == CONFIG_OK) status = config_get_int(table, CONFIG_WATCH_SETTLE_SEC, &watch.settle_sec);
    if (status.kind == CONFIG_OK) watch.exclude_start = cfg->pattern_count;
    if (status.kind == CONFIG_OK) status = config_exclude_parse(table, cfg, &watch.exclude_count);
    if (status.kind == CONFIG_OK) watch.rule_start = cfg->total_rule_count;
    if (status.kind == CONFIG_OK) status = config_get(table, CONFIG_RULE, TOML_ARRAY, &rules);
    if (status.kind == CONFIG_OK) status = config_rules_parse(cfg, rules);
    if (status.kind == CONFIG_MISSING && cfg->global_rule_count > 0) status = config_status_ok(); 
    if (status.kind == CONFIG_OK) watch.rule_count = cfg->total_rule_count - watch.rule_start;
    if (status.kind == CONFIG_OK) status = config_watch_append(cfg, &watch);
    else return status;
  }

  return status;
}

static ConfigStatus config_parse(Config *cfg, const toml_result_t *result) {
  assert(!cfg->rules && cfg->global_rule_count == 0 && cfg->total_rule_count == 0);
  assert(!cfg->watches && cfg->watch_count == 0);

  toml_datum_t rules, watches;

                ConfigStatus err = config_get_int(result->toptab, CONFIG_VERSION, &cfg->version); 
  if (err.kind == CONFIG_OK) err = config_get_bool_or(result->toptab, CONFIG_DRY_RUN, &cfg->dry_run, 0); 
  if (err.kind == CONFIG_OK) err = config_exclude_parse(result->toptab, cfg, &cfg->exclude_count);
  if (err.kind == CONFIG_OK) err = config_get(result->toptab, CONFIG_RULE, TOML_ARRAY, &rules); 
  if (err.kind == CONFIG_OK) err = config_rules_parse(cfg, rules); 
  if (err.kind == CONFIG_OK) cfg->global_rule_count = cfg->total_rule_count;
  if (err.kind == CONFIG_OK) err = config_get(result->toptab, CONFIG_WATCH, TOML_ARRAY, &watches); 
  if (err.kind == CONFIG_OK) err = config_watches_parse(cfg, watches);                                  
  return err;
}

static void config_reset(Config *cfg) {
  cfg->global_rule_count = 0;
  cfg->total_rule_count  = 0;
  cfg->path_count        = 0;
  cfg->pattern_count     = 0;
  cfg->watch_count       = 0;
  cfg->exclude_count     = 0;
  cfg->rules          = NULL; 
  cfg->paths          = NULL;
  cfg->patterns       = NULL;
  cfg->watches        = NULL;
}

void config_destroy(Config *cfg) {
  for (size_t i = 0; i < cfg->pattern_count; i++) {
    if (cfg->patterns[i]) free(cfg->patterns[i]);
  }
  if (cfg->paths)    free(cfg->paths);
  if (cfg->watches)  free(cfg->watches);
  if (cfg->rules)    free(cfg->rules);
  if (cfg->patterns) free(cfg->patterns);
}

ConfigStatus config_build(Config *cfg, const CanonicalPath *config_path) {
  config_reset(cfg);

  const toml_result_t result = toml_parse_file_ex(config_path->s);
  if (!result.ok) return config_status_failf(CONFIG_PARSE, NULL, "%s", result.errmsg);

  ConfigStatus status = config_parse(cfg, &result);
  if (status.kind != CONFIG_OK) config_destroy(cfg);

  toml_free(result);
  return status;
}
