/* DNA Change on PC (port of PS1 mods/axl/src/axl_dna.c without the bosses' attacks yet).
 *
 * With boss weapon k (1-8, X's weapon numbers: 1 Yammark ... 8 Mijinion) selected and its energy >= DNA_MIN, the
 * special button turns Axl into that boss: a short pose (Axl's anim 204), then he is drawn with the boss's own
 * pictures (tools/lc2_dna.py: texture / parts section / palettes added to pl02*.arc) while his own animation,
 * frame, hurt box and moves go on underneath, as on PS1. The weapon's energy is the change gauge: it drains while
 * changed (DRAIN_FRAMES for a full gauge; White Axl: no drain) and the change ends when it is empty, on the special
 * button again, on a weapon change or when a cutscene takes control; every other boss gauge refills meanwhile, and
 * all of them while not changed (empty to full in FILL_FRAMES).
 *
 * The look is applied after X's hurt box (axl_box) and taken back at the start of the next frame (axl_frame), so only
 * the draw sees it (PS1 swapped the fields around its draw call): texture index P+0x54, parts section P+0x58,
 * palette slot P+0x5E, frame P+0x66; a pose taller than the signed-byte parts allow is drawn with the player moved
 * up by its lift (float y P+0x1C), and one moved along x (boss_look 'shift': Heatnix's firebird) with the player
 * moved by its push (float x P+0x14).
 */
#include <windows.h>
#include <math.h>
#include <string.h>

#include "axl_lc2.h"
#include "axl_proj.h"
#include "axl_data.h"      /* the game-derived tables (axl_data.bin) */
#include "axl_test.h"

typedef unsigned char u8;
typedef unsigned short u16;
typedef int s32;
#define O8(p, o) (*(volatile u8 *)((p) + (o)))
#define O16(p, o) (*(volatile u16 *)((p) + (o)))
#define O32(p, o) (*(volatile s32 *)((p) + (o)))

#define STATE(p) O8(p, 0x0D)
#define ANIM(p) O8(p, 0x2B)
#define NEWP(p) O16(p, 0xC4)
#define WEAPON(p) O8(p, 0x14C)                              /* PS1 +0x93 */
#define ENERGY(p, k) (*(volatile short *)((p) + 0x1A0 + 2 * (k)))   /* PS1 +0xA8 + 2k (B + 0xE4) */
#define ENERGY_MAX(p) (*(volatile short *)((p) + 0x2CC))     /* PS1 +0x14A */
#define LOCK(p) O8(p, 0x1EC)
#define B_SPECIAL 0x10

#define DNA_MIN_DIV 4         /* a change needs a quarter of the gauge */
#define DRAIN_FRAMES 600
/* X's parts on the change's drain (user report 2026-10-29 stream: Energy Saver did nothing; docs/research-x6-parts-bits-
 * 20261029.md): Energy Saver (bit 0x20 of the parts as equipped, st + 0xB0 - not P's copy, X main hides it, axl_x.c
 * charge_hide) halves it, the limited Weapon Drive (mode 3 in P + 0x2B8) stops it */
#define PART_ENERGY_SAVER 0x20
#define LIMITED_WEAPON_DRIVE 3

static int drain_frames(char *p)
{
    char *g = *(char **)0x041D8C8C;
    if (*(volatile int *)(p + 0x2B8) == LIMITED_WEAPON_DRIVE)
        return 0;
    return g && (*(volatile unsigned *)(g + 0x70 + 0xB0) & PART_ENERGY_SAVER) ? 2 * DRAIN_FRAMES : DRAIN_FRAMES;
}
#define FILL_FRAMES 1200
#define ANIM_IDLE 5
#define ANIM_FALL 11
#define ANIM_DNA 204

typedef void(__fastcall *set_anim_t)(char *ecx, void *edx, char *obj, int anim);
#define SET_ANIM ((set_anim_t)0x023C1730)

u8 dna_boss, dna_pose, dna_alt;
int dna_toss_t = -1, dna_shell_away;
static u16 dna_t;
static unsigned drain_acc, fill_acc;
static short atk_cur = -1;
static u16 atk_n;
static u8 look_i, look_d, look_s;
int dna_look_code(void) { return look_s; }   /* test (axl_test.c look): 0 idle, 2 attack, 3 move, 4-7 motion */
/* the fields the look replaced for the draw */
static struct { char *p; s32 tex, pat; u16 pal; u8 frame; float x, y; int on; } saved;

/* the game's CLUT upload FUN_023aaef0(slot, src, count): copies count * 16 colours to the emulated VRAM CLUTs
 * (*0x041D8C44 + 0x1002C + 32 * slot) and marks them for the GPU (PS1 FUN_8002b3d4) */
typedef void(__fastcall *clut_upload_t)(void *ecx, void *edx, int slot, const void *src, int n);
#define CLUT_UPLOAD ((clut_upload_t)0x023AAEF0)
#define CLUT_OWNER (*(void **)0x041D8C44)

/* Our palettes' CLUT slots (user report 2026-10-27: the gate stage's big piece of art took the boss's / the badges'
 * colours, the fixed slots 400-403 / 410-454 are that stage's): a run of slots whose colours are all 0 (none loaded)
 * is taken when needed, its colours kept and put back when done (the change's end, the menu closed) */
#define CLUT_SLOT_MEM(s) ((unsigned short *)((char *)CLUT_OWNER + 0x1002C) + 16 * (s))
#define CLUT_N_SLOTS 512

int clut_free_run(int n, int lo, int hi)   /* the highest run of n empty slots outside [lo, hi), -1 if none */
{
    int s, k, run = 0;

    if (!CLUT_OWNER)
        return -1;
    for (s = CLUT_N_SLOTS - 1; s >= 32; s--) {
        unsigned short *c = CLUT_SLOT_MEM(s);
        int empty = s < lo || s >= hi;
        for (k = 0; empty && k < 16; k++)
            empty = c[k] == 0;
        run = empty ? run + 1 : 0;
        if (run == n)
            return s;
    }
    return -1;
}

int dna_pal_slot = DNA_PAL_SLOT;
static unsigned short dna_pal_orig[4 * 16];
static int dna_pal_held;

static void dna_pal_take(void)
{
    int s;

    if (dna_pal_held || !CLUT_OWNER)
        return;
    s = clut_free_run(4, menu_pal_lo, menu_pal_lo + MENU_SLOTS);
    dna_pal_slot = s >= 0 ? s : DNA_PAL_SLOT;
    memcpy(dna_pal_orig, CLUT_SLOT_MEM(dna_pal_slot), sizeof dna_pal_orig);
    dna_pal_held = 1;
    if (test_on)
        test_log("dna palettes at slot %d%s", dna_pal_slot, s < 0 ? " (no free run: the default)" : "");
}

static void dna_pal_give(void)
{
    if (!dna_pal_held)
        return;
    if (CLUT_OWNER)
        CLUT_UPLOAD(CLUT_OWNER, 0, dna_pal_slot, dna_pal_orig, 4);
    dna_pal_held = 0;
}

static void boss_palettes(int k)
{
    dna_pal_take();
    if (CLUT_OWNER)
        CLUT_UPLOAD(CLUT_OWNER, 0, dna_pal_slot, dna_pals[k], 4);
}

/* the boss's palettes blended halfway toward Axl's hurt flash (the sheet's light blue, about (150, 160, 255)) */
#define HURT_FRAMES 32
static int hurt_t;

static void hurt_palettes(int k)
{
    static unsigned short pal[64];
    int i;
    unsigned c, r, g, b;

    if (!CLUT_OWNER)
        return;
    for (i = 0; i < 64; i++) {
        c = dna_pals[k][i];
        if (!(c & 0x7fff)) {
            pal[i] = (unsigned short)c;
            continue;
        }
        r = c & 31, g = c >> 5 & 31, b = c >> 10 & 31;
        r = (r + 19) / 2, g = (g + 20) / 2, b = (b + 31) / 2;
        pal[i] = (unsigned short)(r | g << 5 | b << 10 | (c & 0x8000));
    }
    CLUT_UPLOAD(CLUT_OWNER, 0, dna_pal_slot, pal, 4);
}

/* The change's sparkle (PS1 axl_dna.c): during the pose the body flashes white every other 4 frames (its palette slot
 * P+0x5E gets a white one, then X's palettes come back by FUN_023ac680 - PS1 FUN_8003c4ec); when the pose ends the
 * whole screen flashes white for FLASH_LEN frames. PS1 drew an additive white tile; on PC every CLUT in use
 * (FLASH_SLOTS slots of the emulated VRAM) is blended toward white and put back afterwards. */
typedef void(__fastcall *xpal_t)(void *game);
#define X_PALETTES ((xpal_t)0x023AC680)
#define GAME (*(void **)0x041D8C8C)
#define CLUT_MEM ((unsigned short *)((char *)CLUT_OWNER + 0x1002C))
#define FLASH_LEN 8
#define FLASH_SLOTS 512
static const unsigned short flash_pal[16] = {0, 0x7fff, 0x7fff, 0x7fff, 0x7fff, 0x7fff, 0x7fff, 0x7fff,
                                             0x7fff, 0x7fff, 0x7fff, 0x7fff, 0x7fff, 0x7fff, 0x7fff, 0x7fff};
static const unsigned char flash_level[FLASH_LEN] = {64, 96, 128, 160, 192, 224, 255, 255};   /* used from the end */
static unsigned short flash_saved[FLASH_SLOTS * 16], flash_buf[FLASH_SLOTS * 16];
static int flash_t;

static void body_flash(char *p, int white)
{
    if (!CLUT_OWNER || !GAME)
        return;
    if (test_on)
        test_log("body flash %d slot %d", white != 0, O16(p, 0x5E));
    if (white)
        CLUT_UPLOAD(CLUT_OWNER, 0, O16(p, 0x5E), flash_pal, 1);
    else
        X_PALETTES(GAME);
}

static void screen_flash_start(void)
{
    if (!CLUT_OWNER)
        return;
    if (!flash_t)
        memcpy(flash_saved, CLUT_MEM, sizeof flash_saved);
    flash_t = FLASH_LEN;
}

void dna_screen_flash(void)   /* (the Giga Crush's flash, axl_x.c) */
{
    screen_flash_start();
}

int dna_flashing(void)
{
    return flash_t != 0;
}

/* every player frame */
void dna_flash_tick(void)
{
    int i, l;
    unsigned c, r, g, b;

    if (!flash_t || !CLUT_OWNER)
        return;
    if (--flash_t == 0) {
        CLUT_UPLOAD(CLUT_OWNER, 0, 0, flash_saved, FLASH_SLOTS);
        if (test_on)
            test_log("screen flash end: clut[17] %04x", CLUT_MEM[17]);
        return;
    }
    l = flash_level[flash_t];
    for (i = 0; i < FLASH_SLOTS * 16; i++) {
        c = flash_saved[i];
        if (!(c & 0x7fff)) {                    /* transparent stays */
            flash_buf[i] = (unsigned short)c;
            continue;
        }
        r = c & 31, g = c >> 5 & 31, b = c >> 10 & 31;
        r += (31 - r) * l / 255, g += (31 - g) * l / 255, b += (31 - b) * l / 255;
        flash_buf[i] = (unsigned short)(r | g << 5 | b << 10 | (c & 0x8000));
    }
    CLUT_UPLOAD(CLUT_OWNER, 0, 0, flash_buf, FLASH_SLOTS);
    if (test_on)
        test_log("screen flash %d: clut[17] %04x -> %04x (memory %04x)", flash_t, flash_saved[17], flash_buf[17],
                 CLUT_MEM[17]);
}

static int armor(void)
{
    char *g = *(char **)0x041D8C8C;
    return g ? g[0x70 + 0x9A] : 0;
}

static int owned(char *p, int k)
{
    return k >= 1 && k <= 8 && (OWNED(p) >> (k - 1) & 1);   /* the beaten bosses (every energy is full anyway) */
}

static void pose_done(char *p)
{
    if (ANIM(p) == ANIM_DNA)
        SET_ANIM(p, 0, p, (STATE(p) == 6 || STATE(p) == 7) ? ANIM_FALL : ANIM_IDLE);
}

/* the change over: Axl's anim for the state he is in, whatever a move left (a roll / bounce / flight anim stayed on
 * him standing - user report 2026-10-28, Turtloid / Heatnix back to Axl in a mid-air pose) */
#define ANIM_WALK 8
static void end_anim(char *p)
{
    int st = STATE(p);
    if (O8(p, 0x0C) != 1)   /* (dying, a cutscene: the game's anim stays) */
        return;
    SET_ANIM(p, 0, p, st == 6 || st == 7 ? ANIM_FALL : st == 3 || st == 4 ? ANIM_WALK : ANIM_IDLE);
}

/* the change's end as its start (user request 2026-10-28: he just popped back; the boss blinks, then Axl - no sound):
 * the boss's look stays for the pose's length, held like the pose, white every other 4 frames (dna_look), then the
 * change is over with the screen's white flash */
#define LEAVE_LEN DNA_POSE_LEN
static int leave_t;

int dna_leaving(void)
{
    return leave_t != 0;
}
static int look_pose = -1;   /* the boss picture drawn last (dna_ghost) */

static void dna_end(char *p)
{
    moves_reset(p);
    proj_kill_dna();   /* (the change's shots go with it: user request 2026-10-28) */
    dna_pal_give();
    hurt_t = 0;
    dna_boss = 0;
    dna_pose = 0;
    atk_n = 0;
    atk_cur = -1;
    leave_t = 0;
    dna_toss_t = -1;
    dna_shell_away = 0;
    end_anim(p);
    if (test_on)
        test_log("dna end");
}

/* Shark, Turtloid, Sheldon: a press comes down onto them as onto the floor - a thud and back up (user request
 * 2026-11-02). The Metal Shark stage's (4) ceiling press: its task's offset (16.16 + 0x68, whole part + 0x6A, larger =
 * lower) sets screen layer 1 (y = camera y + its offset - that, at 0x024FC6B2), the press's picture, and its solids
 * follow layer 1 (FUN_023f24b0). Coming down (state 4, FUN_024fcf00) it asks a contact flag (FUN_023b6cd0) each frame;
 * set, it thuds (shake, sound 5), keeps that place (+ 0x6C) and goes back up as in the game. Each frame, after the
 * objects moved (the game's solid list, axl_hook.c), the lowest solid over the changed boss gives the offset at which
 * its bottom meets the boss's top = his picture's (dna_room + its overlap 16); reaching it counts as the contact
 * (dna_press_contact). Only there: layer 1 is plain scenery elsewhere. press_p: changed this frame. */
#define PRESS_STAGE 4
#define PRESS_OFF(t) (*(volatile short *)((t) + 0x6A))
static char *press_p;
static int press_max = 0x7fff, press_now, press_seen;   /* the offset at the boss's top, its last value, a press seen */

void __cdecl dna_press_task(char *task)
{
    if (test_on && PRESS_OFF(task) != press_now)
        test_log("dna press offset %d", PRESS_OFF(task));
    press_seen = 1;
    press_now = PRESS_OFF(task);
}

typedef char(__fastcall *contact_t)(void *ecx, void *edx, int i);
#define PRESS_CONTACT ((contact_t)0x023B6CD0)

char __fastcall dna_press_contact(void *ecx, void *edx, int i)
{
    char r = PRESS_CONTACT(ecx, edx, i);
    if (!r && press_max != 0x7fff && press_now >= press_max) {
        if (test_on)
            test_log("dna press thud on boss %d at %d", dna_boss, press_now);
        return 1;
    }
    return r;
}

/* Clinging to the press, Axl goes up with it as the Shadow Armor would (user request 2026-11-02: the game carries a
 * wall-clinging player only down with a wall; shooting on the rising press he stayed in the air): the press's rise
 * this frame (its offset, the solids' y 1:1) is added to his y while he clings (9) or slides (11) beside one of its
 * solids. */
static int press_last = -0x8000;

static void press_carry(char *p)
{
    int d = press_last == -0x8000 ? 0 : press_now - press_last;
    float x = *(volatile float *)(p + 0x14), y = *(volatile float *)(p + 0x1C);
    press_last = press_now;
    if (d >= 0 || (STATE(p) != 9 && STATE(p) != 11) || !objs_solid_beside(x - 8, x + 8, y - 16, y + 22))
        return;
    *(volatile float *)(p + 0x1C) = y + d;
    if (test_on)
        test_log("press carry %d (state %d)", d, STATE(p));
}

void dna_press_hold(void)
{
    char *p = press_p, *g = *(char **)0x041D8C8C;
    float feet, half, top, b;
    press_p = 0;
    if (p && g && press_seen && g[0x70 + 0x48] == PRESS_STAGE)
        press_carry(p);
    else
        press_last = -0x8000;
    if (!press_seen || !p || !g || !(dna_boss == 4 || dna_boss == 6 || dna_boss == 7) || g[0x70 + 0x48] != PRESS_STAGE) {
        press_max = 0x7fff;   /* (not changed, or another boss: the press as in the game) */
        return;
    }
    feet = *(volatile float *)(p + 0x1C) + 22;
    half = dna_box[dna_boss][2] / 2 > 24 ? dna_box[dna_boss][2] / 2 : 24;
    top = feet - (dna_room[dna_boss] + 16);
    b = objs_overhead_bottom(*(volatile float *)(p + 0x14), half, top);
    press_max = b < -1e8f ? 0x7fff : press_now - (int)(b - top);   /* (the solids move with the offset 1:1) */
}

static float pose_x;   /* the pose and the leaving blink hold Axl's x (user report 2026-10-29: the boss slid forward;
                        * X's state code - a dash, a walk - kept moving him under the picture) */

static void hold_x(char *p)
{
    *(volatile float *)(p + 0x14) = pose_x;
    O32(p, 0x3C) = 0;
}

/* the change to end: its leaving blink first (not in a cutscene or on a dying player: at once) */
static void dna_leave(char *p)
{
    if (leave_t)
        return;
    if (LOCK(p) || O8(p, 0x0C) != 1 || dna_pose) {   /* (in the pose: no boss shown yet to blink) */
        dna_end(p);
        return;
    }
    moves_reset(p);
    proj_kill_dna();
    hurt_t = 0;
    atk_n = 0;
    atk_cur = -1;
    leave_t = LEAVE_LEN;
    axl_dash_cancel(p);
    pose_x = *(volatile float *)(p + 0x14);
    if (test_on)
        test_log("dna leave");
}

#ifndef AXL_RELEASE
/* test (seq 'unchange'): the change ended as when its gauge empties, the gauge left as it is */
void dna_test_end(void)
{
    if (axl_p && dna_boss)
        dna_end(axl_p);
}
#endif

/* Weakness (user request 2026-10-26): changed into boss k, Axl hit while the boss whose weapon beats k is there loses
 * the change and boss k's whole gauge. The weaknesses are the bosses' damage tables (PS1 EXE, +0x58 tables: damage 5
 * for one weapon; Mijinion's by elimination): 1 Yammark <- 8 Mijinion, 2 Wolfang <- 3 Heatnix, 3 Heatnix <- 5
 * Scaravich, 4 Shark <- 6 Turtloid, 5 Scaravich <- 1 Yammark, 6 Turtloid <- 2 Wolfang, 7 Sheldon <- 4 Shark,
 * 8 Mijinion <- 7 Sheldon. A boss is found by its object type (PS1 boss types, docs/boss_attacks.md section 0:
 * the same in the stage fights and the rematches, assumed); hit points P+0x84 (PS1 +0x5C). */
static const u8 weak_of[9] = {0, 8, 3, 5, 6, 1, 2, 4, 7};
static const u8 boss_type[9] = {0, 0x31, 0x36, 0x34, 0x38, 0x32, 0x39, 0x33, 0x3A};

int dna_stage_boss_type(int stage)   /* the Maverick of stage 1-8 (his object type), 0 other stages */
{
    return stage >= 1 && stage <= 8 ? boss_type[stage] : 0;
}
#define HP(p) O8(p, 0x84)
static u8 last_hp;
static int pose_air;     /* the change started in the air: Axl hangs there until the pose ends, then falls */
static float pose_y;
int dna_weak_test = -1;   /* test (axl_test.c weakid): this object id counts as the weakness boss */

int dna_hurt_raw, dna_hurt_extra, dna_hits;   /* test (axl_test.c hurt_*, dna_hits): damage taken while changed */
static int taken_acc;

static int weak_hit(char *p)
{
    int hit = HP(p) < last_hp, w = weak_of[dna_boss], d = last_hp - HP(p), pct = boss_mods[dna_boss].taken_pct;
    char *b;

    last_hp = HP(p);
    if (!hit)
        return 0;
    dna_hurt_raw += d;
    dna_hits++;
    if (pct != 100 && HP(p) > 0) {   /* the specialty's damage taken (boss_mods): the extra part, fractions carried;
                                        * not below 1 (the game's own hit killed or it did not) */
        int e;
        taken_acc += d * (pct - 100);
        e = taken_acc / 100;
        taken_acc -= e * 100;
        if (e > HP(p) - 1)
            e = HP(p) - 1;
        if (e > 0) {
            HP(p) = (u8)(HP(p) - e);
            dna_hurt_extra += e;
        }
        last_hp = HP(p);   /* (read as this hit's: not a new one next frame) */
    }
    hurt_t = HURT_FRAMES;
    b = objs_find_id(dna_weak_test >= 0 ? dna_weak_test : boss_type[w]);
    if (test_on)
        test_log("dna hit: form %d weakness boss %d (type %02x) %s", dna_boss, w, boss_type[w], b ? "present" : "absent");
    return b != 0;
}

/* PS1 FUN_80016c48(kind, id, obj) = FUN_023be620 (thiscall on the sound manager *0x041D8E7C, 3 stack args):
 * kind picks the bank (1 = the player's, here Axl's), id is the request id in that bank's SRQR */
typedef void(__fastcall *se_t)(void *mgr, void *edx, int kind, int id, char *obj);
#define SE_PLAY ((se_t)0x023BE620)
#define SE_MGR (*(void **)0x041D8E7C)

/* the common bank (kind 0, se_com, loaded in every stage): request 13 = se_com_PG01_ID04, a short 1 kHz beep */
#define SE_REFUSE 13

static void refuse_beep(void)
{
    if (SE_MGR && axl_p)
        SE_PLAY(SE_MGR, 0, 0, SE_REFUSE, axl_p);
    if (test_on)
        test_log("dna refused: beep");
}

typedef void(__fastcall *se_stop_t)(void *mgr, void *edx, int kind, int id);
#define SE_STOP ((se_stop_t)0x023BE810)   /* FUN_023be810: stops the bank's sounds of that request id (0xff: all) */

void axl_sound_stop(int id)
{
    if (SE_MGR)
        SE_STOP(SE_MGR, 0, 1, id);
}

/* a voice on no object (as READY, axl_hook.c): on the player it went unheard (user report 2026-11-10, Yammark's
 * "Formation!" - the requests reached the game) */
int axl_se_own;   /* a request of ours under way (axl_voice / axl_sound) */

void axl_voice(int id)
{
    if (!SE_MGR)
        return;
    axl_se_own = 1;   /* (axl_hook.c se_hook: ours, not the game's - the voice slots 9 / 12 kept from it) */
    SE_PLAY(SE_MGR, 0, 1, id, 0);
    axl_se_own = 0;
    if (test_on)
        test_log("voice %d", id);
}

void axl_sound(int id)
{
    if (!SE_MGR || !axl_p)
        return;
    axl_se_own = 1;
    SE_PLAY(SE_MGR, 0, 1, id, axl_p);
    axl_se_own = 0;
    if (test_on)
        test_log("sound %d", id);
}

/* Boss-size collision (user decision 2026-10-26): changed, Axl's hurt box is the boss's (dna_box: since 2026-11-02 the
 * boss's own hurt box from the original fight, before that his idle picture's extent)
 * (the change asked the boss's room free above him, 2026-11-02; removed, user decision 2026-11-14: "변신 할 때 높이조건
 * ... 없애줘" - he changes under any ceiling). */
typedef struct { const void *vt; signed char b[4]; } box_t;
#define BOX_VT ((const void *)0x031DA67C)
static box_t boss_boxes[9];
/* axl_box: the changed boss's hurt box (0 = keep X's) */
void *dna_hurt_box(void)
{
    if (!dna_boss || dna_pose)
        return 0;
    if (!boss_boxes[dna_boss].vt) {
        boss_boxes[dna_boss].vt = BOX_VT;
        memcpy(boss_boxes[dna_boss].b, dna_box[dna_boss], 4);
    }
    return &boss_boxes[dna_boss];
}

/* every unit pass (axl_menu.c move_hook, not while the menu is open): the player whose X main no longer runs (his
 * death, the stage's end) while changed */
void dna_watch(void)
{
    static unsigned last, stale;
    char *p = axl_p;

    stale = axl_frames == last ? stale + 1 : 0;
    last = axl_frames;
    if (!p || stale < 2 || (!dna_boss && !saved.on))
        return;
    if (O8(p, 0x0C) == 1)
        return;
    /* init 2 (his death: hit points 0, X main stopped, measured 2026-10-26): the boss look would stay on the dying
     * player (and his death anim stall): Axl's look back, the change over */
    dna_unlook();
    dna_pal_give();
    moves_reset(p);
    leave_t = 0;
    if (test_on)
        test_log("dna watch: player init %d (hp %d) while changed: change ended", O8(p, 0x0C), O8(p, 0x84));
    dna_boss = 0;
    dna_pose = 0;
    atk_n = 0;
    atk_cur = -1;
}

void dna_reset(void)
{
    moves_reset(0);
    dna_alt = 0;        /* (and the rest of the change: user request 2026-10-27, every boss starts afresh) */
    look_s = 0xff;
    look_i = look_d = 0;
    hurt_t = 0;
    atk_n = 0;
    atk_cur = -1;
    dna_pal_held = 0;   /* (the stage is gone: nothing of its to put back) */
    leave_t = 0;
    dna_toss_t = -1;
    dna_shell_away = 0;
    dna_boss = 0;
    dna_pose = 0;
}

/* the look's fields back before anything of the game reads them again */
static u16 axl_pal;   /* Axl's palette slot as last seen outside a look */

int dna_test_nolift;   /* test (axl_test.c 'nolift on') */

void dna_unlook(void)
{
    if (!saved.on)
        return;
    O32(saved.p, 0x54) = saved.tex;
    O32(saved.p, 0x58) = saved.pat;
    O16(saved.p, 0x5E) = saved.pal == dna_pal_slot ? axl_pal : saved.pal;   /* (never leave the boss's slot) */
    O8(saved.p, 0x66) = saved.frame;
    *(volatile float *)(saved.p + 0x14) = saved.x;
    *(volatile float *)(saved.p + 0x1C) = saved.y;
    saved.on = 0;
}

/* before X's state code (input not yet hidden): start / pose / end. 1 while the pose holds Axl. */
int dna_before(char *p, int special_new, int busy)
{
    int k = WEAPON(p), i;

    /* every boss gauge refills (empty to full in FILL_FRAMES), the changed boss's one only while not changed (user
     * request 2026-11-13: the others fill up during a change too) */
    for (fill_acc += ENERGY_MAX(p); fill_acc >= FILL_FRAMES; fill_acc -= FILL_FRAMES)
        for (i = 1; i <= 8; i++)
            if (owned(p, i) && i != dna_boss && ENERGY(p, i) < ENERGY_MAX(p))
                ENERGY(p, i)++;
    if (leave_t) {   /* the leaving blink holds Axl (as the pose), the change over at its end */
        if (LOCK(p)) {
            dna_end(p);
            return 0;
        }
        if (--leave_t == 0) {
            dna_end(p);
            screen_flash_start();
            return 0;
        }
        hold_x(p);
        return 1;
    }
    if (dna_boss && LOCK(p)) {
        dna_end(p);
        return 0;
    }
    if (dna_boss && (k != dna_boss || (special_new && !dna_pose))) {
        dna_leave(p);
        return 1;
    }
    if (!dna_boss) {
        if (test_on && special_new)
            test_log("dna press k %d owned %d energy %d state %d busy %d", k, owned(p, k), k <= 8 ? ENERGY(p, k) : -1,
                     STATE(p), busy);
        if (!busy && special_new && owned(p, k) && ENERGY(p, k) >= ENERGY_MAX(p) / DNA_MIN_DIV &&
            (STATE(p) == 2 || STATE(p) == 3 || STATE(p) == 4 || STATE(p) == 6 || STATE(p) == 7 || STATE(p) == 8 ||
             STATE(p) == 12)) {   /* (12 = the ground dash, 2026-11-02: refused before, Axl dashed on under the beep -
                                   * "the boss slid forward"; the pose's held x stops him) */
            moves_reset(p);
            look_pose = -1;   /* (the last boss's picture: not for dna_relook) */
            dna_boss = (u8)k;
            dna_t = 0;
            drain_acc = 0;
            dna_pose = DNA_POSE_LEN;
            dna_alt = 0;
            axl_sound(SND_CHANGE);
            last_hp = HP(p);
            taken_acc = dna_hurt_raw = dna_hurt_extra = dna_hits = 0;   /* (the test's hurt_*: this change's) */
            axl_dash_cancel(p);
            pose_air = STATE(p) == 6 || STATE(p) == 7;
            pose_y = *(volatile float *)(p + 0x1C);
            pose_x = *(volatile float *)(p + 0x14);
            O32(p, 0x3C) = 0;
            look_s = 0xff;
            boss_palettes(k);
            SET_ANIM(p, 0, p, ANIM_DNA);
            if (test_on)
                test_log("dna start %d energy %d/%d", k, ENERGY(p, k), ENERGY_MAX(p));
        }
        if (!dna_boss && special_new && !busy && k >= 1 && k <= 8)   /* the change refused (no room, no gauge, ...):
                                                                    * a beep (user request 2026-10-27) */
            refuse_beep();
        return dna_boss != 0;
    }
    if (dna_pose) {
        if (pose_air) {
            *(volatile float *)(p + 0x1C) = pose_y;
            O32(p, 0x40) = 0;
        }
        axl_dash_cancel(p);
        hold_x(p);
        if (--dna_pose == 0) {
            pose_done(p);
            body_flash(p, 0);
            screen_flash_start();                 /* the pose ends: the whole screen flashes white */
        } else {
            if (ANIM(p) != ANIM_DNA && (STATE(p) == 2))
                SET_ANIM(p, 0, p, ANIM_DNA);
            body_flash(p, dna_pose & 4);
        }
        return 1;
    }
    /* (a ceiling coming within the boss's height while changed no longer ends the change: user decision 2026-10-27;
     * the room is only needed to start it) */
    if (weak_hit(p)) {
        ENERGY(p, k) = 0;
        dna_leave(p);
        return 1;
    }
    if (armor() != 4) {
        int df = drain_frames(p);
        if (df)
            for (drain_acc += ENERGY_MAX(p); drain_acc >= df; drain_acc -= df)
                if (ENERGY(p, k) > 0)
                    ENERGY(p, k)--;
        if (ENERGY(p, k) <= 0) {
            ENERGY(p, k) = 0;
            dna_leave(p);
            return 1;
        }
    }
    return 0;
}

/* The boss's own attack motion (docs/boss_attack_anims.md; dna_looks atk / at): mode 1 = played on the shot button,
 * the shot comes out at the boss's event frame; 2 = played at the change and at the mode switch (Yammark's option
 * summon, the options fire on their own timers); 3 = played at the change, the attack starts at the event frame
 * (unused now); 4 = played when the DNA button turns the attack on, which starts at the event frame (Sheldon's
 * Guard Shell: the shell held at the change, user report 2026-10-27 - the plates were out at once); 0 = none */
typedef void (*dna_fn)(char *p, int t, int fire, int fire_new);
static const dna_fn dna_fns[9] = {0, dna_yammark, dna_wolfang, dna_heatnix, dna_shark, dna_scaravich, dna_turtloid,
                                  dna_sheldon, dna_mijinion};
static const u8 atk_mode[9] = {0, 2, 1, 1, 1, 1, 0, 4, 1};
static u8 alt_prev;

/* after X's state code: the boss attack (fire = shot held, fire_new = new press, alt_new = DNA button) */
void dna_after(char *p, int fire, int fire_new, int alt_new)
{
    const dna_seq *q;
    int mode, len, i, go = 0;

    if (dna_boss && dna_pose && pose_air) {   /* (after X's move too: no frame of falling shows) */
        *(volatile float *)(p + 0x1C) = pose_y;
        O32(p, 0x40) = 0;
    }
    if (dna_boss && (dna_pose || leave_t))   /* (after X's move too) */
        hold_x(p);
    press_p = p;
    if (!dna_boss || dna_pose || leave_t)
        return;
    dna_t++;
    skill_tick();   /* (the special's and the dash's cooldowns, axl_skills.c: after this frame's moves_before) */
    dna_giga_tick(p);   /* (a giga attack under way, axl_dna_bosses.c) */
    if (alt_new && !(dna_boss == 7 && dna_shell_away)) {   /* (Sheldon: no plates while his shell is thrown) */
        if (dna_boss == 1)
            yam_next_form();   /* (his formations with their own motion - not the summon's: axl_dna_bosses.c) */
        else
            dna_alt ^= 1;
    }
    mode = atk_mode[dna_boss];
    q = &dna_looks[dna_boss].atk;
    if (!q->n && mode == 1)
        mode = 0;                                 /* no motion of its own: the shot button fires directly */
    if (dna_t == 1) {
        atk_n = (mode == 2 || mode == 3) && q->n;
        alt_prev = dna_alt;
    }
    if (mode == 4 && dna_alt != alt_prev) {
        alt_prev = dna_alt;
        atk_n = dna_alt && q->n;
    }
    if (mode == 2 && dna_alt != alt_prev) {
        alt_prev = dna_alt;
        atk_n = q->n != 0;
    }
    if (dna_boss == 3) {                          /* Heatnix: a press shoots, a hold charges (axl_dna_bosses.c) */
        if (heat_charge(fire, fire_new, moves_look() != 0 || dna_giga_look() >= 0, !atk_n) && mode == 1)
            atk_n = 1;
    } else if (mode == 1 && !atk_n && fire)
        atk_n = 1;                                /* held: the next attack as soon as the motion ends */
    atk_cur = -1;
    if (atk_n) {
        for (len = 0, i = 0; i < q->n; i++)
            len += q->dur[i];
        atk_cur = atk_n - 1;
        go = atk_cur == q->at;
        if (++atk_n > len)
            atk_n = 0;
    }
    if (test_on && (go || dna_t == 1))
        test_log("dna after k %d t %d mode %d go %d atk_cur %d fire %d", dna_boss, dna_t, mode, go, atk_cur, fire);
    if (mode == 1) {
        if (go)
            dna_cool_reset();
        dna_fns[dna_boss](p, dna_t, go, go);
    } else if ((mode != 3 && mode != 4) || atk_cur < 0 || atk_cur >= q->at)
        dna_fns[dna_boss](p, dna_t, fire, fire_new);
}

/* the shot slot's time until the next shot (axl_skills.c skill_left, for the HUD): the attack motion's frames left
 * (mode 1: the motion is the interval), else the boss attack's own cooldown */
int dna_shot_left(void)
{
    const dna_seq *q;
    int i, len;
    if (!dna_boss || dna_boss > 8)
        return 0;
    q = &dna_looks[dna_boss].atk;
    if (atk_mode[dna_boss] == 1 && q->n && atk_n) {
        for (len = 0, i = 0; i < q->n; i++)
            len += q->dur[i];
        return len - atk_n + 1;
    }
    return dna_cool_left();
}

/* after X's hurt box: put the boss's picture in for the draw */
static int seq_len(const dna_seq *q)
{
    int i, n = 0;
    for (i = 0; i < q->n; i++)
        n += q->dur[i];
    return n ? n : 1;
}

/* the boss's own motion for what Axl does (user request 2026-10-27: the jump / dash showed the standing loop):
 * flyers (Yammark, Heatnix, Mijinion) by their flight speed, the others by X's state (3 / 4 walk, 6 jump, 7 fall,
 * 8 land); 0 = none (standing / shooting). *code = its look_s value (4..7) */
extern int axl_floor_walk;
static const dna_seq *motion_seq(char *p, const dna_look_t *L, int *code)
{
    int st = STATE(p), m = 0;
    s32 vy = *(volatile s32 *)(p + 0x40), vx = *(volatile s32 *)(p + 0x3C);
    const dna_seq *q;

    if ((dna_boss == 1 || dna_boss == 3) && moves_yam_dash()) {   /* the dash (speeds 0, it moves him itself): the
        * flight's look that way - forward, up, down (user report 2026-11-10; Heatnix's plain dash too, 2026-11-12) */
        int dy, dx = moves_yam_dash_dir(&dy);
        m = dx ? 4 : dy < 0 ? 5 : 6;
    } else if (dna_flies() && axl_floor_walk)   /* (walking on the floor, down held: X crouched, axl_x.c flight) */
        m = 4;
    else if (dna_flies() && st == 26)
        m = 0;
    else if (dna_flies())
        m = st == 3 || st == 4 ? 4 : st != 6 && st != 7 ? 0 : vx ? 4 : vy > 0 ? 5 : vy < 0 ? 6 : 0;   /* (Yammark's
                                                   * order: vx, then vy; walking on the floor: forward flight) */
    else
        m = st == 3 || st == 4 ? 4 : st == 6 ? 5 : st == 7 ? 6 : st == 8 ? 7 : 0;
    q = m == 4 ? &L->walk : m == 5 ? ((dna_alt || dna_shell_away) && L->jump2.n ? &L->jump2 : &L->jump) :
        m == 6 ? ((dna_alt || dna_shell_away) && L->fall2.n ? &L->fall2 : &L->fall) : m == 7 ? &L->land : 0;   /* (jump2 / fall2: Sheldon
                                                   * with the plates up, no shell in his hands) */
    if (!q || !q->n)
        return 0;
    *code = m;
    return q;
}


/* an afterimage of the changed boss: its texture / parts section / palette and the picture drawn now
 * (axl_moves.c, Wolfang's ice dash); 0 = not changed */
int dna_ghost(char *w)
{
    const dna_look_t *L;
    if (!dna_boss || dna_pose || look_pose < 0)
        return 0;
    L = &dna_looks[dna_boss];
    proj_pose_look(w, dna_ghost_tex[dna_boss], L->pat, look_pose);   /* (palette: dna_pal_slot, apply_look) */
    if (proj_of(w)) {
        proj_of(w)->y -= L->lift[look_pose];
        proj_of(w)->x += (O8(w, 0x29) & 0x40) ? -L->push[look_pose] : L->push[look_pose];
    }
    return 1;
}

/* Yammark's dash afterimages (user request 2026-11-09: X's and Zero's dash shape, green, not blinking): the boss's
 * picture drawn with flat green palettes, 3 shades (newest brightest) x the boss's 4 palettes, in a free CLUT run
 * taken while they are out and given back after. Returns the run's first slot (shade s: + 4 s), -1 none */
#define GHOST_SLOTS 12
static int gpal_slot = -1;
static unsigned short gpal_orig[GHOST_SLOTS * 16];

int dna_ghost_palette(int on)
{
    /* added to the screen as X's (user report 2026-11-11: opaque, they looked off): STP set and the objects' translucent
     * flag (axl_moves.c yd_ghost_fn, byte 8 | 0x10 - the PS1's o[0] 0x10, its quads' ABE: additive). (Without the flag,
     * STP colours were not seen: their dark greens, added, 2026-11-09 / 11.) (7, 22, 6), (4, 14, 4), (2, 7, 2) */
    static const unsigned short green[3] = {0x9AC7, 0x91C4, 0x88E2};
    /* Heatnix's (user request 2026-11-12: his plain dash showed Yammark's green): fire orange, (24, 10, 2), (14, 6, 1),
     * (7, 3, 0) */
    static const unsigned short orange[3] = {0x8958, 0x84CE, 0x8067};
    const unsigned short *col = dna_boss == 3 ? orange : green;
    unsigned short pal[GHOST_SLOTS * 16];
    int s, i;

    if (!CLUT_OWNER)
        return -1;
    if (!on) {
        if (gpal_slot >= 0)
            CLUT_UPLOAD(CLUT_OWNER, 0, gpal_slot, gpal_orig, GHOST_SLOTS);
        gpal_slot = -1;
        return -1;
    }
    if (gpal_slot < 0) {
        s = clut_free_run(GHOST_SLOTS, dna_pal_slot, dna_pal_slot + 4);
        if (s < 0)
            return -1;
        gpal_slot = s;
        memcpy(gpal_orig, CLUT_SLOT_MEM(s), sizeof gpal_orig);
        if (test_on)
            test_log("dash afterimage palettes at slot %d", s);
    }
    for (s = 0; s < GHOST_SLOTS; s++)
        for (i = 0; i < 16; i++)
            pal[16 * s + i] = i ? col[s / 4] : 0;
    CLUT_UPLOAD(CLUT_OWNER, 0, gpal_slot, pal, GHOST_SLOTS);
    return gpal_slot;
}

/* The charge glow's colours per boss (user request 2026-11-12: "the charge effect's palette per boss, Axl's too"):
 * X's charge sparks are drawn in CLUT slot CHG_SLOT's colours CHG_FROM..CHG_TO (the blues 10-13 = X's COL CLUT 78,
 * f609 the sparks' own colour on the screen; the PS1 copy shot was X's charge shot with this slot's blue made green,
 * docs/feasibility.md; 2026-11-12 probe zz_charge_probe: slots 11 / 0x2C, the sparks' W+0x5E, did nothing). While Axl
 * or a changed boss charges, those colours are turned to the hue chg_hue[k] (saturation and brightness kept: the
 * whites stay white; chg_sv scales them), put back when the charge ends; colours 1-5 (the slot's yellows) are left. 0 = Axl. */
#define CHG_SLOT 2
#define CHG_FROM 6
#define CHG_TO 14
static const short chg_hue[9] = {
    228,   /* Axl: light navy (user request 2026-11-12; with chg_sv) */
    120,   /* Yammark: green */
    195,   /* Wolfang: ice blue */
    28,    /* Heatnix: fire orange */
    270,   /* Shark: purple */
    48,    /* Scaravich: sand yellow */
    165,   /* Turtloid: sea green */
    52,    /* Sheldon: gold */
    305,   /* Mijinion: magenta */
};
/* saturation and brightness in percent of X's (Axl's navy: paler and a little darker than X's blue) */
static const unsigned char chg_sv[9][2] = {{60, 85}, {100, 100}, {100, 100}, {100, 100}, {100, 100}, {100, 100},
                                           {100, 100}, {100, 100}, {100, 100}};
static unsigned short chg_orig[16];
static int chg_on, chg_k = -1;

static unsigned short chg_recolour(unsigned short c, int hue, int sat_pct, int val_pct)
{
    float r = (c & 31) / 31.0f, g = (c >> 5 & 31) / 31.0f, b = (c >> 10 & 31) / 31.0f;
    float mx = r > g ? (r > b ? r : b) : (g > b ? g : b), mn = r < g ? (r < b ? r : b) : (g < b ? g : b);
    float v = mx * val_pct / 100, sat = (mx > 0 ? (mx - mn) / mx : 0) * sat_pct / 100, h = hue / 60.0f, f, pp, q, t;
    int i = (int)h % 6;

    if (!(c & 0x7FFF))
        return c;                                 /* (0: transparent / black kept) */
    f = h - (int)h;
    pp = v * (1 - sat);
    q = v * (1 - sat * f);
    t = v * (1 - sat * (1 - f));
    r = i == 0 || i == 5 ? v : i == 1 ? q : i == 4 ? t : pp;
    g = i == 1 || i == 2 ? v : i == 0 ? t : i == 3 ? q : pp;
    b = i == 3 || i == 4 ? v : i == 2 ? t : i == 5 ? q : pp;
    return (unsigned short)((c & 0x8000) | (int)(r * 31 + 0.5f) | (int)(g * 31 + 0.5f) << 5 | (int)(b * 31 + 0.5f) << 10);
}

void dna_charge_palette(int on, int k)
{
    unsigned short pal[16];
    int i;

    if (!CLUT_OWNER)
        return;
    if (!on) {
        if (chg_on)
            CLUT_UPLOAD(CLUT_OWNER, 0, CHG_SLOT, chg_orig, 1);
        chg_on = 0;
        chg_k = -1;
        return;
    }
    if (!chg_on) {
        memcpy(chg_orig, CLUT_SLOT_MEM(CHG_SLOT), sizeof chg_orig);
        chg_on = 1;
    }
    if (k == chg_k)
        return;
    chg_k = k;
    for (i = 0; i < 16; i++)
        pal[i] = i >= CHG_FROM && i <= CHG_TO ?
                 chg_recolour(chg_orig[i], chg_hue[k < 9 ? k : 0], chg_sv[k < 9 ? k : 0][0], chg_sv[k < 9 ? k : 0][1]) :
                 chg_orig[i];
    CLUT_UPLOAD(CLUT_OWNER, 0, CHG_SLOT, pal, 1);
    if (test_on)
        test_log("charge palette: boss %d hue %d (slot %d)", k, chg_hue[k < 9 ? k : 0], CHG_SLOT);
}

int dna_test_nolook;   /* test (axl_test.c 'nolift look'): no look at all */
static void put_look(char *p, int pose, int tick);

void dna_look(char *p, int fire)
{
    if (dna_test_nolook)
        return;
    static int mv_prev, mv_seq, mv_t;
    const dna_look_t *L;
    const dna_seq *q;
    int t, i, pose, s = fire != 0, mv;

    if (!dna_boss || dna_pose)
        return;
    L = &dna_looks[dna_boss];
    mv = moves_look();
    if (mv && mv != mv_prev) {   /* a move starts: its look from the first picture */
        mv_t = 0;
        mv_seq = mv;
    }
    mv_prev = mv;
    /* a giga attack holding him: his attack motion at that frame (none: his shot's pose - Turtloid) */
    if ((t = dna_giga_look()) >= 0 && (L->giga.n || L->atk.n || L->fire.n)) {
        mv_seq = 0;
        q = L->giga.n ? &L->giga : L->atk.n ? &L->atk : &L->fire;   /* (his own giga motion first: Heatnix's anim 7) */
        for (i = 0; i < q->n - 1 && t >= q->dur[i]; i++)
            t -= q->dur[i];
        look_s = 2;
        put_look(p, q->pose[i], 1);
        return;
    }
    q = mv_seq == 1 ? &L->move : mv_seq == 2 ? &L->alt : mv_seq == 3 ? &L->land : mv_seq == 4 ? &L->cling :
        mv_seq == 5 ? &L->ceil : mv_seq == 6 ? &L->cshot : mv_seq == 7 ? &L->cdash : mv_seq == 8 ? &L->leap :
        mv_seq == 9 ? &L->cjump : 0;   /* (3: a move's landing; 4-9: Wolfang's walls and ceiling, moves_look) */
    if (q && q->n && (mv || (mv_seq >= 2 && mv_t < seq_len(q)))) {
        /* the move's look (user request 2026-10-27): the dash move loops its pictures while it lasts, the second
         * attack plays its once (to the end, then holds the last while the attack goes on) */
        t = mv_t;
        if (mv_seq <= 2 && q->at != DNA_HOLD) {   /* loops from entry at (DNA_HOLD: plays once, the last held) */
            int pre = 0;
            for (i = 0; i < q->at && i < q->n; i++)
                pre += q->dur[i];
            if (t >= pre && seq_len(q) > pre)
                t = pre + (t - pre) % (seq_len(q) - pre);
        }
        for (i = 0; i < q->n - 1 && t >= q->dur[i]; i++)
            t -= q->dur[i];
        pose = q->pose[i];
        mv_t++;
        look_s = 3;
    } else if (dna_toss_t >= 0 && L->toss.n) {   /* a toss (Sheldon's shell): once, the last held */
        mv_seq = 0;
        q = &L->toss;
        for (t = dna_toss_t, i = 0; i < q->n - 1 && t >= q->dur[i]; i++)
            t -= q->dur[i];
        pose = q->pose[i];
        look_s = 2;
    } else if (atk_cur >= 0) {
        mv_seq = 0;
        q = &L->atk;
        for (t = atk_cur, i = 0; i < q->n - 1 && t >= q->dur[i]; i++)
            t -= q->dur[i];
        pose = q->pose[i];
        look_s = 2;
    } else {
        if (!L->fire.n)
            s = 0;
        q = s ? &L->fire : 0;
        if (!q)
            q = motion_seq(p, L, &s);
        if (!q)
            q = (dna_alt || dna_shell_away) && L->idle2.n ? &L->idle2 : &L->idle;
        if (!q->n)
            return;
        if (s != look_s) {
            look_s = (u8)s;
            look_i = look_d = 0;
        } else if (++look_d >= q->dur[look_i]) {
            look_d = 0;
            if (++look_i >= q->n)   /* at = the entry it loops back to, DNA_HOLD = hold the last (the boss's script) */
                look_i = q->at == DNA_HOLD ? q->n - 1 : q->at;
        }
        pose = q->pose[look_i];
    }
    put_look(p, pose, 1);
}

/* the look again with the picture drawn last, nothing moved on (axl_x.c axl_look_late): the frames X main does not run
 * - the DNA menu's pause and the game's own (Space) - drew Axl, the change seemed over (user report 2026-10-28) */
void dna_relook(char *p)
{
    if (!p || !dna_boss || dna_pose || look_pose < 0 || dna_test_nolook || O8(p, 0x0C) != 1)
        return;
    put_look(p, look_pose, 0);
}

/* the boss's picture pose on the player for the draw (tick: the hurt blink moves on) */
static void put_look(char *p, int pose, int tick)
{
    const dna_look_t *L = &dna_looks[dna_boss];
    int i;

    if (O16(p, 0x5E) != dna_pal_slot)
        axl_pal = O16(p, 0x5E);   /* Axl's own palette slot */
    dna_unlook();   /* a look still on (no frame start between two draws, flying 2026-10-27): its saved fields are
                     * Axl's, not the boss's (else the end of the change brought the boss's palette back) */
    saved.p = p;
    saved.tex = O32(p, 0x54);
    saved.pat = O32(p, 0x58);
    saved.pal = O16(p, 0x5E);
    saved.frame = O8(p, 0x66);
    saved.x = *(volatile float *)(p + 0x14);
    saved.y = *(volatile float *)(p + 0x1C);
    saved.on = 1;
    O32(p, 0x54) = L->tex;
    O32(p, 0x58) = L->pat;
    O16(p, 0x5E) = (unsigned short)dna_pal_slot;   /* the changed boss's palettes, uploaded every frame as PS1 queued them */
    boss_palettes(dna_boss);
    if (leave_t & 4) {   /* the leaving blink: the boss white (the pose's pattern: 23-20, 15-12, 7-4) */
        for (i = 0; i < 4; i++)
            if (CLUT_OWNER)
                CLUT_UPLOAD(CLUT_OWNER, 0, dna_pal_slot + i, flash_pal, 1);
    } else if (hurt_t) {   /* hit while changed: the boss blinks in the sheet's "Hurt Flash" light blue (user request) */
        if (tick)
            hurt_t--;
        if (hurt_t & 4)
            hurt_palettes(dna_boss);
    }
    O8(p, 0x66) = (u8)pose;
    look_pose = pose;
    /* (drawn at whole pixels, user report 2026-11-12: flying, a fractional x showed the background through a seam
     * between two 16 px parts of Heatnix's picture - the screen is drawn scaled up; the bosses' own positions are
     * whole pixels) */
    if (!dna_test_nolift)   /* (test 'nolift': the lift's move left out, to see what it does to the terrain) */
        *(volatile float *)(p + 0x1C) = floorf(saved.y - L->lift[pose] + 0.5f);
    else
        *(volatile float *)(p + 0x1C) = floorf(saved.y + 0.5f);
    /* the push in the boss's own facing: its pictures face left, mirrored when Axl faces right */
    *(volatile float *)(p + 0x14) = floorf(saved.x + ((O8(p, 0x29) & 0x40) ? -L->push[pose] : L->push[pose]) + 0.5f);
}

/* The weapon HUD's 3-digit number (user request 2026-10-28: the change's time left): X's HUD (FUN_023d82d0) shows a
 * boss weapon's energy / its shot cost there; for Axl's boss weapons it is the change's seconds left instead (the
 * gauge's drain, DRAIN_FRAMES for a full one; White Axl does not drain: the game's number). Hooked at 0x023D83AE
 * (mov ecx, 100 right after the idiv: EAX = the number, EBP = B = P + 0xBC). */
#define HUD_NUMBER 0x023D83AEu

int __cdecl dna_hud_number(char *p, int n)
{
    int k = O8(p, 0x14C);   /* (the weapon; B + 0x91 the HUD divides by is its shot id, not always the same) */
    int e, m = ENERGY_MAX(p);

    if (p != axl_p || k < 1 || k > 8 || m <= 0 || armor() == 4)
        return n;
    e = ENERGY(p, k) > 0 ? ENERGY(p, k) : 0;
    return (int)(((long long)e * (drain_frames(p) ? drain_frames(p) : DRAIN_FRAMES) / m + 59) / 60);   /* (Weapon
        * Drive: the seconds stand still) */
}

int dna_hud_install(void)
{
    static const unsigned char site[5] = {0xB9, 0x64, 0x00, 0x00, 0x00};
    unsigned char *t, b[5];
    DWORD old;

    if (memcmp((void *)HUD_NUMBER, site, 5))
        return 0;
    t = code_alloc(32);
    if (!t)
        return 0;
    /* push eax; lea edx, [ebp-0xBC]; push edx; call dna_hud_number; add esp, 8; mov ecx, 100; jmp back */
    static const unsigned char code[] = {0x50, 0x8D, 0x95, 0x44, 0xFF, 0xFF, 0xFF, 0x52, 0xE8, 0, 0, 0, 0,
                                         0x83, 0xC4, 0x08, 0xB9, 0x64, 0x00, 0x00, 0x00, 0xE9, 0, 0, 0, 0};
    memcpy(t, code, sizeof code);
    *(LONG *)(t + 9) = (LONG)((DWORD)dna_hud_number - ((DWORD)t + 13));
    *(LONG *)(t + 22) = (LONG)((HUD_NUMBER + 5) - ((DWORD)t + 26));
    code_seal(t, 32);
    b[0] = 0xE9;
    *(LONG *)(b + 1) = (LONG)((DWORD)t - (HUD_NUMBER + 5));
    if (!VirtualProtect((void *)HUD_NUMBER, 5, PAGE_EXECUTE_READWRITE, &old))
        return 0;
    memcpy((void *)HUD_NUMBER, b, 5);
    VirtualProtect((void *)HUD_NUMBER, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void *)HUD_NUMBER, 5);
    return 1;
}

/* The pause menu's giga row (row 9: energy[0], the giga gauge) as with an armor on (user request 2026-10-28: the Giga
 * Crush worked with no gauge shown anywhere; the ring stays without it): the menu's row set-up (caseD_4 at 0x023E9E70)
 * hides it when the armor B+0x199 is 0 - that jump (0x023E9F78 je) taken out. Picking the row sets weapon 0 (the
 * buster), as for the armors. */
#define GIGA_ROW_JE 0x023E9F78u

int dna_giga_row_install(void)
{
    static const unsigned char je[2] = {0x74, 0x4B}, nop[2] = {0x90, 0x90};
    DWORD old;

    if (memcmp((void *)GIGA_ROW_JE, je, 2))
        return 0;
    if (!VirtualProtect((void *)GIGA_ROW_JE, 2, PAGE_EXECUTE_READWRITE, &old))
        return 0;
    memcpy((void *)GIGA_ROW_JE, nop, 2);
    VirtualProtect((void *)GIGA_ROW_JE, 2, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void *)GIGA_ROW_JE, 2);
    return 1;
}

/* Rematch on a red panel (user decision 2026-10-28: a missed DNA is lost, except that a beaten Maverick fights again
 * when its stage's panel is red - Nightmare - on the stage select; not Gate's / Sigma's stages). The red stages are
 * st + 0x4A1..0x4A3 (three stage numbers), rewritten with the entered stage's own targets by the stage select's
 * sub-state 8 (0x023DD8A0, table slot 0x3481818) as the stage is entered: whether the entered stage was red is read in
 * a wrapper of it and kept (deaths and retries keep it; the next stage select uses it up, as the game's red does).
 * The boss-room trigger (0x02494610, state 0 of the object table 0x3486060: beaten -> stage end, else the fight)
 * reads the beaten bits with g8_9c (call at 0x02494669): that call is ours, the stage's bit hidden while red (as the
 * PS1 axl_rematch; the bits themselves never change). */
#define SUB8_SLOT 0x03481818u
#define SUB8_ORIG 0x023DD8A0u
#define TRIGGER_BEATEN_CALL 0x02494669u
typedef void(__fastcall *sub8_t)(void *scene, void *edx);
static int rematch_red;

static void __fastcall sub8_hook(void *scene, void *edx)
{
    char *g = *(char **)0x041D8C8C, *st = g ? g + 0x70 : 0;
    if (st) {
        int s = (unsigned char)st[0x48];
        rematch_red = s >= 1 && s <= 8 && (st[0x4A1] == s || st[0x4A2] == s || st[0x4A3] == s);
        if (test_on)
            test_log("stage %d entered: red %d (beaten %02x, red list %d %d %d)", s, rematch_red,
                     (unsigned char)st[0x9C], st[0x4A1], st[0x4A2], st[0x4A3]);
    }
    ((sub8_t)SUB8_ORIG)(scene, edx);
}

static unsigned char __fastcall trigger_beaten(char *st, void *edx)
{
    unsigned char b = (unsigned char)st[0x9C];
    int s = (unsigned char)st[0x48];
    (void)edx;
    if (rematch_red && s >= 1 && s <= 8 && (b >> (s - 1) & 1)) {
        b &= (unsigned char)~(1u << (s - 1));
        /* no dialogue on the rematch (user request 2026-10-29): the stage's "boss talk seen" flag (st + 0x6A, the
         * one X6 sets after the first talk - a retry after dying skips it too); every Maverick calls its talk with
         * it as the skip argument (FUN_023b4b20: a 60 frame wait instead) */
        st[0x6A] = 1;
        if (test_on)
            test_log("boss room of stage %d: red panel, rematch (no talk)", s);
    }
    return b;
}

int dna_rematch_install(void)
{
    static const unsigned char call[5] = {0xE8, 0xD2, 0x26, 0xF2, 0xFF};   /* call 0x023B6D40 (g8_9c) */
    unsigned char b[5];
    DWORD old;
    void *fn = (void *)sub8_hook;

    if (memcmp((void *)TRIGGER_BEATEN_CALL, call, 5) || *(DWORD *)SUB8_SLOT != SUB8_ORIG)
        return 0;
    b[0] = 0xE8;
    *(LONG *)(b + 1) = (LONG)((DWORD)trigger_beaten - (TRIGGER_BEATEN_CALL + 5));
    if (!VirtualProtect((void *)TRIGGER_BEATEN_CALL, 5, PAGE_EXECUTE_READWRITE, &old))
        return 0;
    memcpy((void *)TRIGGER_BEATEN_CALL, b, 5);
    VirtualProtect((void *)TRIGGER_BEATEN_CALL, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void *)TRIGGER_BEATEN_CALL, 5);
    if (!VirtualProtect((void *)SUB8_SLOT, 4, PAGE_READWRITE, &old))
        return 0;
    memcpy((void *)SUB8_SLOT, &fn, 4);
    VirtualProtect((void *)SUB8_SLOT, 4, old, &old);
    return 1;
}

/* PLAYER SELECT starts on Axl (user request 2026-10-29: it started on the armour - LEFT for AXL): the stage select's
 * sub-state 4 (0x023DD2A0) sets the cursor (st + 0x6C) from its list's entry 1 (movzx eax, byte [edi+0x512]); entry
 * 0 is always normal X (= Axl): read 0x511 instead. Picking, left / right and the armour written at the pick
 * (st + 0x9A) are the game's own. */
#define PSEL_CURSOR 0x023DD2A4u

int dna_player_select_install(void)
{
    static const unsigned char want[7] = {0x0F, 0xB6, 0x87, 0x12, 0x05, 0x00, 0x00};
    unsigned char b = 0x11;
    DWORD old;

    if (memcmp((void *)PSEL_CURSOR, want, 7))
        return 0;
    if (!VirtualProtect((void *)(PSEL_CURSOR + 3), 1, PAGE_EXECUTE_READWRITE, &old))
        return 0;
    *(volatile unsigned char *)(PSEL_CURSOR + 3) = b;
    VirtualProtect((void *)(PSEL_CURSOR + 3), 1, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void *)(PSEL_CURSOR + 3), 1);
    return 1;
}
