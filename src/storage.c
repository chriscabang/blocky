/**
 * @file storage.c
 * @brief Block object store and ref management.
 *
 * On-disk layout mirrors git's repository structure. HEAD is a symbolic
 * ref pointing to a branch file (e.g. refs/heads/main), which holds the
 * hash of the current chain tip. Inserting a block and advancing the
 * chain tip are intentionally separate operations.
 */

#include "storage.h"
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

#define STORAGE_DIR   ".chain/"
#define BLOCKS_DIR    ".chain/blocks/"
#define REFS_DIR      ".chain/refs/"
#define HEADS_DIR     ".chain/refs/heads/"
#define HEAD_FILE     ".chain/HEAD"
#define MAIN_REF      "refs/heads/main"
#define MAIN_REF_FILE ".chain/refs/heads/main"

#define REF_PREFIX     "ref: "
#define REF_PREFIX_LEN 5

#define PATH_BUF 512

/* ── internal helpers ─────────────────────────────────────────────────── */

static int init(void) {
  static const char *dirs[] = {
    STORAGE_DIR, BLOCKS_DIR, REFS_DIR, HEADS_DIR, NULL
  };

  for (int i = 0; dirs[i]; i++) {
    if (mkdir(dirs[i], 0755) == -1 && errno != EEXIST) {
      log_error("Failed to create %s: %s", dirs[i], strerror(errno));
      return EXIT_FAILURE;
    }
  }

  /* Write HEAD → refs/heads/main if it does not exist yet */
  if (access(HEAD_FILE, F_OK) != 0) {
    FILE *f = fopen(HEAD_FILE, "w");
    if (!f) {
      log_error("Failed to create HEAD: %s", strerror(errno));
      return EXIT_FAILURE;
    }
    fprintf(f, REF_PREFIX "%s", MAIN_REF);
    fflush(f);
    fsync(fileno(f));
    fclose(f);
    log_debug("HEAD initialized -> %s", MAIN_REF);
  }

  return EXIT_SUCCESS;
}

/* Write value to path, flushing to disk. */
static int ref_write(const char *path, const char *value) {
  FILE *f = fopen(path, "w");
  if (!f) {
    log_error("ref_write: cannot open %s: %s", path, strerror(errno));
    return EXIT_FAILURE;
  }
  fprintf(f, "%s", value);
  fflush(f);
  fsync(fileno(f));
  fclose(f);
  return EXIT_SUCCESS;
}

/* Read one line from path into buf, stripping any trailing newline. */
static int ref_read(const char *path, char *buf, size_t size) {
  FILE *f = fopen(path, "r");
  if (!f) {
    log_debug("ref_read: %s not found", path);
    return EXIT_FAILURE;
  }
  int ok = (fgets(buf, (int)size, f) != NULL);
  fclose(f);
  if (!ok) return EXIT_FAILURE;

  size_t len = strlen(buf);
  if (len > 0 && buf[len - 1] == '\n')
    buf[len - 1] = '\0';

  return EXIT_SUCCESS;
}

/* ── public API ───────────────────────────────────────────────────────── */

int storage_insert(const Block *block) {
  if (!block) {
    log_error("storage_insert: NULL block");
    return EXIT_FAILURE;
  }
  if (block->hash[0] == '\0') {
    log_error("storage_insert: block has empty hash");
    return EXIT_FAILURE;
  }

  if (init() != EXIT_SUCCESS) return EXIT_FAILURE;

  const char *hash = (const char *)block->hash;
  char path[PATH_BUF];
  snprintf(path, sizeof(path), "%s%s", BLOCKS_DIR, hash);

  /* Idempotent: already stored */
  if (access(path, F_OK) == 0) {
    log_debug("storage_insert: block %s already exists", hash);
    return EXIT_SUCCESS;
  }

  FILE *file = fopen(path, "wb");
  if (!file) {
    log_error("storage_insert: cannot open %s: %s", path, strerror(errno));
    return EXIT_FAILURE;
  }

  /* Write a copy with next zeroed — runtime pointer must not reach disk */
  Block copy = *block;
  copy.next  = NULL;

  size_t written = fwrite(&copy, sizeof(Block), 1, file);
  fflush(file);
  fsync(fileno(file));
  fclose(file);

  if (written != 1) {
    log_error("storage_insert: short write for block %s", hash);
    remove(path);
    return EXIT_FAILURE;
  }

  log_info("storage_insert: block %s stored", hash);
  return EXIT_SUCCESS;
}

Block *storage_read(const char *hash) {
  if (!hash || hash[0] == '\0') {
    log_error("storage_read: NULL or empty hash");
    return NULL;
  }

  char path[PATH_BUF];
  snprintf(path, sizeof(path), "%s%s", BLOCKS_DIR, hash);

  FILE *file = fopen(path, "rb");
  if (!file) {
    log_error("storage_read: block not found: %s", hash);
    return NULL;
  }

  Block *block = malloc(sizeof(Block));
  if (!block) {
    log_error("storage_read: malloc failed");
    fclose(file);
    return NULL;
  }

  size_t n = fread(block, sizeof(Block), 1, file);
  fclose(file);

  if (n != 1) {
    log_error("storage_read: short read for block %s (corrupt?)", hash);
    free(block);
    return NULL;
  }

  block->next = NULL; /* runtime-only: never trust what was on disk */
  return block;
}

int storage_read_into(const char *hash, Block *out) {
  if (!hash || hash[0] == '\0' || !out) {
    log_error("storage_read_into: invalid arguments");
    return EXIT_FAILURE;
  }

  char path[PATH_BUF];
  snprintf(path, sizeof(path), "%s%s", BLOCKS_DIR, hash);

  FILE *file = fopen(path, "rb");
  if (!file) {
    log_error("storage_read_into: block not found: %s", hash);
    return EXIT_FAILURE;
  }

  size_t n = fread(out, sizeof(Block), 1, file);
  fclose(file);

  if (n != 1) {
    log_error("storage_read_into: short read for block %s", hash);
    return EXIT_FAILURE;
  }

  out->next = NULL; /* runtime-only: never trust what was on disk */
  return EXIT_SUCCESS;
}

int storage_exists(const char *hash) {
  if (!hash || hash[0] == '\0') return 0;
  char path[PATH_BUF];
  snprintf(path, sizeof(path), "%s%s", BLOCKS_DIR, hash);
  return (access(path, F_OK) == 0) ? 1 : 0;
}

int storage_head(char *buf, size_t size) {
  if (!buf || size < HASH_SIZE) {
    log_error("storage_head: invalid buffer");
    return EXIT_FAILURE;
  }

  char content[PATH_BUF];
  memset(content, 0, sizeof(content));

  if (ref_read(HEAD_FILE, content, sizeof(content)) != EXIT_SUCCESS) {
    log_error("storage_head: failed to read HEAD");
    return EXIT_FAILURE;
  }

  /* Symbolic ref: "ref: refs/heads/main" */
  if (strncmp(content, REF_PREFIX, REF_PREFIX_LEN) == 0) {
    char ref_path[PATH_BUF];
    snprintf(ref_path, sizeof(ref_path), ".chain/%s", content + REF_PREFIX_LEN);
    if (ref_read(ref_path, buf, size) != EXIT_SUCCESS) {
      log_debug("storage_head: ref target not found (chain may be empty)");
      return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
  }

  /* Detached HEAD: raw hash stored directly */
  if (content[0] == '\0') return EXIT_FAILURE;
  snprintf(buf, size, "%s", content);
  return EXIT_SUCCESS;
}

int storage_checkout(const char *hash) {
  if (!hash || hash[0] == '\0') {
    log_error("storage_checkout: NULL or empty hash");
    return EXIT_FAILURE;
  }

  if (!storage_exists(hash)) {
    log_error("storage_checkout: block %s does not exist", hash);
    return EXIT_FAILURE;
  }

  if (init() != EXIT_SUCCESS) return EXIT_FAILURE;

  char content[PATH_BUF];
  memset(content, 0, sizeof(content));

  if (ref_read(HEAD_FILE, content, sizeof(content)) != EXIT_SUCCESS) {
    log_error("storage_checkout: failed to read HEAD");
    return EXIT_FAILURE;
  }

  /* Symbolic ref: update the branch file, not HEAD itself */
  if (strncmp(content, REF_PREFIX, REF_PREFIX_LEN) == 0) {
    char ref_path[PATH_BUF];
    snprintf(ref_path, sizeof(ref_path), ".chain/%s", content + REF_PREFIX_LEN);

    char current[HASH_SIZE];
    if (ref_read(ref_path, current, sizeof(current)) == EXIT_SUCCESS &&
        strcmp(current, hash) == 0) {
      return EXIT_SUCCESS; /* already at this hash, no-op */
    }

    return ref_write(ref_path, hash);
  }

  /* Detached HEAD: update HEAD directly */
  if (strcmp(content, hash) == 0) return EXIT_SUCCESS;
  return ref_write(HEAD_FILE, hash);
}

char **storage_scan(unsigned int offset, unsigned int *count) {
  if (!count || *count == 0) return NULL;

  unsigned int max = *count;
  *count = 0;

  char head[HASH_SIZE];
  memset(head, 0, sizeof(head));
  if (storage_head(head, sizeof(head)) != EXIT_SUCCESS) {
    log_debug("storage_scan: chain is empty");
    return NULL;
  }

  char current[HASH_SIZE];
  memcpy(current, head, sizeof(current));

  /* Skip 'offset' blocks walking backwards toward genesis */
  for (unsigned int i = 0; i < offset; i++) {
    Block *b = storage_read(current);
    if (!b) return NULL;
    int at_genesis = (b->previous_hash[0] == GENESIS_PREVIOUS_HASH[0] &&
                      b->previous_hash[1] == '\0');
    memcpy(current, b->previous_hash, HASH_SIZE);
    free(b);
    if (at_genesis) return NULL; /* offset past genesis */
  }

  char **scans = malloc(max * sizeof(char *));
  if (!scans) return NULL;

  unsigned int actual = 0;
  while (actual < max) {
    Block *b = storage_read(current);
    if (!b) break;

    scans[actual] = malloc(HASH_SIZE);
    if (!scans[actual]) {
      free(b);
      break;
    }
    memcpy(scans[actual], current, HASH_SIZE);
    actual++;

    int at_genesis = (b->previous_hash[0] == GENESIS_PREVIOUS_HASH[0] &&
                      b->previous_hash[1] == '\0');
    memcpy(current, b->previous_hash, HASH_SIZE);
    free(b);
    if (at_genesis) break;
  }

  if (actual == 0) {
    free(scans);
    return NULL;
  }

  *count = actual;
  return scans;
}

char **storage_list_all(unsigned int *count) {
  if (!count) return NULL;
  *count = 0;

  DIR *d = opendir(BLOCKS_DIR);
  if (!d) {
    log_debug("storage_list_all: %s not found", BLOCKS_DIR);
    return NULL;
  }

  /* First pass: count valid entries */
  unsigned int n = 0;
  struct dirent *e;
  while ((e = readdir(d)) != NULL) {
    if (e->d_name[0] == '.') continue;
    n++;
  }
  rewinddir(d);

  if (n == 0) { closedir(d); return NULL; }

  char **result = calloc(n, sizeof(char *));
  if (!result) { closedir(d); return NULL; }

  unsigned int i = 0;
  while ((e = readdir(d)) != NULL && i < n) {
    if (e->d_name[0] == '.') continue;
    result[i] = malloc(HASH_SIZE);
    if (!result[i]) {
      for (unsigned int j = 0; j < i; j++) free(result[j]);
      free(result);
      closedir(d);
      return NULL;
    }
    strncpy(result[i], e->d_name, HASH_SIZE - 1);
    result[i][HASH_SIZE - 1] = '\0';
    i++;
  }
  closedir(d);
  *count = i;
  return result;
}
