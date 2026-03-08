/**
 * @file validator.c
 * @brief Validator registry — Dilithium-3 public keys and locked stake.
 */

#include "validator.h"
#include "log.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef _WIN32
#  include <direct.h>
#  define mkdir(path, mode) _mkdir(path)
#else
#  include <sys/stat.h>
#endif

#define VALIDATORS_DIR ".chain/validators"
#define PATH_BUF       512
#define INITIAL_CAP    8

/* ── private struct ───────────────────────────────────────────────────── */

struct ValidatorRegistry {
  Validator    *entries;
  unsigned int  count;
  unsigned int  capacity;
};

/* ── internal helpers ─────────────────────────────────────────────────── */

static int ensure_dir(void) {
  /* .chain/ may not exist if the storage module has not been initialised yet */
  if (mkdir(".chain", 0755) == -1 && errno != EEXIST) {
    log_error("validator: cannot create .chain: %s", strerror(errno));
    return EXIT_FAILURE;
  }
  if (mkdir(VALIDATORS_DIR, 0755) == -1 && errno != EEXIST) {
    log_error("validator: cannot create %s: %s", VALIDATORS_DIR, strerror(errno));
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}

static int registry_grow(ValidatorRegistry *reg) {
  unsigned int  new_cap = reg->capacity == 0 ? INITIAL_CAP : reg->capacity * 2;
  Validator    *p       = realloc(reg->entries, new_cap * sizeof(Validator));
  if (!p) {
    log_error("validator: realloc failed (capacity %u → %u)", reg->capacity, new_cap);
    return EXIT_FAILURE;
  }
  reg->entries  = p;
  reg->capacity = new_cap;
  return EXIT_SUCCESS;
}

/* Write one Validator to disk. Idempotent — overwrites existing file. */
static int persist(const Validator *v) {
  if (ensure_dir() != EXIT_SUCCESS) return EXIT_FAILURE;

  char path[PATH_BUF];
  snprintf(path, sizeof(path), "%s/%s", VALIDATORS_DIR, v->id);

  FILE *f = fopen(path, "wb");
  if (!f) {
    log_error("validator: cannot open %s for write: %s", path, strerror(errno));
    return EXIT_FAILURE;
  }

  size_t n = fwrite(v, sizeof(Validator), 1, f);
  fflush(f);
  fsync(fileno(f));
  fclose(f);

  if (n != 1) {
    log_error("validator: short write for '%s'", v->id);
    remove(path);
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}

/* ── public API ───────────────────────────────────────────────────────── */

ValidatorRegistry *validator_registry_load(void) {
  ValidatorRegistry *reg = calloc(1, sizeof(ValidatorRegistry));
  if (!reg) {
    log_error("validator_registry_load: calloc failed");
    return NULL;
  }

  DIR *d = opendir(VALIDATORS_DIR);
  if (!d) {
    log_debug("validator_registry_load: %s not found; starting empty", VALIDATORS_DIR);
    return reg; /* empty registry — not an error */
  }

  struct dirent *e;
  while ((e = readdir(d)) != NULL) {
    if (e->d_name[0] == '.') continue;

    char path[PATH_BUF];
    snprintf(path, sizeof(path), "%s/%s", VALIDATORS_DIR, e->d_name);

    FILE *f = fopen(path, "rb");
    if (!f) continue;

    Validator v;
    size_t n = fread(&v, sizeof(Validator), 1, f);
    fclose(f);

    if (n != 1) {
      log_warn("validator_registry_load: skipping corrupt entry '%s'", e->d_name);
      continue;
    }

    if (reg->count >= reg->capacity) {
      if (registry_grow(reg) != EXIT_SUCCESS) {
        log_error("validator_registry_load: out of memory at entry %u", reg->count);
        break;
      }
    }
    reg->entries[reg->count++] = v;
  }
  closedir(d);

  log_info("validator_registry_load: loaded %u validator(s)", reg->count);
  return reg;
}

void validator_registry_free(ValidatorRegistry *reg) {
  if (!reg) return;
  free(reg->entries);
  free(reg);
}

int validator_register(ValidatorRegistry *reg, const Validator *v) {
  if (!reg || !v) {
    log_error("validator_register: NULL argument");
    return EXIT_FAILURE;
  }
  if (v->id[0] == '\0') {
    log_error("validator_register: empty id");
    return EXIT_FAILURE;
  }

  /* Upsert: update in-place if ID already exists */
  for (unsigned int i = 0; i < reg->count; i++) {
    if (strncmp(reg->entries[i].id, v->id, VALIDATOR_ID_SIZE) == 0) {
      reg->entries[i] = *v;
      log_info("validator_register: updated '%s' (stake=%llu)",
               v->id, (unsigned long long)v->stake);
      return persist(v);
    }
  }

  /* New entry */
  if (reg->count >= reg->capacity) {
    if (registry_grow(reg) != EXIT_SUCCESS) {
      log_error("validator_register: out of memory");
      return EXIT_FAILURE;
    }
  }
  reg->entries[reg->count++] = *v;
  log_info("validator_register: registered '%s' (stake=%llu)",
           v->id, (unsigned long long)v->stake);
  return persist(v);
}

const Validator *validator_lookup(const ValidatorRegistry *reg, const char *id) {
  if (!reg || !id || id[0] == '\0') return NULL;
  for (unsigned int i = 0; i < reg->count; i++) {
    if (strncmp(reg->entries[i].id, id, VALIDATOR_ID_SIZE) == 0)
      return &reg->entries[i];
  }
  return NULL;
}

int validator_check_stake(const ValidatorRegistry *reg, const char *id) {
  const Validator *v = validator_lookup(reg, id);
  if (!v) {
    log_warn("validator_check_stake: '%s' not found", id ? id : "(null)");
    return EXIT_FAILURE;
  }
  if (v->stake < VALIDATOR_MIN_STAKE) {
    log_warn("validator_check_stake: '%s' stake %llu < minimum %llu",
             v->id,
             (unsigned long long)v->stake,
             (unsigned long long)VALIDATOR_MIN_STAKE);
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}

unsigned int validator_count(const ValidatorRegistry *reg) {
  return reg ? reg->count : 0;
}
