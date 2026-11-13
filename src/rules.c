//
// Created by redonxharja on 11/12/26.
//

#include "rules.h"
#include "file.h"
#include "dictionary.h"
#include <linux/limits.h>
#include <tomlc17.h>
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

static config_error_t err_init() {
  return (config_error_t) {
    .kind = CONFIG_NOT_PROCESSED,
    .key = NULL,
    .sys_errno = 0
  };
}

static int rules_dict_insert(const Rules *rules, const char *key, const void *idx) {
    assert(rules);
    assert(rules->by_ext);
    assert(rules->by_name);
    assert(key);

    if (key[0] == '.') return dict_insert(rules->by_ext, key, IDX_TO_VAL(idx));
    return dict_insert(rules->by_name, key, IDX_TO_VAL(idx));
}

static config_error_t config_type_check(const toml_type_t type, const toml_type_t expected, const char *key) {
  config_error_t err = err_init();
  if (type  == TOML_UNKNOWN) return (config_error_t){CONFIG_MISSING, 0, key};
  if (type != expected) return (config_error_t){CONFIG_WRONG_TYPE, 0, key};
  return (config_error_t){CONFIG_OK, 0, NULL};
}

static config_error_t config_get(const toml_datum_t datum, const char *key, toml_type_t expected, toml_datum_t *out) {
  config_error_t err = err_init();
  const toml_datum_t d = toml_seek(datum, key);
  err = config_type_check(d.type, expected, key); 
  if (err.kind != CONFIG_OK) return err;
  *out = d;
  return err;
}

const char* rules_config_strerror(config_parse_err err) {
  switch (err) {
    case CONFIG_OK:         return "ok";
    case CONFIG_MISSING:    return "missing";
    case CONFIG_WRONG_TYPE: return "wrong type";
    default:                return "unkown error";
  }
}

static config_error_t rules_watch_build(Rules *out, const toml_result_t *result) {
    toml_datum_t config_watch_tables;
    config_error_t err = err_init();

    err = config_get(result->toptab, "watch", TOML_ARRAY, &config_watch_tables);
    if (err.kind != CONFIG_OK) goto fail;

    out->watch_path_count = config_watch_tables.u.arr.size;
    out->watch_paths = calloc(out->watch_path_count, sizeof(CanonicalPath));
    if(!out->watch_paths) {
      err.sys_errno = errno;
      goto fail;
    }

    for (size_t i = 0; i < out->watch_path_count; i++) {
        const toml_datum_t watch_table = config_watch_tables.u.arr.elem[i];
        err = config_type_check(watch_table.type, TOML_TABLE, CONFIG_WATCH_TABLE);
        if (err.kind != CONFIG_OK) goto fail;

        toml_datum_t config_watch_path; 
        err = config_get(watch_table, "path", TOML_STRING, &config_watch_path);
        if (err.kind != CONFIG_OK) goto fail;

        if (file_normalize(config_watch_path.u.s, out->watch_paths[i].s) != 0 && errno != ENOENT) goto fail;

        toml_datum_t config_watch_ignores;
        err = config_get(watch_table, "ignore", TOML_ARRAY, &config_watch_ignores);
        if (err.kind != CONFIG_OK) goto fail;

        for (int32_t k = 0; k < config_watch_ignores.u.arr.size; k++) {
            const toml_datum_t ignore = config_watch_ignores.u.arr.elem[k];
            err = config_type_check(ignore.type, TOML_STRING, CONFIG_WATCH_IGNORE);
            if (err.kind != CONFIG_OK) goto fail;
            if(rules_dict_insert(out, ignore.u.s, RULE_EXCLUDE) != 1) {
              err.sys_errno = errno;
              goto fail;
            }
        }
    }

  err.kind = CONFIG_OK;
  return err;

fail:
    if (out->watch_paths) free(out->watch_paths);
    return err;
}

static config_error_t rules_dest_build(Rules *out, const toml_result_t *result) {
    config_error_t err = err_init();

    toml_datum_t default_path;
    err = config_get(result->toptab, CONFIG_DEFAULTS_DEST_PATH, TOML_STRING, &default_path);
    if (err.kind != CONFIG_OK) goto fail;

    toml_datum_t config_dest_tables;
    err = config_get(result->toptab, CONFIG_DEST_TABLE, TOML_ARRAY, &config_dest_tables);
    if (err.kind != CONFIG_OK) goto fail;

    // N dest dirs and 1 default
    out->dir_count = 1 + config_dest_tables.u.arr.size;
    out->dir_paths = calloc(out->dir_count, sizeof(CanonicalPath));
    if(!out->dir_paths) {
      err.sys_errno = errno;
      goto fail;
    }

    out->fallback_idx = RULES_FALLBACK_IDX;
    if (snprintf(out->dir_paths[RULES_FALLBACK_IDX].s, PATH_MAX, "%s", default_path.u.s) == -1) {
      err.sys_errno = errno;
      goto fail;
    }

    for (size_t i = 0; i < out->dir_count - 1; i++) {
        const toml_datum_t dest_table = config_dest_tables.u.arr.elem[i];
        err = config_type_check(dest_table.type, TOML_TABLE, CONFIG_DEST_TABLE);
        if (err.kind != CONFIG_OK) goto fail;

        toml_datum_t config_dest_path; 
        err = config_get(dest_table, "path", TOML_STRING, &config_dest_path);
        if (err.kind != CONFIG_OK) goto fail;

        if (file_normalize(config_dest_path.u.s, out->dir_paths[i].s) != 0 && errno != ENOENT) goto fail;

        toml_datum_t config_dest_targets;
        err = config_get(dest_table, CONFIG_DEST_EXTENSIONS, TOML_ARRAY, &config_dest_targets);
        if (err.kind != CONFIG_OK) goto fail;

        for (int32_t j = 0; j < config_dest_targets.u.arr.size; j++) {
            const toml_datum_t targets = config_dest_targets.u.arr.elem[j];
            err = config_type_check(targets.type, TOML_STRING, CONFIG_DEST_EXTENSIONS);
            if (err.kind != CONFIG_OK) goto fail;
            if(rules_dict_insert(out, targets.u.s, IDX_TO_VAL(i + 1)) != 1) {
              err.sys_errno = errno;
              goto fail;
            }
        }
    }

    err.kind = CONFIG_OK;
    return err;

fail:
    if (out->dir_paths) free(out->dir_paths);
    return err;
}

config_error_t rules_build(Rules *out, const char *config_path) {
    assert(out);
    assert(config_path);
    config_error_t err = err_init();

    const toml_result_t result = toml_parse_file_ex(config_path);
    if (!result.ok) {
      err.sys_errno = errno;
      goto fail;
    }

    out->by_name = dict_init();
    if (!out->by_name) {
      err.sys_errno = errno;
      goto fail;
    }

    out->by_ext = dict_init();
    if (!out->by_ext) {
      err.sys_errno = errno;
      goto fail;
    }

    err = rules_watch_build(out, &result); 
    if (err.kind != CONFIG_OK) goto fail;

    err = rules_dest_build(out, &result); 
    if (err.kind != CONFIG_OK) goto fail;

    toml_free(result);
    err.kind = CONFIG_OK;
    return err;

fail:
    if (!result.ok) toml_free(result);
    rules_deinit(out);
    return err;
}

static void kvp_key_free(void *p) {
    Kvp *kvp = p;
    free((void*)kvp->key);
}

void rules_deinit(Rules *out) {
    if (!out) return;
    if (out->dir_paths) free(out->dir_paths);
    if (out->watch_paths) free(out->watch_paths);
    if (out->by_name) dict_free(out->by_name, kvp_key_free);
    if (out->by_ext) dict_free(out->by_ext, kvp_key_free);
}
