/* equivocation.h — Slot-based equivocation guard for PoS proposers.
 *
 * Equivocation occurs when a proposer commits two different blocks for the
 * same slot. This module prevents it by tracking the last committed slot per
 * proposer in .chain/slots/<proposer_id> (binary uint32_t).
 *
 * Integration:
 *   equivocation_check()  — called by verify_pos_rules() before a block
 *                           is accepted (read-only; no side effects).
 *   equivocation_record() — called by chain_add() after a PoS block is
 *                           successfully committed (writes the slot record).
 */
#ifndef EQUIVOCATION_H
#define EQUIVOCATION_H

#include <stdint.h>

/* Directory that holds per-proposer slot records. */
#define SLOTS_DIR ".chain/slots"

/*
 * Check whether proposer_id has already committed a block for slot.
 *
 * Returns EXIT_FAILURE if a duplicate is detected (equivocation).
 * Returns EXIT_SUCCESS if the slot is new or no record exists yet.
 * Returns EXIT_FAILURE if proposer_id is NULL or empty.
 */
int equivocation_check(const char *proposer_id, uint32_t slot);

/*
 * Record that proposer_id has committed a block for slot.
 * Overwrites any previous record for this proposer.
 *
 * Returns EXIT_SUCCESS on success, EXIT_FAILURE on I/O error.
 * Returns EXIT_FAILURE if proposer_id is NULL or empty.
 */
int equivocation_record(const char *proposer_id, uint32_t slot);

#endif /* EQUIVOCATION_H */
