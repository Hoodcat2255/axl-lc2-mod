/* The 1UP item's own palette (user request 2026-11-08: Axl's grey 1UP came out blue).
 *
 * Items are drawn from the player's parts section 3 (the 1UP: frames 89-98, Axl's pictures since lc2_build.put_oneup)
 * with the stage's common item CLUT slot 11 (stage COL *_0x, the same 16 colours in every stage: X's blue helmet, the
 * energy capsules ...). Axl's 1UP picture is quantized to its own palette (lc2_build.oneup_own_pal, axl_data.bin
 * oneup_pal) instead: after the unit pass (axl_menu.c move_hook, before the draw) a live object drawing one of those
 * frames with slot 11 gets the slot of that palette, uploaded to a free CLUT slot (axl_dna.c clut_free_run) while a
 * 1UP is out; the slot's colours are put back when none is. An object that leaves the 1UP frames gets slot 11 back.
 *
 * Items are the objects drawing parts section 3 of texture index 0 (the player's: com00); enemies have their own
 * section 3 (probe 2026-11-08: a dropped item pool 3, header 21 2f KIND, tex 0; the intro's enemies tex 3). Kinds
 * (W+0xA): 0x20 / 0x21 / 0x23 / 0x24 the capsules (palette slot 7), 0x26 the 1UP (slot 11, anim 8 = frames 96-98,
 * 89-95: 'itemkind 26', the user's play 2026-11-08 - the palette slot 510 taken and given back).
 *
 * test: every item object is logged once per (object, item) - its pool, header, palette, frame and
 * anim; 'item1up' turns every item to the 1UP's anim (found in its anim table: the anim starting on frame 96).
 */
#include <windows.h>
#include <string.h>

#include "axl_lc2.h"
#include "axl_data.h"
#include "axl_test.h"

#define O8(p, o) (*(volatile unsigned char *)((p) + (o)))
#define O16(p, o) (*(volatile unsigned short *)((p) + (o)))
#define O32(p, o) (*(volatile int *)((p) + (o)))

typedef void(__fastcall *clut_upload_t)(void *ecx, void *edx, int slot, const void *src, int n);
#define CLUT_UPLOAD ((clut_upload_t)0x023AAEF0)   /* (axl_dna.c) */
#define CLUT_OWNER (*(void **)0x041D8C44)
#define CLUT_SLOT_MEM(s) ((unsigned short *)((char *)CLUT_OWNER + 0x1002C) + 16 * (s))

#define ITEM_PAT 3          /* the player's parts section of the items */
#define ITEM_CLUT 11        /* the stage's item palette slot */
#define ONEUP_F0 89         /* the 1UP's frames 89-98 (lc2_build.ONEUP_FRAMES) */
#define ONEUP_F1 98
#define N_POOLS 16
#define PER_POOL 96

char *objs_get(int pool, int i);   /* axl_objs.c */

static int slot = -1;               /* our 1UP slot while held */
static unsigned short slot_orig[16], slot_now[16];   /* its colours before / as our upload left them */
static int idle_n;                  /* frames with no 1UP out */

typedef void(__fastcall *set_anim_t)(char *ecx, void *edx, char *obj, int anim);
#define SET_ANIM ((set_anim_t)0x023C1730)   /* (axl_dna.c) */

int items_force_1up;   /* test 'item1up' */
int items_kinds[8], items_n_kinds;   /* test 'itemkind' */
#define ITEM_TYPE 0x2F       /* header W+9 of a dropped item (probe 2026-11-08) */

static int is_item(char *o)
{
    return O32(o, 0x54) == 0 && O32(o, 0x58) == ITEM_PAT;
}

static int is_oneup(char *o)
{
    return is_item(o) && O8(o, 0x66) >= ONEUP_F0 && O8(o, 0x66) <= ONEUP_F1;
}

static int readable(const void *p, unsigned n)
{
    MEMORY_BASIC_INFORMATION mi;
    return p && VirtualQuery(p, &mi, sizeof mi) && mi.State == MEM_COMMIT && !(mi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) &&
           (const char *)p + n <= (const char *)mi.BaseAddress + mi.RegionSize;
}

/* test: the anim of o's table whose first entry shows frame 96 (entries: vtable, dur, flag, step, frame), -1 none;
 * the table's anims' first frames logged once */
static int oneup_anim(char *o)
{
    static int logged;
    char **tbl = *(char ***)(o + 0x4C);
    int a, found = -1;
    char line[400];
    int len = 0;

    if (!readable(tbl, 4))
        return -1;
    for (a = 0; a < 64 && readable(tbl + a, 4); a++) {
        char *e = tbl[a];
        if (!readable(e, 8))
            break;
        if (test_on && len < 360)   /* (the log line only) */
            len += wsprintfA(line + len, " %d:%d", a, (unsigned char)e[7]);
        if ((unsigned char)e[7] == 96 && found < 0)
            found = a;
    }
    if (!logged++)
        test_log("item anim table %p first frames%s -> 1up anim %d", (void *)tbl, line, found);
    return found;
}

static int take(void)
{
    if (slot >= 0 && memcmp(CLUT_SLOT_MEM(slot), slot_now, sizeof slot_now))
        slot = -1;   /* (the game loaded other colours there: a stage load) - not ours to put back */
    if (slot < 0) {
        slot = clut_free_run(1, menu_pal_lo, menu_pal_lo + MENU_SLOTS);
        if (slot < 0)
            return -1;
        memcpy(slot_orig, CLUT_SLOT_MEM(slot), sizeof slot_orig);
        CLUT_UPLOAD(CLUT_OWNER, 0, slot, oneup_pal, 1);
        memcpy(slot_now, CLUT_SLOT_MEM(slot), sizeof slot_now);
        if (test_on)
            test_log("1up palette at slot %d", slot);
    }
    return slot;
}

static void give(void)
{
    if (slot < 0)
        return;
    if (!memcmp(CLUT_SLOT_MEM(slot), slot_now, sizeof slot_now))
        CLUT_UPLOAD(CLUT_OWNER, 0, slot, slot_orig, 1);
    if (test_on)
        test_log("1up palette slot %d given back", slot);
    slot = -1;
}

static void test_items(int k, char *o)
{
    static char *seen[64];
    static unsigned char seen_f[64];
    static int n_seen, n_logged;
    int i, f = O8(o, 0x66);

    for (i = 0; i < n_seen; i++)
        if (seen[i] == o && (seen_f[i] >= ONEUP_F0 && seen_f[i] <= ONEUP_F1) == (f >= ONEUP_F0 && f <= ONEUP_F1))
            return;
    if (n_logged >= 300)
        return;
    n_logged++;
    seen[n_seen % 64] = o;
    seen_f[n_seen % 64] = (unsigned char)f;
    n_seen++;
    test_log("item pool %d %p hdr %02x %02x %02x %02x %02x %02x %02x %02x tex %d pal %d frame %d anim %d xy %.0f %.0f",
             k, o, O8(o, 8), O8(o, 9), O8(o, 10), O8(o, 11), O8(o, 12), O8(o, 13), O8(o, 14), O8(o, 15), O32(o, 0x54),
             O16(o, 0x5E), f, O8(o, 0x2B), *(volatile float *)(o + 0x14), *(volatile float *)(o + 0x1C));
}

static int forced(char *o)   /* test item1up: o turned already (else remembered as turned now) */
{
    static char *done[32];
    static unsigned char kind[32];
    static int n;
    int i;

    for (i = 0; i < 32; i++)
        if (done[i] == o && kind[i] == O8(o, 10))
            return 1;
    done[n % 32] = o;
    kind[n % 32] = O8(o, 10);
    n++;
    return 0;
}

/* test: Nightmare Zero's state, sub-state, anim, damage table and hit points logged at each change (his flinch,
 * 'nzids'). Type 0x23 (Yammark stage, 2026-10-27) and 0x35 (Mijinion stage, user 2026-11-08), not pool 6 (effects)
 * - every live one, tracked by object */
#define N_NZ 4
static void test_nzero(void)
{
    static char *zo[N_NZ];
    static unsigned last[N_NZ][2];
    int k, i, j;
    char *o;

    for (k = 0; k < N_POOLS; k++)
        for (i = 0; i < PER_POOL && (o = objs_get(k, i)) != 0; i++) {
            unsigned a, b;
            if (!O8(o, 8) || (O8(o, 9) != 0x23 && O8(o, 9) != 0x35) || k == 6)
                continue;
            for (j = 0; j < N_NZ && zo[j] != o; j++)
                ;
            if (j == N_NZ) {
                for (j = 0; j < N_NZ && zo[j] && O8(zo[j], 8); j++)
                    ;
                if (j == N_NZ)
                    continue;
                zo[j] = o;
                last[j][0] = last[j][1] = 0xFFFFFFFFu;
            }
            a = (unsigned)O8(o, 0x0D) << 24 | (unsigned)O8(o, 0x0E) << 16 | (unsigned)O8(o, 0x2B) << 8 | O8(o, 0x66);
            b = (unsigned)O8(o, 0x84) << 24 | ((unsigned)O32(o, 0x80) & 0xFFFFFF);   /* hit points, table */
            if (last[j][1] != 0xFFFFFFFFu && O8(o, 0x84) < (unsigned char)(last[j][1] >> 24)) {
                /* hurt: every live object within 80 px (the weapon that hit: pool, id W+9, its header) */
                int k2, i2;
                char *w;
                float zx = *(volatile float *)(o + 0x14), zy = *(volatile float *)(o + 0x1C);
                test_log("nz hurt %d -> %d (state %d sub %d anim %d)", (int)(last[j][1] >> 24), O8(o, 0x84),
                         O8(o, 0x0D), O8(o, 0x0E), O8(o, 0x2B));
                for (k2 = 0; k2 < N_POOLS; k2++)
                    for (i2 = 0; i2 < PER_POOL && (w = objs_get(k2, i2)) != 0; i2++) {
                        float dx = *(volatile float *)(w + 0x14) - zx, dy = *(volatile float *)(w + 0x1C) - zy;
                        if (!O8(w, 8) || w == o || dx > 80 || dx < -80 || dy > 80 || dy < -80)
                            continue;
                        test_log("  near pool %d %p hdr %02x %02x %02x %02x %02x %02x tex %d pat %d anim %d c4 %08x d4 %d xy %.0f %.0f",
                                 k2, w, O8(w, 8), O8(w, 9), O8(w, 10), O8(w, 11), O8(w, 12), O8(w, 13), O32(w, 0x54),
                                 O32(w, 0x58), O8(w, 0x2B), O32(w, 0xC4), (signed char)O8(w, 0xD4),
                                 *(volatile float *)(w + 0x14), *(volatile float *)(w + 0x1C));
                    }
            }
            if (a != last[j][0] || (b & 0xFFFFFF) != (last[j][1] & 0xFFFFFF))
                test_log("nz %p pool %d type %02x sub %02x state %d sub %d anim %d frame %d table %08x hp %d xy %.0f %.0f",
                         o, k, O8(o, 9), O8(o, 10), O8(o, 0x0D), O8(o, 0x0E), O8(o, 0x2B), O8(o, 0x66), O32(o, 0x80),
                         O8(o, 0x84), *(volatile float *)(o + 0x14), *(volatile float *)(o + 0x1C));
            last[j][0] = a;
            last[j][1] = b;
        }
}

/* after the unit pass, before the draw */
void items_late(void)
{
    int k, i, n = 0;
    char *o;

    if (test_on)
        test_nzero();
    if (!CLUT_OWNER)
        return;
    for (k = 0; k < N_POOLS; k++)
        for (i = 0; i < PER_POOL && (o = objs_get(k, i)) != 0; i++) {
            if (!O8(o, 8) || k <= 1)   /* (dead; the player and his afterimages) */
                continue;
            if (test_on && is_item(o))
                test_items(k, o);
            if (items_n_kinds && O8(o, 9) == ITEM_TYPE && is_item(o) && !forced(o)) {
                static int next;
                int kind = items_kinds[next < items_n_kinds ? next : items_n_kinds - 1];
                test_log("item %p kind %02x -> %02x, set-up again", o, O8(o, 10), kind);
                next++;
                O8(o, 10) = (unsigned char)kind;
                O8(o, 0x0C) = 0;
                forced(o);   /* (remembered with its new kind) */
            }
            if (items_force_1up && is_item(o) && !forced(o)) {
                int a = oneup_anim(o);
                if (a >= 0) {
                    SET_ANIM(o, 0, o, a);
                    O16(o, 0x5E) = ITEM_CLUT;   /* (as a 1UP's: a capsule has its own slot, 7) */
                    test_log("item %p -> anim %d (1up) frame %d pal %d", o, a, O8(o, 0x66), O16(o, 0x5E));
                }
            }
            if (is_oneup(o) && (O16(o, 0x5E) == ITEM_CLUT || (slot >= 0 && O16(o, 0x5E) == slot))) {
                int s = take();
                if (s >= 0) {
                    O16(o, 0x5E) = (unsigned short)s;
                    n++;
                }
            } else if (slot >= 0 && O16(o, 0x5E) == slot && !is_oneup(o))
                O16(o, 0x5E) = ITEM_CLUT;
        }
    idle_n = n ? 0 : idle_n + 1;
    if (slot >= 0 && idle_n > 60)
        give();
}
