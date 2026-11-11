/* The game's objects for Axl's targeting (PS1 find_target walked the enemy and boss pools; Sheldon's plates walked
 * the enemy bullet pool). On PC the pools hand out objects through FUN_023b16b0(pool, a, b) (axl_hook.c wraps it):
 * every object made is remembered per pool here, and a remembered object counts while its header says it lives
 * (W+8 != 0, the PS1 free clears bytes 0-7) and it has a hurt box (W+0x7C, PS1 +0x54).
 */
#include <windows.h>
#include <string.h>

#include "axl_lc2.h"
#include "axl_test.h"

#define N_POOLS 16
#define PER_POOL 96
#define O8(p, o) (*(volatile unsigned char *)((p) + (o)))
#define PX(p) (*(volatile float *)((p) + 0x14))
#define O32(p, o) (*(volatile unsigned *)((p) + (o)))
#define PY(p) (*(volatile float *)((p) + 0x1C))

static char *objs[N_POOLS][PER_POOL];
static int n_objs[N_POOLS];
static int logged[N_POOLS];
static int n_late;

/* the pools of enemies / bosses and of enemy bullets (re/lc2/codemap.md section 2) */
#define TARGET_POOLS objs_target_pools
#define EBULLET_POOLS objs_ebullet_pools
unsigned objs_target_pools = 1u << 3 | 1u << 5;   /* enemies (intro stage, 2026-10-26: walkers in pool 5, 3) */
/* the guard-break bullet's targets: the enemies' pools and pool 10 (user report 2026-11-07, Yammark stage: the pill
 * bug, type 01, rolled up on the all-immune table 031dea38, open on 031dead8 - pool 10, which held the Shark stage's
 * press too: only objects with a damage table, hit points and a hurt box count) */
#define GUARD_POOLS (TARGET_POOLS | 1u << 10)
unsigned objs_ebullet_pools = 1u << 11;           /* enemy bullets: the pool whose types hold every boss shot type (PS1 *0x1F800084 table) */

/* X's shot code makes a muzzle flash (pool 6) that plays X's shot sound: for our projectiles both are made while
 * objs_mute is set, and the sound requests of those effects are dropped (axl_hook.c se_hook) */
#define N_MUTED 16
static char *muted[N_MUTED];
static int n_muted_at;
int objs_mute;

int objs_muted(char *o)
{
    int i;
    for (i = 0; i < N_MUTED; i++)
        if (o && muted[i] == o)
            return 1;
    return 0;
}

void objs_alloc(char *o, int pool, int a, int b)
{
    int i;

    if (objs_mute && pool == 6)
        muted[n_muted_at++ % N_MUTED] = o;
    else if (pool == 6)
        for (i = 0; i < N_MUTED; i++)
            if (muted[i] == o)
                muted[i] = 0;   /* (the slot is X's again) */

    if (pool < 0 || pool >= N_POOLS)
        return;
    for (i = 0; i < n_objs[pool]; i++)
        if (objs[pool][i] == o)
            return;
    if (n_objs[pool] < PER_POOL)
        objs[pool][n_objs[pool]++] = o;
    if (test_on && test_frame >= 8000 && n_late < 300) {   /* (in play: every object made) */
        n_late++;
        test_log("made pool %d a %d b %d -> %p hdr %02x %02x %02x xy %.0f %.0f", pool, a, b, o, O8(o, 8), O8(o, 9),
                 O8(o, 10), PX(o), PY(o));
    }
    if (test_on && logged[pool] < 6) {
        logged[pool]++;
        test_log("pool %d a %d b %d -> %p (%d known)", pool, a, b, o, n_objs[pool]);
    }
}

/* a live object with this id (header byte 1, W+9) in any pool (a boss: its type) */
char *objs_find_id(int id)
{
    int k, i;
    for (k = 0; k < N_POOLS; k++)
        for (i = 0; i < n_objs[k]; i++)
            if (O8(objs[k][i], 8) && O8(objs[k][i], 9) == id)
                return objs[k][i];
    return 0;
}

/* a live boss (test harness boss_hp / bot): type 0x30 (the intro's D-1000, 2026-10-28 dump) or a Maverick
 * (axl_dna.c boss_type; not 0x37 = High Max of the intro's ending event), its body (W+10 = 0; the D-1000's parts
 * share its type), alive in any pool */
#define MAVERICKS "\x31\x32\x33\x34\x36\x38\x39\x3A\x3D\x3E\x3F"   /* (+ Sigma 0x3D-0x3F: the 2026-10-28 dump showed 0x3D) */
char *objs_boss(void)
{
    int k, i;
    char *o;
    for (k = 0; k < N_POOLS; k++)
        for (i = 0; i < n_objs[k]; i++) {
            o = objs[k][i];
            if (O8(o, 8) && !O8(o, 10) && (O8(o, 9) == 0x30 || (O8(o, 9) && strchr(MAVERICKS, O8(o, 9)))))
                return o;
        }
    return 0;
}

/* test (objs): the live objects' types 0x30 and up, each once, with its sub-type and hit points (one line) */
void objs_types(void)
{
    char line[512] = "e2e objs";
    int k, i, n = 8;
    char *o;
    for (k = 0; k < N_POOLS && n < 480; k++)
        for (i = 0; i < n_objs[k] && n < 480; i++) {
            o = objs[k][i];
            if (O8(o, 8) && O8(o, 9) >= 0x30)
                n += wsprintfA(line + n, " %d:%02x.%02x/%d@%d,%d", k, O8(o, 9), O8(o, 10), O8(o, 0x84), (int)PX(o),
                               (int)PY(o));   /* (wsprintf has no %f) */
        }
    test_log("%s", line);
}

/* test: the live objects of every pool with their headers, hurt box and position */
void objs_dump(void)
{
    int k, i;
    char *o;

    for (k = 0; k < N_POOLS; k++)
        for (i = 0; i < n_objs[k]; i++) {
            o = objs[k][i];
            if (!O8(o, 8))
                continue;
            test_log("obj pool %d %p hdr %02x %02x %02x %02x %02x %02x %02x %02x xy %.0f %.0f box %p tex %d pat %d pal %d "
                     "frame %d atk %p v %x %x", k, o, O8(o, 8), O8(o, 9), O8(o, 10), O8(o, 11), O8(o, 12), O8(o, 13), O8(o, 14),
                     O8(o, 15), PX(o), PY(o), *(void **)(o + 0x7C), O32(o, 0x54), O32(o, 0x58),
                     *(volatile unsigned short *)(o + 0x5E), O8(o, 0x66), *(void **)(o + 0x78), O32(o, 0x3C), O32(o, 0x40));
            if (k <= 1) {   /* the player and his afterimages: their bytes */
                char hex[0xC0 * 2 + 1];
                int b;
                for (b = 0; b < 0xC0; b++)
                    wsprintfA(hex + 2 * b, "%02x", (unsigned char)o[b]);
                test_log("  bytes %s", hex);
            }
        }
}

/* test (hpwatch): log every change of an enemy's damage table (+0x80, PS1 +0x58) or hit points (+0x84, PS1 +0x5C)
 * with the ids of the player's weapons (pool 4) within 40 px */
void objs_hp_watch(void)
{
    static unsigned last[N_POOLS][PER_POOL][2];
    static float wx[PER_POOL];
    static unsigned char prev[PER_POOL][0xE0];   /* (weapon objects are 0xE0 apart) */
    static unsigned char wid[PER_POOL];
    char *o, *w, ids[64];
    int k, i, j, n;
    unsigned t, hp;

    for (j = 0; j < n_objs[4]; j++) {   /* where each of the player's weapons ended */
        w = objs[4][j];
        if (O8(w, 8)) {
            wx[j] = PX(w);
            wid[j] = O8(w, 9);
        } else if (wid[j] != 0xFF) {
            if (wx[j] != 0)
                test_log("shot %p id %02x freed at x %.0f", w, wid[j], wx[j]);
            wid[j] = 0xFF;
        }
    }

    for (k = 0; k < N_POOLS; k++) {
        if (!(TARGET_POOLS >> k & 1))
            continue;
        for (i = 0; i < n_objs[k]; i++) {
            o = objs[k][i];
            if (!O8(o, 8))
                continue;
            t = *(volatile unsigned *)(o + 0x80);
            hp = *(volatile unsigned short *)(o + 0x84);
            if (t == last[k][i][0] && hp == last[k][i][1])
                continue;   /* (prev is refreshed at the end) */
            n = 0;
            ids[0] = 0;
            for (j = 0; j < n_objs[4] && n < 56; j++) {
                w = objs[4][j];
                if (O8(w, 8) && PX(w) - PX(o) < 40 && PX(o) - PX(w) < 40 && PY(w) - PY(o) < 40 && PY(o) - PY(w) < 40)
                    n += wsprintfA(ids + n, " %02x", O8(w, 9));
            }
            test_log("hp pool %d %p id %02x table %08x hp %u -> %u, shots near:%s", k, o, O8(o, 9), t,
                     last[k][i][1], hp, ids);
            for (j = 0; j < n_objs[4]; j++) {   /* what changed in those shots since the last tick */
                char d[400];
                int m = 0, b;
                w = objs[4][j];
                if (!O8(w, 8) || PX(w) - PX(o) > 40 || PX(o) - PX(w) > 40)
                    continue;
                for (b = 0; b < 0xE0 && m < 380; b++)
                    if ((unsigned char)w[b] != prev[j][b])
                        m += wsprintfA(d + m, " %x:%02x>%02x", b, prev[j][b], (unsigned char)w[b]);
                d[m] = 0;
                test_log("  shot %p diff%s", w, d);
            }
            last[k][i][0] = t;
            last[k][i][1] = hp;
        }
    }
    for (j = 0; j < n_objs[4]; j++) {
        w = objs[4][j];
        if (O8(w, 8) && memcmp(prev[j] + 0xC4, w + 0xC4, 0x14))
            test_log("  shot %p id %02x x %.0f c4..d7 %08x %08x %08x %08x %08x", w, O8(w, 9), PX(w), O32(w, 0xC4),
                     O32(w, 0xC8), O32(w, 0xCC), O32(w, 0xD0), O32(w, 0xD4));
        memcpy(prev[j], w, 0xE0);
    }
}

/* test (etable): every live enemy uses the damage table at va */
void objs_set_table(unsigned va)
{
    int k, i;
    for (k = 0; k < N_POOLS; k++)
        if (TARGET_POOLS >> k & 1)
            for (i = 0; i < n_objs[k]; i++)
                if (O8(objs[k][i], 8))
                    *(volatile unsigned *)(objs[k][i] + 0x80) = va;
}

/* Enemies just hurt (hit points down or freed) within the last 2 player frames: Axl's guard-break bullet uses it
 * to tell a hit from passing a saber (both set the shot's W+0xC4 only) */
static unsigned short hp_seen[N_POOLS][PER_POOL];
static unsigned hurt_at[N_POOLS][PER_POOL];
static float hurt_x[N_POOLS][PER_POOL], hurt_y[N_POOLS][PER_POOL];
static unsigned hp_frames;

/* Axl's hits before the invincibility (user request 2026-10-27: his single-damage bullets did little to bosses, every
 * hit made them immune for about 28 frames). A target hurt swaps its damage table (+0x80) for an all-immune one
 * (2 bytes per weapon id, byte 0 = 0xff: no damage; Nightmare Zero's 0x031DF398 -> 0x031DEA38 at each hit,
 * hpwatch 2026-10-27) until its own timer puts the table back: for the first COMBO_HITS - 1 hits the table it had
 * is put back at once. Targets already immune before the hit (High Max unstunned, a closed mouth) are left alone. */
#define COMBO_HITS 3   /* (5 for a while: too strong, back to 3, user 2026-10-27) */
static unsigned tbl_ok[N_POOLS][PER_POOL], tbl_iframe[N_POOLS][PER_POOL];   /* iframe: the table under a hit's immunity */
static unsigned char combo[N_POOLS][PER_POOL], hp_lo[N_POOLS][PER_POOL];
static unsigned tbl_prev[N_POOLS][PER_POOL];   /* the table last frame */

static int immune_table(unsigned t)
{
    return t && !IsBadReadPtr((void *)t, 2) && *(volatile unsigned char *)t == 0xff;
}

static void combo_frame(int k, int i, char *o)
{
    unsigned t = O8(o, 8) ? *(volatile unsigned *)(o + 0x80) : 0;
    unsigned char lo = O8(o, 8) ? O8(o, 0x84) : 0;

    if (!t) {
        tbl_ok[k][i] = 0, combo[k][i] = 0, tbl_iframe[k][i] = 0;
    } else if (!immune_table(t)) {
        tbl_iframe[k][i] = 0;
        if (t != tbl_ok[k][i] || !lo)
            combo[k][i] = 0;
        tbl_ok[k][i] = t;
    } else if (lo < hp_lo[k][i] && lo && tbl_ok[k][i] && axl_p && tbl_prev[k][i] != t) {
        /* hurt just now, the immune table on with the hit (not a guard's, on before it: the guard-break bullet's hit
         * through it, 2026-11-07 - the open table went back on a guarding enemy) */
        if (++combo[k][i] < COMBO_HITS) {
            *(volatile unsigned *)(o + 0x80) = tbl_ok[k][i];
            if (test_on)
                test_log("combo: %p hit %d (hp %d), table %08x back", o, combo[k][i], lo, tbl_ok[k][i]);
        } else {
            combo[k][i] = 0;
            tbl_iframe[k][i] = tbl_ok[k][i];
            tbl_ok[k][i] = 0;   /* immune until its own timer ends (or a guard-break bullet: objs_iframe_open) */
        }
    }
    hp_lo[k][i] = lo;
    tbl_prev[k][i] = t;
}

/* the guard-break bullet ignores the hit immunity (user request 2026-10-27): targets within r px of it that are immune
 * from a hit get their table back, the bullet's hit makes them immune again (its count set to the last of the
 * combo). Immune ones that were not hit (closed mouths) stay so. Returns how many. */
int objs_iframe_open(float x, float y, int r)
{
    int k, i, n = 0;
    char *o;

    for (k = 0; k < N_POOLS; k++) {
        if (!(TARGET_POOLS >> k & 1))
            continue;
        for (i = 0; i < n_objs[k]; i++) {
            o = objs[k][i];
            if (!tbl_iframe[k][i] || !O8(o, 8) || !immune_table(*(volatile unsigned *)(o + 0x80)) ||
                PX(o) - x >= r || x - PX(o) >= r || PY(o) - y >= r || y - PY(o) >= r)
                continue;
            *(volatile unsigned *)(o + 0x80) = tbl_iframe[k][i];
            tbl_ok[k][i] = tbl_iframe[k][i];
            tbl_iframe[k][i] = 0;
            combo[k][i] = COMBO_HITS - 1;
            n++;
            if (test_on)
                test_log("guard break: %p immunity off, table %08x", o, tbl_ok[k][i]);
        }
    }
    return n;
}

/* The guard-break bullet ignores guards too (user request 2026-11-07, "무적 자세까지"): a target whose damage table
 * blocks its id 0x18 (0xFF bounce / 7 / 8) has that table's 0x18 entry made a hit while the bullet is near: the damage
 * the least its table takes from any id if some id hurts it (a guard on one side), else the least of the last table
 * it had that took damage (a guard pose, a closed mouth). The entry goes back GB_HOLD frames after the bullet left.
 * The table itself, not the target's pointer: enemies in a guard write their guard table every frame before their
 * hit test (Inami Temple's purple guard, type 09: a copy put on it by the bullet's update was gone again - user report
 * 2026-11-07, hp 32 stayed). Only id 0x18 changes, and only the guard-break bullet carries it in Axl's game (Zero's
 * first saber hit too, not in it): the others sharing the table (031DEA38 is the hit immunity's too) are open to the
 * bullet alone. A table that lets 0x18 through (reaction 2: enemy sabers and shields, made so) is left alone, High
 * Max keeps his stun rule (axl_proj.c), bosses before their fight (no table taking damage yet) stay immune.
 * (Before: the bullet took the id the nearest target takes - near a shield it took the shield's and the body behind
 * bounced it; then a copy of the table on the target - see above.) */
#define TBL_IDS 64          /* ids 0-63 (2 bytes each; PS1 notes: past 64 is other data) */
#define GB_HOLD 2           /* frames without the bullet near before the entry goes back */
#define REACT_PASS 2        /* passes through, no hit */
#define N_PATCH 8
static unsigned tbl_hurt[N_POOLS][PER_POOL], tbl_seen[N_POOLS][PER_POOL];   /* the last table taking damage */
static unsigned char hm_seen[N_POOLS][PER_POOL];
static struct {
    unsigned t, at;          /* the table (0 = free), the last frame the bullet was near */
    unsigned char id, orig[2];
} patch[N_PATCH];

static int table_readable(unsigned t)
{
    return t && !IsBadReadPtr((void *)t, 2 * TBL_IDS);
}

static int id_hurts(unsigned t, int id)   /* reaction 0 (normal) or 9 (damage, shot gone) with damage > 0 */
{
    const unsigned char *e = (const unsigned char *)t + 2 * id;
    return (e[0] == 0 || e[0] == 9) && (signed char)e[1] > 0;
}

static int bad_id(int id)   /* not counted: hit stop ids (PS1 notes 3-3 step 4), 0x20 (+w[6] damage) */
{
    int d = id - 0x1A;
    return (d >= 0 && d <= 5) || d == 9 || d == 0x14 || d == 0x19 || d == 0x1F || d == 0x23 || d == 0x24 || id == 0x20;
}

/* the least damage table t takes from any id (the guard-break hit on it: not a weak spot's), 0 none */
static int soft_damage(unsigned t)
{
    int id, bd = 0;
    if (!table_readable(t))
        return 0;
    for (id = 0; id < TBL_IDS; id++)
        if (!bad_id(id) && id_hurts(t, id) && (!bd || (signed char)((const unsigned char *)t)[2 * id + 1] < bd))
            bd = (signed char)((const unsigned char *)t)[2 * id + 1];
    return bd;
}

static int is_patched(unsigned t)
{
    int j;
    for (j = 0; j < N_PATCH; j++)
        if (patch[j].t == t)
            return 1;
    return 0;
}

static void guard_frame(int k, int i, char *o)
{
    unsigned t = O8(o, 8) ? *(volatile unsigned *)(o + 0x80) : 0;

    if (!O8(o, 8)) {
        tbl_hurt[k][i] = tbl_seen[k][i] = 0;
        hm_seen[k][i] = 0;
        return;
    }
    if (t == HIGHMAX_TABLE)
        hm_seen[k][i] = 1;
    if (t != tbl_seen[k][i]) {
        tbl_seen[k][i] = t;
        if (soft_damage(t) && !is_patched(t))   /* (one we opened is not its own: 2026-11-07, the guard then
                                                 * counted as its open table and was never opened again) */
            tbl_hurt[k][i] = t;
    }
}

static int patch_write(unsigned t, int id, const unsigned char *e)
{
    DWORD old;
    unsigned char *a = (unsigned char *)t + 2 * id;
    if (!VirtualProtect(a, 2, PAGE_READWRITE, &old))
        return 0;
    a[0] = e[0];
    a[1] = e[1];
    VirtualProtect(a, 2, old, &old);
    return 1;
}

static void patches_frame(void)   /* entries the bullet left GB_HOLD frames ago go back */
{
    int j;
    for (j = 0; j < N_PATCH; j++)
        if (patch[j].t && hp_frames - patch[j].at > GB_HOLD) {
            patch_write(patch[j].t, patch[j].id, patch[j].orig);
            if (test_on)
                test_log("guard: table %08x id %02x back to %02x %02x", patch[j].t, patch[j].id, patch[j].orig[0],
                         patch[j].orig[1]);
            patch[j].t = 0;
        }
}

static int patched(unsigned t, int id)   /* its index, the bullet seen near now; -1 none */
{
    int j;
    for (j = 0; j < N_PATCH; j++)
        if (patch[j].t == t && patch[j].id == id) {
            patch[j].at = hp_frames;
            return j;
        }
    return -1;
}

/* the guard-break bullet at (x, y) with damage id `id`: the guards of the targets within r px of it open (above) */
int objs_guard_open(float x, float y, int r, int id)
{
    int k, i, j, n = 0;
    char *o;

    for (k = 0; k < N_POOLS; k++) {
        if (!(GUARD_POOLS >> k & 1))
            continue;
        for (i = 0; i < n_objs[k]; i++) {
            unsigned t;
            int dmg;
            unsigned char hit[2];
            o = objs[k][i];
            if (!O8(o, 8) || !*(void **)(o + 0x7C) || !*(volatile unsigned short *)(o + 0x84) || hm_seen[k][i] ||
                PX(o) - x >= r || x - PX(o) >= r || PY(o) - y >= r || y - PY(o) >= r)
                continue;
            t = *(volatile unsigned *)(o + 0x80);
            if (t == HIGHMAX_TABLE || !table_readable(t) || patched(t, id) >= 0 || id_hurts(t, id))
                continue;
            if ((dmg = soft_damage(t)) != 0) {            /* guarded against it only */
                if (((const unsigned char *)t)[2 * id] == REACT_PASS)
                    continue;                             /* (made to let it through) */
            } else if (table_readable(tbl_hurt[k][i]) && id_hurts(tbl_hurt[k][i], id))
                dmg = ((const signed char *)tbl_hurt[k][i])[2 * id + 1];   /* a guard pose: what its open table takes */
            else if (!(dmg = soft_damage(tbl_hurt[k][i])))
                continue;
            for (j = 0; j < N_PATCH && patch[j].t; j++)
                ;
            if (j == N_PATCH)
                continue;
            patch[j].orig[0] = ((const unsigned char *)t)[2 * id];
            patch[j].orig[1] = ((const unsigned char *)t)[2 * id + 1];
            hit[0] = 0;
            hit[1] = (unsigned char)dmg;
            if (!patch_write(t, id, hit))
                continue;
            patch[j].t = t;
            patch[j].id = (unsigned char)id;
            patch[j].at = hp_frames;
            n++;
            if (test_on)
                test_log("guard: %p type %02x table %08x id %02x %02x %02x -> 00 %02x (hurt table %08x)", o, O8(o, 9), t,
                         id, patch[j].orig[0], patch[j].orig[1], dmg, tbl_hurt[k][i]);
        }
    }
    return n;
}

/* test: every live object of any pool within r px of (x, y) - pool, type, table, its entry for id, hit points (the
 * guard-break bullet against an enemy it does not hurt: which pool, which table) */
void objs_log_near(float x, float y, int r, int id)
{
    int k, i;
    char *o;
    for (k = 0; k < N_POOLS; k++)
        for (i = 0; i < n_objs[k]; i++) {
            unsigned t;
            o = objs[k][i];
            if (!O8(o, 8) || k == 4 || k == 6 || PX(o) - x >= r || x - PX(o) >= r || PY(o) - y >= r || y - PY(o) >= r)
                continue;
            t = *(volatile unsigned *)(o + 0x80);
            test_log("near pool %d%s %p type %02x xy %.0f %.0f table %08x id %02x -> %02x %02x, hurt table %08x, "
                     "hp %u, box %p", k, (GUARD_POOLS >> k & 1) ? "" : " (not a guard pool)", o, O8(o, 9), PX(o),
                     PY(o), t, id, table_readable(t) ? ((unsigned char *)t)[2 * id] : 0xEE,
                     table_readable(t) ? ((unsigned char *)t)[2 * id + 1] : 0xEE, k < N_POOLS ? tbl_hurt[k][i] : 0,
                     *(volatile unsigned short *)(o + 0x84), *(void **)(o + 0x7C));
        }
}

void objs_hp_frame(void)
{
    int k, i;
    char *o;
    unsigned short hp;

    hp_frames++;
    patches_frame();
    for (k = 0; k < N_POOLS; k++) {
        if (!(GUARD_POOLS >> k & 1))
            continue;
        for (i = 0; i < n_objs[k]; i++) {
            o = objs[k][i];
            if (TARGET_POOLS >> k & 1)
                combo_frame(k, i, o);
            guard_frame(k, i, o);
            hp = O8(o, 8) ? *(volatile unsigned short *)(o + 0x84) : 0;
            if (hp < hp_seen[k][i])
                hurt_at[k][i] = hp_frames;
            if (O8(o, 8)) {
                hurt_x[k][i] = PX(o);
                hurt_y[k][i] = PY(o);
            }
            hp_seen[k][i] = hp;
        }
    }
}

/* (the guard-break bullet's end: the target pools only, as in 0.0.5 - with pool 10 (GUARD_POOLS, 2026-11-07) it ended
 * on enemies it used to pass, user report 2026-11-08) */
int objs_hurt_near(float x, float y, int r)
{
    int k, i;

    for (k = 0; k < N_POOLS; k++) {
        if (!(TARGET_POOLS >> k & 1))
            continue;
        for (i = 0; i < n_objs[k]; i++)
            if (hurt_at[k][i] && hp_frames - hurt_at[k][i] <= 2 && hurt_x[k][i] - x < r && x - hurt_x[k][i] < r &&
                hurt_y[k][i] - y < r && y - hurt_y[k][i] < r)
                return 1;
    }
    return 0;
}

/* the enemy hurt (hit points down) within the last 2 player frames nearest to (x, y) within r px, 0 none (the piercing
 * bullet: what it went through) */
char *objs_hurt_obj_near(float x, float y, int r)
{
    int k, i;
    char *best = 0;
    float bd = (float)r * r * 2, dx, dy;

    for (k = 0; k < N_POOLS; k++) {
        if (!(GUARD_POOLS >> k & 1))
            continue;
        for (i = 0; i < n_objs[k]; i++) {
            if (!hurt_at[k][i] || hp_frames - hurt_at[k][i] > 2)
                continue;
            dx = hurt_x[k][i] - x;
            dy = hurt_y[k][i] - y;
            if (dx < r && dx > -r && dy < r && dy > -r && dx * dx + dy * dy < bd) {
                bd = dx * dx + dy * dy;
                best = objs[k][i];
            }
        }
    }
    return best;
}

/* a live enemy / boss within r px of (x, y) whose damage table (+0x80) is table */
char *objs_table_near(float x, float y, int r, unsigned table)
{
    int k, i;
    char *o;
    for (k = 0; k < N_POOLS; k++) {
        if (!(TARGET_POOLS >> k & 1))
            continue;
        for (i = 0; i < n_objs[k]; i++) {
            o = objs[k][i];
            if (O8(o, 8) && O32(o, 0x80) == table && PX(o) - x < r && x - PX(o) < r && PY(o) - y < r && y - PY(o) < r)
                return o;
        }
    }
    return 0;
}

/* a target: live with a hurt box (the hit points field of PC objects is not known; a beaten enemy loses its box) */
static int alive(char *o)
{
    return O8(o, 8) && *(void **)(o + 0x7C);
}

/* the live targets within rx / ry px of (x, y) (the Giga Crush: the screen around Axl), at most max */
int objs_targets(float x, float y, int rx, int ry, char **out, int max)
{
    int k, i, n = 0;
    char *o;

    for (k = 0; k < N_POOLS && n < max; k++) {
        if (!(TARGET_POOLS >> k & 1))
            continue;
        for (i = 0; i < n_objs[k] && n < max; i++) {
            o = objs[k][i];
            if (alive(o) && PX(o) - x < rx && x - PX(o) < rx && PY(o) - y < ry && y - PY(o) < ry)
                out[n++] = o;
        }
    }
    return n;
}

/* an enemy bullet Axl's shots cannot break (user report 2026-10-29: the bullets bent toward them): small (attack box
 * at most 32 x 32), moving, and its damage table immune to the buster (id 0: FF) */
static int unbreakable_shot(char *o)
{
    unsigned char *b = *(unsigned char **)(o + 0x78);
    unsigned t = *(volatile unsigned *)(o + 0x80);
    return b && b[6] <= 32 && b[7] <= 32 && (O32(o, 0x3C) || O32(o, 0x40)) && immune_table(t);
}

char *find_target(float x, float y, int range)
{
    char *best = 0, *o;
    float bd = (float)range * range, dx, dy, d;
    int k, i;

    for (k = 0; k < N_POOLS; k++) {
        if (!(TARGET_POOLS >> k & 1))
            continue;
        for (i = 0; i < n_objs[k]; i++) {
            o = objs[k][i];
            if (!alive(o) || unbreakable_shot(o))
                continue;
            dx = PX(o) - x;
            dy = PY(o) - y;
            if (dx < -range || dx > range || dy < -range || dy > range)
                continue;
            d = dx * dx + dy * dy;
            if (d < bd) {
                bd = d;
                best = o;
            }
        }
    }
    return best;
}

/* pool 5 also holds what enemies throw (the intro's grenade: type 0, moving, a 2026-10-27 dump) next to bosses and
 * their parts: a thrown thing is small (attack box at most 32 x 32) and moving */
static int thrown(char *o)
{
    unsigned char *b = *(unsigned char **)(o + 0x78);
    return O8(o, 9) == 0 && b[6] <= 32 && b[7] <= 32 && (O32(o, 0x3C) || O32(o, 0x40));   /* (type 0: the only one
                                                                                     * measured; small moving enemies
                                                                                     * stay targets) */
}

/* enemy bullets with an attack box within r px of (x, y) are blocked: moved far below so their own off-screen
 * check frees them (their owners keep pointers to them), their box with them. Returns how many. (The box pointer
 * +0x78 is left alone: Yammark's stage's type 05 shot reads through it every frame - cleared, the game crashed at
 * 0x020B5BA0 on the giga's clearing, user report 2026-11-10) */
static int enemy_shots(float x, float y, int r, int clear)
{
    char *o;
    int k, i, n = 0;

    for (k = 0; k < N_POOLS; k++) {
        if (!(EBULLET_POOLS >> k & 1) && k != 5)
            continue;
        for (i = 0; i < n_objs[k]; i++) {
            o = objs[k][i];
            if (!O8(o, 8) || !*(void **)(o + 0x78) || PX(o) - x > r || x - PX(o) > r || PY(o) - y > r || y - PY(o) > r)
                continue;
            if (k == 5 && !thrown(o))
                continue;
            if (!clear) {
                n++;
                continue;
            }
            if (test_on)
                test_log("enemy shot blocked: pool %d %p type %02x xy %.0f %.0f", k, o, O8(o, 9), PX(o), PY(o));
            PY(o) += 0x400;
            n++;
        }
    }
    return n;
}

int block_enemy_shots(float x, float y, int r)
{
    return enemy_shots(x, y, r, 1);
}

int count_enemy_shots(float x, float y, int r)   /* the same ones, left as they are (the test's eshots) */
{
    return enemy_shots(x, y, r, 0);
}

/* Ride-armor-like bosses (user request 2026-11-02: Turtloid, Shark, Sheldon changed - a press stops on top of them).
 * The press's hits are solid objects of pool 10: the game lists their boxes for the terrain every frame
 * (FUN_023d1320 walks pool 10, 0 .. 0x4F: box = comp FUN_023c9f50 -> FUN_02ae0d90, bytes 4..7 = dx, dy, half width,
 * half height; dx mirrored when the facing byte is 0x40); their y = base (task + 0xDE) + layer 0 y - layer 1 y
 * (FUN_023f24b0), and the press's picture is screen layer 1 (axl_dna.c). objs_overhead_bottom: the lowest bottom of
 * the solids over Axl (x within half_w, their top above top), -1e9 when none. */
typedef unsigned char *(__fastcall *getter_t)(void *ecx, void *edx);
#define SOLID_COMP ((getter_t)0x023C9F50)
#define SOLID_BOX ((getter_t)0x02AE0D90)
#define SOLID_POOL 10

static int solid_box(char *o, float *x0, float *x1, float *y0, float *y1)
{
    unsigned char *c, *b;
    float cx, cy;
    if (!O8(o, 8) || !(c = SOLID_COMP(o, 0)) || !(b = SOLID_BOX(c, 0)))
        return 0;
    cx = PX(o) + ((O8(o, 0x29) & 0x40) ? -(float)(signed char)b[4] : (float)(signed char)b[4]);
    cy = PY(o) + (float)(signed char)b[5];
    *x0 = cx - b[6];
    *x1 = cx + b[6];
    *y0 = cy - b[7];
    *y1 = cy + b[7];
    return 1;
}

float objs_overhead_bottom(float x, float half_w, float top)
{
    int i;
    float x0, x1, y0, y1, b = -1e9f;
    for (i = 0; i < n_objs[SOLID_POOL]; i++)
        if (solid_box(objs[SOLID_POOL][i], &x0, &x1, &y0, &y1) && x1 > x - half_w && x0 < x + half_w && y0 < top &&
            y1 > b)
            b = y1;
    return b;
}

/* a solid beside the player: its side within 4 px of x0 .. x1 (his terrain box's sides) and overlapping y0 .. y1 */
int objs_solid_beside(float x0, float x1, float y0, float y1)
{
    int i;
    float a, b, c, d;
    for (i = 0; i < n_objs[SOLID_POOL]; i++)
        if (solid_box(objs[SOLID_POOL][i], &a, &b, &c, &d) && d > y0 && c < y1 &&
            ((a > x1 - 4 && a < x1 + 4) || (b > x0 - 4 && b < x0 + 4)))
            return 1;
    return 0;
}

/* (test) the i-th remembered object of a pool, 0 if none */
char *objs_get(int pool, int i)
{
    return pool >= 0 && pool < N_POOLS && i >= 0 && i < n_objs[pool] ? objs[pool][i] : 0;
}
