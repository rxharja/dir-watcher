#include <stdlib.h>
#include <tomlc17.h>
#include "config.h"

static void error(const char *msg, const char *msg1) {
  fprintf(stderr, "ERROR: %s%s\n", msg, msg1 ? msg1 : "");
  exit(1);
}

int config_read() {
  // Parse the toml file
  toml_result_t result = toml_parse_file_ex("examples/config.toml");

  // Check for parse error
  if (!result.ok) {
    error(result.errmsg, 0);
  }

  // Extract values
  toml_datum_t path   = toml_seek(result.toptab, "watch.path");
  toml_datum_t ignore = toml_seek(result.toptab, "watch.ignore");

  // Print server.host
  if (path.type != TOML_STRING) {
    error("missing or invalid 'watch.path' property in config", 0);
  }

  // Print server.port
  if (ignore.type != TOML_ARRAY) {
    error("missing or invalid 'watch.ignore' property in config", 0);
  }

  for (int i = 0; i < ignore.u.arr.size; i++) {
    toml_datum_t elem = ignore.u.arr.elem[i];
    if (elem.type != TOML_STRING) {
      error("watch.ignore element not an integer", 0);
    }
    printf("%s%s", i ? ", " : "", elem.u.s);
  }
  printf("\n");

  // Negative test
  toml_datum_t dummy = toml_seek(result.toptab, "server.dummy");
  if (dummy.type != TOML_UNKNOWN) {
    printf("Error: dummy element exists\n");
    exit(1);
  }

  // Done!
  toml_free(result);
  return 0;
}
