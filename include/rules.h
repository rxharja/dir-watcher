//
// Created by redonxharja on 11/12/26.
//

#ifndef DIR_WATCHER_RULES_H
#define DIR_WATCHER_RULES_H
#include "dictionary.h"
#include "file.h"

// +1 so index 0 doesn't encode to NULL (dict_find's "absent" value)
#define IDX_TO_VAL(i) ((void *)(uintptr_t)((i) + 1))
#define VAL_TO_IDX(v) ((size_t)(uintptr_t)(v) - 1)
#define RULE_EXCLUDE ((void *)UINTPTR_MAX)

#define CONFIG_DEFAULTS_ON_CONFLICT "defaults.on_conflict"
#define CONFIG_DEFAULTS_DEST_PATH "defaults.dest_path"
#define CONFIG_DEFAULTS_DRY_RUN "defaults.dry_run"
#define CONFIG_WATCH_PATH "watch.path"
#define CONFIG_WATCH_IGNORE "watch.ignore"
#define CONFIG_WATCH_SETTLE_SECONDS "watch.settle_seconds"
#define CONFIG_DEST_PATH "dest.path"
#define CONFIG_DEST_EXTENSIONS "dest.extensions"

typedef struct {
    Dictionary *by_name;
    Dictionary *by_ext;

    CanonicalPath *dir_paths;
    size_t dir_count;

    CanonicalPath *watch_paths;
    size_t watch_path_count;

    size_t fallback_idx;
} Rules;

int rules_build(Rules *out, const char *config_path);

void rules_deinit(Rules *out);

#endif //DIR_WATCHER_RULES_H
