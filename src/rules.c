//
// Created by redonxharja on 11/12/26.
//

#include "rules.h"
#include "file.h"
#include "dictionary.h"
#include <tomlc17.h>
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

static int rules_dict_insert(const Rules *rules, const char *key, const void *idx) {
    assert(rules);
    assert(rules->by_ext);
    assert(rules->by_name);
    assert(key);

    if (key[0] == '.') return dict_insert(rules->by_ext, key, IDX_TO_VAL(idx));
    return dict_insert(rules->by_name, key, IDX_TO_VAL(idx));
}

static int rules_watch_build(Rules *out, const toml_result_t *result) {
    const toml_datum_t config_watch_tables = toml_seek(result->toptab, "watch");
    if (config_watch_tables.type != TOML_ARRAY) {
        fprintf(stderr, "ERROR: missing or invalid '%s' property in config", "watch");
        return -1;
    }

    out->watch_path_count = config_watch_tables.u.arr.size;
    out->watch_paths = calloc(out->watch_path_count, sizeof(CanonicalPath));
    if(!out->watch_paths) {
        perror("calloc");
        return -1;
    }

    for (size_t i = 0; i < out->watch_path_count; i++) {
        const toml_datum_t watch_table = config_watch_tables.u.arr.elem[i];
        if (watch_table.type != TOML_TABLE) {
            fprintf(stderr, "ERROR: missing or invalid '%s' property in config", "watch");
            return -1;
        }

        const toml_datum_t config_watch_path = toml_get(watch_table, "path");
        if (config_watch_path.type != TOML_STRING) {
            fprintf(stderr, "ERROR: missing or invalid '%s' property in config", "path");
            return -1;
        }

        if (file_normalize(config_watch_path.u.s, out->watch_paths[i].s) != 0 && errno != ENOENT) return -1;

        const toml_datum_t config_watch_ignores = toml_get(watch_table, "ignore");
        if (config_watch_ignores.type != TOML_ARRAY) {
            fprintf(stderr, "ERROR: missing or invalid '%s' property in config", "ignore");
            return -1;
        }

        for (int32_t k = 0; k < config_watch_ignores.u.arr.size; k++) {
            const toml_datum_t ignore = config_watch_ignores.u.arr.elem[k];
            if (ignore.type != TOML_STRING) return -1;
            if(rules_dict_insert(out, ignore.u.s, RULE_EXCLUDE) != 1) return -1;
        }
    }

    return 0;
}

int rules_build(Rules *out, const char *config_path) {
    assert(out);
    assert(config_path);

    const toml_result_t result = toml_parse_file_ex(config_path);
    if (!result.ok) {
        perror(result.errmsg);
        return -1;
    }

    // 3 test dirs and 1 default
    out->dir_count = 3 + 1;
    out->dir_paths = calloc(out->dir_count, sizeof(CanonicalPath));
    if(!out->dir_paths) goto fail;

    out->by_name = dict_init();
    if (!out->by_name) goto fail;

    out->by_ext = dict_init();
    if (!out->by_ext) goto fail;

    // default
    const toml_datum_t default_path = toml_seek(result.toptab, CONFIG_DEFAULTS_DEST_PATH);
    if (default_path.type != TOML_STRING) {
        fprintf(stderr, "ERROR: missing or invalid '%s' property in config", CONFIG_DEFAULTS_DEST_PATH);
        goto fail;
    }

    out->fallback_idx = 0;
    out->dir_paths[out->fallback_idx] = (CanonicalPath){ .s = *default_path.u.s };

    if (rules_watch_build(out, &result) != 0) goto fail;

    // todo: normalize dirs properly here
    for (size_t i = 1; i < out->dir_count; i++) {
        char *dirs[4] = {"", "./pdfs", "./zips", "./configs" };
        char *exts[4] = {"", ".pdf", ".zip", ".txt" };
        // convert relative to absolute, ENOENT is not handled here
        if (file_normalize(dirs[i], out->dir_paths[i].s) != 0 && errno != ENOENT) goto fail;
        if(dict_insert(out->by_ext, exts[i], IDX_TO_VAL(i)) != 1) goto fail;
    }

    toml_free(result);
    return 0;

fail:
    toml_free(result);
    rules_deinit(out);
    return -1;
}

static void kvp_key_free(void *p) {
    Kvp *kvp = p;
    free((void*)kvp->key);
}

void rules_deinit(Rules *out) {
    if (!out) return;
    free(out->dir_paths);
    free(out->watch_paths);
    if(out->by_name) dict_free(out->by_name, kvp_key_free);
    if(out->by_ext) dict_free(out->by_ext, kvp_key_free);
}
