//
// Created by redonxharja on 11/12/26.
//

#include "rules.h"
#include "file.h"
#include <inttypes.h>
#include <linux/limits.h>
#include <string.h>
#include <tomlc17.h>
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <fnmatch.h>
#include <config.h>

static ConfigStatus config_status_ok(void) {
  return (ConfigStatus) { .kind = CONFIG_OK };
}

static ConfigStatus config_status_build(ConfigStatusType kind, int sys_errno, const char *key) {
  return (ConfigStatus) {
    .kind = kind,
    .key = key,
    .sys_errno = sys_errno,
    .err_msg = ""
  };
}

static ConfigStatus config_type_check(const toml_type_t type, const toml_type_t expected, const char *key) {
  if (type  == TOML_UNKNOWN) return (ConfigStatus){CONFIG_MISSING, 0, key, ""};
  if (type != expected) return (ConfigStatus){CONFIG_WRONG_TYPE, 0, key, ""};
  return (ConfigStatus){CONFIG_OK, 0, NULL, ""};
}

static ConfigStatus config_get(const toml_datum_t datum, const char *key, const toml_type_t expected, toml_datum_t *out) {
  const toml_datum_t d = toml_seek(datum, key);
  ConfigStatus err = config_type_check(d.type, expected, key); 
  if (err.kind != CONFIG_OK) return err;
  *out = d;
  return err;
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
    default:                    return "unkown error";
  }
}

void rules_reset(Rules *r) {
  r->dir_paths = NULL; r->dir_count = 0;
  r->watch_paths = NULL; r->watch_path_count = 0;
  r->rule_set_count = 0;
}

static ConfigStatus rules_set_add(Rules *out, const char *match, ssize_t target_idx) {
  assert(out);
  assert(match);

  if (out->rule_set_count == MAX_RULES) return config_status_build(CONFIG_MAX_RULES, 0, NULL);

  Rule rule = (Rule){ .target_dir_idx = target_idx };
  int written = snprintf(rule.match, sizeof(rule.match), "%s", match);
  if (written != (int)strlen(match)) return config_status_build(CONFIG_PATTERN_LENGTH, 0, NULL);

  out->rule_set[out->rule_set_count++] = rule;
  return config_status_ok();
}

ssize_t rules_set_path_find_idx(const Rules *rules, const char *string) {
  for (size_t i = 0; i < rules->rule_set_count; i++) {
    Rule rule = rules->rule_set[i];
    if (fnmatch(rule.match, string, FNM_PERIOD | FNM_CASEFOLD) == 0) {
      return rule.target_dir_idx;
    }
  }

  return CONFIG_IGNORE_IDX;
}

static ConfigStatus rules_watch_build(Rules *out, const toml_result_t *result) {
  assert(out);
  assert(result);

  toml_datum_t config_watch_tables;

  ConfigStatus err = config_get(result->toptab, "watch", TOML_ARRAY, &config_watch_tables);
  if (err.kind != CONFIG_OK) return err;

  out->watch_paths = calloc(config_watch_tables.u.arr.size, sizeof(out->watch_paths[0]));
  if(!out->watch_paths) return config_status_build(CONFIG_SYSTEM_ERR, errno, "calloc watch paths");
  out->watch_path_count = config_watch_tables.u.arr.size;

  for (size_t i = 0; i < out->watch_path_count; i++) {
    const toml_datum_t watch_table = config_watch_tables.u.arr.elem[i];
    err = config_type_check(watch_table.type, TOML_TABLE, CONFIG_WATCH_TABLE);
    if (err.kind != CONFIG_OK) return err;

    toml_datum_t config_watch_path; 
    err = config_get(watch_table, "path", TOML_STRING, &config_watch_path);
    if (err.kind != CONFIG_OK) return err;

    FileStatus fs = file_normalize(config_watch_path.u.s, out->watch_paths[i].path.s);
    if (fs.kind != FILE_OK && fs.kind != FILE_ERR_NOT_FOUND) {
      err = config_status_build(CONFIG_BAD_PATH, 0, "path");
      err.file = fs;
      return err;
    }

    toml_datum_t config_watch_settle_sec; 
    err = config_get(watch_table, CONFIG_WATCH_SETTLE_SECONDS, TOML_INT64, &config_watch_settle_sec);
    if (err.kind == CONFIG_MISSING) out->watch_paths[i].settle_sec = CONFIG_DEFAULT_SETTLE_SECONDS;
    else if (err.kind == CONFIG_OK) out->watch_paths[i].settle_sec = config_watch_settle_sec.u.int64;
    else return err;

    // if any ignores exist we can process them here
    toml_datum_t config_watch_ignores;
    err = config_get(watch_table, "ignore", TOML_ARRAY, &config_watch_ignores);

    if (err.kind != CONFIG_MISSING && err.kind != CONFIG_OK) return err;

    if (err.kind == CONFIG_OK) {
      for (int32_t k = 0; k < config_watch_ignores.u.arr.size; k++) {
        const toml_datum_t ignore = config_watch_ignores.u.arr.elem[k];

        err = config_type_check(ignore.type, TOML_STRING, CONFIG_WATCH_IGNORE);
        if (err.kind != CONFIG_OK) return err;

        err = rules_set_add(out, ignore.u.s, CONFIG_IGNORE_IDX);
        err.key = CONFIG_WATCH_IGNORE;
        if(err.kind != CONFIG_OK) return err;
      }
    }
  }

  return config_status_ok();
}

static ConfigStatus rules_dest_build(Rules *out, const toml_result_t *result) {
  assert(out);
  assert(result);

  toml_datum_t default_path;
  ConfigStatus err = config_get(result->toptab, CONFIG_DEFAULTS_DEST_PATH, TOML_STRING, &default_path);
  if (err.kind != CONFIG_OK) return err;

  toml_datum_t config_dest_tables;
  err = config_get(result->toptab, CONFIG_DEST_TABLE, TOML_ARRAY, &config_dest_tables);
  if (err.kind != CONFIG_OK) return err;

  // N dest dirs and 1 default
  out->dir_paths = calloc(config_dest_tables.u.arr.size, sizeof(CanonicalPath));
  if(!out->dir_paths) return config_status_build(CONFIG_SYSTEM_ERR, errno, "calloc");
  out->dir_count = config_dest_tables.u.arr.size;

  for (size_t i = 0; i < out->dir_count; i++) {
    const toml_datum_t dest_table = config_dest_tables.u.arr.elem[i];
    err = config_type_check(dest_table.type, TOML_TABLE, CONFIG_DEST_TABLE);
    if (err.kind != CONFIG_OK) return err;

    toml_datum_t config_dest_path; 
    err = config_get(dest_table, "path", TOML_STRING, &config_dest_path);
    if (err.kind != CONFIG_OK) return err;

    FileStatus fs = file_normalize(config_dest_path.u.s, out->dir_paths[i].s);
    if (fs.kind != FILE_OK && fs.kind != FILE_ERR_NOT_FOUND) {
      err = config_status_build(CONFIG_BAD_PATH, 0, "path");
      err.file = fs;
      return err;
    }

    toml_datum_t config_dest_targets;
    err = config_get(dest_table, CONFIG_DEST_EXTENSIONS, TOML_ARRAY, &config_dest_targets);
    if (err.kind != CONFIG_OK) return err;

    for (int32_t j = 0; j < config_dest_targets.u.arr.size; j++) {
      const toml_datum_t targets = config_dest_targets.u.arr.elem[j];
      err = config_type_check(targets.type, TOML_STRING, CONFIG_DEST_EXTENSIONS);
      if (err.kind != CONFIG_OK) return err;

      err = rules_set_add(out, targets.u.s, i);
      err.key = CONFIG_DEST_EXTENSIONS;
      if (err.kind != CONFIG_OK) return err;
    }
  }

  return config_status_ok();
}

ConfigStatus rules_build(Rules *out, const char *config_path) {
    assert(out);
    assert(config_path);

    rules_reset(out);

    const toml_result_t result = toml_parse_file_ex(config_path);

    if (!result.ok) {
      ConfigStatus status = config_status_build(CONFIG_PARSE, 0, NULL);
      snprintf(status.err_msg, sizeof (status.err_msg), "%s", result.errmsg);
      return status;
    }

    ConfigStatus err = rules_watch_build(out, &result); 
    if (err.kind == CONFIG_OK) err = rules_dest_build(out, &result); 
    if (err.kind != CONFIG_OK) rules_deinit(out);

    toml_free(result);
    return err;
}

void config_status_show(ConfigStatus s) {
  fprintf(stderr, "config: %s: %s", s.key, config_type_show(s.kind));
  s.sys_errno != 0 ? fprintf(stderr, " %s\n", strerror(s.sys_errno)) : fprintf(stderr, "\n");
}

void rules_deinit(Rules *rules) {
  if (!rules) return;
  if (rules->dir_paths) free(rules->dir_paths);
  if (rules->watch_paths) free(rules->watch_paths);
  rules_reset(rules);
}

