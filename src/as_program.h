#ifndef AS_PROGRAM_H
#define AS_PROGRAM_H

#include <stddef.h>
#include <stdint.h>

typedef struct AsLine {
    struct AsLine *next;
    uint16_t number;
    int breakpoint;
    uint8_t *tokens;
    size_t len;
} AsLine;

typedef struct AsProgram {
    AsLine *head;
    size_t count;
    int dirty;
    uint64_t targets[1000]; /* bitset of line numbers that are jump targets */
} AsProgram;

AsProgram *as_program_new(void);
void as_program_clear(AsProgram *p);
void as_program_free(AsProgram *p);

size_t as_program_count(const AsProgram *p);
AsLine *as_program_at(AsProgram *p, size_t index);
AsLine *as_program_find(AsProgram *p, uint16_t number);

/* Insert or replace. len 0 deletes that line number. Returns 0 on success. */
int as_program_upsert(AsProgram *p, uint16_t number, const uint8_t *tokens, size_t len, int breakpoint);
void as_program_set_break(AsProgram *p, size_t index, int on);
void as_program_delete_range(AsProgram *p, uint16_t first, uint16_t last);

/* Renumber from `start` by `step`. Rewrites GOTO/GOSUB/THEN/RUN/RESTORE/ON targets.
   Returns 0, or AS_ERR_ILLEGAL_QUANTITY if a new number would exceed 63999. */
int as_program_renumber(AsProgram *p, uint16_t start, uint16_t step);

void as_program_rebuild_targets(AsProgram *p);
int as_program_is_target(const AsProgram *p, uint16_t number);

int as_program_dirty(const AsProgram *p);
void as_program_mark_clean(AsProgram *p);

/* DOS 3.3 AppleSoft image based at $0801. *out is malloc'd. */
int as_program_save(const AsProgram *p, uint8_t **out, size_t *out_len, char *err, size_t errcap);
int as_program_load(AsProgram *p, const uint8_t *data, size_t len, char *err, size_t errcap);

#endif
