/* t113_unverified.h - the two ways this port is allowed to not know a number.
 *
 * docs/HISTORY.md 8 and the workstream brief both say the same thing: never
 * invent a number. There are two distinct failure modes, and they want two
 * different mechanisms.
 *
 * 1. MTP_T113_UNVERIFIED(tag, value)
 *    We have a value and a reason to believe it, but no primary source that
 *    states it for this part. The value is used, AND it is registered in a
 *    table that main() prints at boot, so the first thing anyone sees on a
 *    real board is the list of assumptions the firmware is running on. An
 *    inferred constant that nobody can see is how a wrong number survives.
 *
 * 2. MTP_T113_UNKNOWN(tag)
 *    We have no value at all. The macro expands to a reference to a symbol
 *    that is declared and never defined, so the *link* fails with
 *
 *        undefined reference to `mtp_t113_unknown__i2s1_mclk_rate_hz'
 *
 *    which names the missing fact. A build that fails loudly on an unknown is
 *    worth more than one that silently contains a made-up address. Nothing in
 *    the default configuration uses this; it guards the optional paths.
 *
 * SPDX-License-Identifier: 0BSD
 */
#ifndef T113_UNVERIFIED_H
#define T113_UNVERIFIED_H

#include <stdint.h>

typedef struct {
    const char *tag;
    const char *why;
    uint32_t    value;
} mtp_t113_assumption;

/* Registration is by linker section, so declaring an assumption costs one
 * line at the point of use and nothing has to be kept in sync. */
#define MTP_T113_UNVERIFIED(tag, val, why_str)                               \
    (__extension__ ({                                                        \
        static const mtp_t113_assumption mtp_t113_assume_##tag               \
            __attribute__((used, section(".mtp_assumptions"), aligned(4))) =  \
            { #tag, why_str, (uint32_t)(val) };                              \
        (void)mtp_t113_assume_##tag;                                         \
        (val);                                                               \
    }))

/* The same thing where a statement expression is not allowed (initialisers of
 * objects with static storage duration). Registers nothing; the caller must
 * add an MTP_T113_NOTE() somewhere that does run. */
#define MTP_T113_UNVERIFIED_CONST(val) (val)

#define MTP_T113_NOTE(tag, val, why_str)                                     \
    static const mtp_t113_assumption mtp_t113_assume_##tag                   \
        __attribute__((used, section(".mtp_assumptions"), aligned(4))) =      \
        { #tag, why_str, (uint32_t)(val) }

#define MTP_T113_UNKNOWN(tag)                                                \
    (__extension__ ({                                                        \
        extern const uint32_t mtp_t113_unknown__##tag;                       \
        mtp_t113_unknown__##tag;                                             \
    }))

/* main() walks this and prints every entry. */
extern const mtp_t113_assumption __mtp_assumptions_start[];
extern const mtp_t113_assumption __mtp_assumptions_end[];

void t113_print_assumptions(void);

#endif /* T113_UNVERIFIED_H */
