/* equivocation.c — Slot-based equivocation guard for PoS proposers. */

#include "equivocation.h"
#include "log.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/stat.h>
#include <unistd.h>

#define PATH_BUF 512

static int ensure_slots_dir(void)
{
    if (mkdir(SLOTS_DIR, 0755) == -1 && errno != EEXIST) {
        log_error("equivocation: cannot create %s: %s",
                  SLOTS_DIR, strerror(errno));
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

/* Build .chain/slots/<proposer_id> into buf. */
static void slot_path(const char *id, char *buf, size_t bufsz)
{
    snprintf(buf, bufsz, "%s/%s", SLOTS_DIR, id);
}

int equivocation_check(const char *proposer_id, uint32_t slot)
{
    if (!proposer_id || proposer_id[0] == '\0') {
        log_error("equivocation_check: empty proposer_id");
        return EXIT_FAILURE;
    }

    char path[PATH_BUF];
    slot_path(proposer_id, path, sizeof(path));

    FILE *f = fopen(path, "rb");
    if (!f) return EXIT_SUCCESS; /* no record yet — first proposal */

    uint32_t committed = 0;
    size_t   n         = fread(&committed, sizeof(committed), 1, f);
    fclose(f);

    if (n != 1) return EXIT_SUCCESS; /* unreadable record — treat as clean */

    if (committed == slot) {
        log_warn("equivocation_check: proposer '%s' already committed slot %u",
                 proposer_id, slot);
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

int equivocation_record(const char *proposer_id, uint32_t slot)
{
    if (!proposer_id || proposer_id[0] == '\0') {
        log_error("equivocation_record: empty proposer_id");
        return EXIT_FAILURE;
    }

    if (ensure_slots_dir() != EXIT_SUCCESS) return EXIT_FAILURE;

    char path[PATH_BUF];
    slot_path(proposer_id, path, sizeof(path));

    FILE *f = fopen(path, "wb");
    if (!f) {
        log_error("equivocation_record: cannot write %s: %s",
                  path, strerror(errno));
        return EXIT_FAILURE;
    }

    size_t written = fwrite(&slot, sizeof(slot), 1, f);
    fclose(f);

    if (written != 1) {
        log_error("equivocation_record: short write for '%s'", proposer_id);
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
