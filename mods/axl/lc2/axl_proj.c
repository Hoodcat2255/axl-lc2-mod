/* Axl's own projectiles on PC (port of PS1 mods/axl/src/axl_proj.c), for the DNA Change bosses' attacks.
 *
 * A projectile is X's plain buster shot (id 0, made by X's shot spawner FUN_023d4a50) taken over by our code:
 * the id 0 class runs its state functions from the table 0x03482314 ([0] set-up 0x023F4070, [1] flight 0x023F4A20,
 * [2] bounced off, [3] free: owner's on-screen count B+0x95 - 1). Entries 0 and 1 are replaced: for our shots the
 * set-up is followed by our own settings (position, speed, damage id W+9, boxes, picture) and the flight is
 * proj_update (behaviour, move, terrain, life, off screen, hits), as PS1 proj_update; other shots run the game's.
 *
 * Work W (task + 0xC0; laid out as the player's P): x / y floats W+0x14 / W+0x1C (pixels), speed 16.16 W+0x3C /
 * W+0x40 (vy > 0 = up), facing W+0x29, anim W+0x2B, anim table W+0x4C, texture W+0x54, parts section W+0x58,
 * palette slot W+0x5E, draw depth W+0x60 (float: X's shot 2.0, the player 12.0), attack box W+0x78 and terrain box W+0xA8 (8-byte box objects: vtable 0x031DA67C, then the
 * PS1 {dx, dy, w, h}), terrain bits W+0xB0 (1 / 2 wall, 4 ceiling, 8 floor), hit result W+0xD4 (> 0 hit,
 * < 0 blocked), owner P W+0xC0.
 * Enemies and bosses test their hurt boxes against the weapon objects and take damage by the damage id from their
 * own tables, so the bosses' weaknesses apply as for X's weapon of that number.
 */
#include <windows.h>

#include "axl_lc2.h"
#include "axl_proj.h"
#include "axl_data.h"      /* the game-derived tables (axl_data.bin) */
#include "axl_test.h"

#define O8(p, o) (*(volatile unsigned char *)((p) + (o)))
#define O32(p, o) (*(volatile int *)((p) + (o)))

#define SHOT_STATES ((void **)0x03482314)
#define SETUP_ORIG 0x023F4070u
#define FLY_ORIG 0x023F4A20u
#define BOX_VT ((const void *)0x031DA67C)
#define SCREEN (*(char **)0x041D8E58)
#define MAP (*(char **)0x041D9124)
#define REHIT 20   /* frames between two hits of a lasting projectile on one enemy */
#define OWNER_COUNT(p) O8(p, 0xBC + 0x95)

typedef void(__fastcall *task_fn)(char *task);
typedef void(__stdcall *obj_fn)(char *w);
typedef int(__fastcall *screen_fn)(char *scr, void *edx, char *w, int a, int b);
typedef void(__fastcall *terrain_fn)(char *map, void *edx, char *w);
#define MOVE ((obj_fn)0x023C14A0)           /* x += vx, y -= vy (PS1 FUN_8002d2b0) */
#define ANIM_STEP ((obj_fn)0x023C1400)      /* PS1 FUN_80017a04 */
#define OFF_SCREEN ((screen_fn)0x023BC5D0)  /* PS1 FUN_8002cbfc: 1 = out by more than (a, b) */
#define SET_VISIBLE ((screen_fn)0x023BBAD0) /* PS1 FUN_8002ccb0 */
#define TERRAIN ((terrain_fn)0x023CEF30)    /* PS1 FUN_8002ee10: terrain box W+0xA8 -> bits W+0xB0 (when 0) */
#define KILL ((task_fn)0x023F3BE0)          /* state 3, boxes off */
#define WALL_SPARK ((task_fn)0x02538ED0)    /* the buster's wall hit effect */
typedef void(__fastcall *set_anim_t)(char *ecx, void *edx, char *obj, int anim);
#define SET_ANIM ((set_anim_t)0x023C1730)
typedef void(__fastcall *spawn_t)(char *, void *, int, int, int, char *);
#define SPAWN_SHOT ((spawn_t)0x023D4A50)

typedef struct { const void *vt; signed char b[4]; } box_t;
#define N_BOXES 32
static box_t boxes[N_BOXES];
static const signed char *box_src[N_BOXES];

static proj projs[N_PROJ];
int proj_tex = -1, proj_pat = -1, proj_anim = -1;   /* test: picture overrides (axl_test.c projlook) */
static unsigned frame_now;
static proj *spawning;   /* set while our spawn call runs: axl_spawned hands the new work to it */

proj *proj_of(char *w)
{
    int i;
    for (i = 0; i < N_PROJ; i++)
        if (projs[i].w == w)
            return &projs[i];
    return 0;
}

/* the 8-byte box object of a PS1 box array (made once per array) */
static box_t *box_obj(const signed char *b)
{
    int i;
    for (i = 0; i < N_BOXES && box_src[i]; i++)
        if (box_src[i] == b)
            return &boxes[i];
    if (i == N_BOXES)
        return 0;
    box_src[i] = b;
    boxes[i].vt = BOX_VT;
    memcpy(boxes[i].b, b, 4);
    return &boxes[i];
}

static void apply_box(proj *q)
{
    box_t *b = q->box ? box_obj(q->box) : 0;
    *(void *volatile *)(q->w + 0x78) = q->nobox ? 0 : b;   /* attack */
    *(void *volatile *)(q->w + 0xA8) = b;                  /* terrain */
}

/* our own anim table for FX pictures: anim f holds FX_PAT frame f (the player table is full: anims are a byte, X's
 * and Axl's take 0-255) */
typedef struct { const void *vt; unsigned char dur, flag; signed char step; unsigned char frame; } fx_ent;
static fx_ent fx_ents[256];
static void *fx_table[256];

static void fx_table_init(void)
{
    int f;
    if (fx_table[0])
        return;
    for (f = 0; f < 256; f++) {
        fx_ents[f].vt = (const void *)0x031DE36C;   /* (AXL_ENT_VT) */
        fx_ents[f].dur = 96;
        fx_ents[f].frame = (unsigned char)f;
        fx_table[f] = &fx_ents[f];
    }
}

static void apply_look(proj *q)
{
    char *w = q->w;

    if (!q->anim && !q->tex)   /* (a pose look may be picture 0) */
        return;
    if (test_on)
        test_projlook(&proj_tex, &proj_pat, &proj_anim);
    fx_table_init();
    P_TABLE(w) = q->fx ? fx_table : axl_table;
    O32(w, 0x54) = proj_tex >= 0 ? proj_tex : q->tex ? q->tex : FX_TEX_INDEX;
    O32(w, 0x58) = proj_pat >= 0 ? proj_pat : q->tex ? q->pat : FX_PAT;
    *(volatile unsigned short *)(w + 0x5E) = (unsigned short)dna_pal_slot;
    SET_ANIM(w, 0, w, proj_anim >= 0 ? proj_anim : q->anim);
    *(volatile float *)(w + 0x60) = 2.0f;   /* draw depth as X's shot (1.0 = behind the stage) */
}

void proj_box(char *w, const signed char *box)
{
    proj *q = proj_of(w);
    if (!q)
        return;
    q->box = box;
    q->nobox = 0;
    if (q->live)
        apply_box(q);
}

void proj_nobox(char *w)
{
    proj *q = proj_of(w);
    if (!q)
        return;
    q->nobox = 1;
    if (q->live)
        *(void *volatile *)(w + 0x78) = 0;
}

void proj_boss_look(char *w, int anim)
{
    proj *q = proj_of(w);
    if (!q)
        return;
    q->anim = (unsigned char)anim;
    q->fx = 0;
    q->tex = 0;
    if (q->live)
        apply_look(q);
}

void proj_fx_look(char *w, int frame)
{
    proj *q = proj_of(w);
    if (!q || (q->fx && !q->tex && q->anim == frame && q->live))
        return;
    q->fx = 1;
    q->tex = 0;
    q->anim = (unsigned char)frame;
    if (q->live)
        apply_look(q);
}

void proj_pose_look(char *w, int tex, int pat, int frame)
{
    proj *q = proj_of(w);
    if (!q)
        return;
    q->tex = tex;
    q->pat = pat;
    q->fx = 1;
    q->anim = (unsigned char)frame;
    if (q->live)
        apply_look(q);
}

void proj_set_anim(char *w, int anim)
{
    SET_ANIM(w, 0, w, anim);
}

void proj_kill(char *w)
{
    proj *q = proj_of(w);

    if (q && q->live && q->task)
        KILL(q->task);
    if (q)
        memset(q, 0, sizeof *q);
}

/* the change ended (axl_dna.c dna_end, user request 2026-10-28): every projectile of a boss attack (id = the boss,
 * 1-8, or a boss giga's P_GIGA; Axl's own are 0 and the Giga Crush's 0x16) killed with it - its look went with the change, its hit stayed */
/* test (axl_test.c dna_shots): live boss-attack projectiles */
int proj_live_dna(void)
{
    int i, n = 0;
    for (i = 0; i < N_PROJ; i++)
        n += projs[i].w && ((projs[i].id >= 1 && projs[i].id <= 8) || (projs[i].flags & P_GIGA));
    return n;
}

void proj_kill_dna(void)
{
    int i, n = 0;
    for (i = 0; i < N_PROJ; i++)
        if (projs[i].w && ((projs[i].id >= 1 && projs[i].id <= 8) || (projs[i].flags & P_GIGA))) {
            proj_kill(projs[i].w);
            n++;
        }
    if (test_on && n)
        test_log("proj kill dna: %d", n);
}

void proj_spark(char *w)
{
    proj *q = proj_of(w);
    if (q && q->task)
        WALL_SPARK(q->task);
}

/* X's shot spawner made w: during proj_spawn it is ours */
int proj_spawned(char *w)
{
    if (!spawning)
        return 0;
    spawning->w = w;
    spawning = 0;
    return 1;
}

static char *proj_flash[4];
static int proj_flash_n, in_spawn;   /* (in_spawn: X's shot spawner is running for us) */

/* axl_x.c axl_allocated: an effect made; X's muzzle flash (pool 6, 2 / 16) made inside proj_spawn is noted */
void proj_allocated(char *o, int pool, int a, int b)
{
    if (in_spawn && pool == 6 && a == 2 && b == 16 && proj_flash_n < 4)
        proj_flash[proj_flash_n++] = o;
}

int proj_made;   /* our projectiles made so far (axl_test.c: shots) */

char *proj_spawn(int id, float x, float y, int vx, int vy, int right, int life, proj_fn fn, int flags)
{
    char *self = axl_self, *p = axl_p;
    proj *q = 0;
    int i;
    unsigned char req;

    if (test_on)
        test_log("proj spawn id %d self %p p %p", id, self, p);
    if (!self || !p)
        return 0;
    for (i = 0; i < N_PROJ; i++)
        if (!projs[i].w) {
            q = &projs[i];
            break;
        }
    if (!q)
        return 0;
    memset(q, 0, sizeof *q);
    q->id = (unsigned char)id;
    q->x = x;
    q->y = y;
    q->vx = vx;
    q->vy = vy;
    q->right = right != 0;
    q->life = life;
    q->fn = fn;
    q->flags = flags;
    q->seen = frame_now;
    spawning = q;
    req = O8(p, 0x14F);          /* B+0x93: the shot X's fire code asked for, read by the set-up */
    O8(p, 0x14F) = 0;
    objs_mute = 1;               /* (no X muzzle flash sound for our shots) */
    proj_flash_n = 0;
    in_spawn = 1;
    SPAWN_SHOT(self, 0, 1, 0, 0, p);
    in_spawn = 0;
    objs_mute = 0;
    while (proj_flash_n)   /* X's muzzle flash that X's shot spawner made with it: freed (the pool frees by
                            * clearing +8; it showed at X's hand, Axl's hips - user report 2026-10-27) */
        *(volatile unsigned char *)(proj_flash[--proj_flash_n] + 8) = 0;
    O8(p, 0x14F) = req;
    spawning = 0;
    if (test_on)
        test_log("proj spawn id %d -> %p", id, q->w);
    if (q->w)
        proj_made++;   /* (test: the scenarios' 'shots') */
    if (!q->w) {
        memset(q, 0, sizeof *q);
        return 0;
    }
    return q->w;
}

/* state 0: the game's set-up, then ours */
static void __fastcall setup_hook(char *task)
{
    char *w = *(char **)(task + 0xC0);
    proj *q;

    static int n_logged;
    q = proj_of(w);
    objs_mute = q && !q->live;
    ((task_fn)SETUP_ORIG)(task);
    objs_mute = 0;
    if (!q)
        axl_shot_setup(w);   /* (Axl's guard-break bullet gets its damage id now) */
    if (test_on && n_logged < 4) {
        n_logged++;
        test_log("shot set-up hook: task %p w %p ours %d", task, w, q != 0);
    }
    if (!q || q->live)
        return;
    q->task = task;
    q->live = 1;
    q->seen = frame_now;
    *(volatile float *)(w + 0x14) = q->x;
    *(volatile float *)(w + 0x1C) = q->y;
    O32(w, 0x3C) = q->vx;
    O32(w, 0x40) = q->vy;
    O8(w, 0x29) = q->right ? 0x40 : 0;
    O8(w, 9) = q->id;
    if (q->box)
        apply_box(q);
    apply_look(q);
    OWNER_COUNT(*(char **)(w + 0xC0))++;   /* X's fire code counts its shots; state 3 takes ours off again */
    if (test_on)
        test_log("proj set up w %p id %d xy %.1f %.1f v %x %x anim %d", w, q->id, q->x, q->y, q->vx, q->vy, q->anim);
}

/* state 1: PS1 proj_update */
static void proj_update(char *task, char *w, proj *q)
{
    signed char hit = *(volatile signed char *)(w + 0xD4);

    q->task = task;
    q->seen = frame_now;
    if (hit) {
        if (!(q->flags & (P_PIERCE | P_TOUGH)) || (hit < 0 && !(q->flags & P_TOUGH))) {
            proj_kill(w);
            return;
        }
        *(volatile signed char *)(w + 0xD4) = 0;
    }
    q->t++;
    if (q->flags & P_HUD)
        return;   /* (axl_hud.c places it, picks its frame and shows it after the unit pass) */
    /* the game's own rule against multi-hits (PS1 FUN_800315cc): a weapon whose hit number (PS1 +0x64 = W+0x8C) is
     * the one the target was last hit by (its +0x65) does no damage. A piercing projectile keeps one number: each
     * enemy once (user request 2026-10-27); a lasting one (P_TOUGH: options, the dash body) takes a new number
     * every REHIT frames: the same enemy again after that. */
    if (q->flags & (P_PIERCE | P_TOUGH)) {
        if (!q->hitno || ((q->flags & P_TOUGH) && q->t % REHIT == 0)) {
            static unsigned char next;
            next = next == 255 ? 1 : next + 1;
            q->hitno = next;
        }
        O8(w, 0x8C) = q->hitno;
    }
    if (test_on && q->t == 5) {
        char hex[0xE0 * 2 + 1];
        int k;
        for (k = 0; k < 0xE0; k++)
            wsprintfA(hex + 2 * k, "%02x", (unsigned char)w[k]);
        test_log("proj w %p t 5 %s", w, hex);
    }
    if ((q->life && q->t >= q->life) || (!(q->flags & P_NOSCREEN) && OFF_SCREEN(SCREEN, 0, w, 0x20, 0x20))) {
        proj_kill(w);
        return;
    }
    if (q->fn) {
        q->fn(w, q);
        if (q->w != w)
            return;                            /* the behaviour freed it */
    }
    MOVE(w);
    if (!(q->flags & P_WALL) && *(void **)(w + 0xA8)) {
        O8(w, 0xB0) = 0;
        TERRAIN(MAP, 0, w);
        if (O8(w, 0xB0) && !(q->flags & P_KEEP)) {
            WALL_SPARK(task);
            proj_kill(w);
            return;
        }
    } else
        O8(w, 0xB0) = 0;
    ANIM_STEP(w);
    if (q->nobox && *(void *volatile *)(w + 0x78)) {   /* (no attack box: the anim step had put one back - the hover
        * flames hit, many times a second, user report 2026-10-29) */
        if (test_on) {
            static int n;
            if (n++ < 20)
                test_log("proj %p id %d anim %d: attack box %p back after the anim step, cleared", w, q->id, q->anim,
                         *(void **)(w + 0x78));
        }
        *(void *volatile *)(w + 0x78) = 0;
    }
    if (q->flags & P_HIDDEN)
        O8(w, 0x0B) = 0;
    else
        SET_VISIBLE(SCREEN, 0, w, 0x10, 0x10);
}

/* the terrain test on any object with a terrain box (W+0xA8; the player's too); its own bits are kept */
/* our live projectiles: each one counts in X's own shots (OWNER_COUNT, set-up hook) */
int proj_live_count(void)
{
    int i, n = 0;
    for (i = 0; i < N_PROJ; i++)
        if (projs[i].w && projs[i].live)
            n++;
    return n;
}

int proj_terrain(char *w)
{
    unsigned char keep = O8(w, 0xB0), bits;
    if (!*(void **)(w + 0xA8) || !MAP)
        return 0;
    O8(w, 0xB0) = 0;
    TERRAIN(MAP, 0, w);
    bits = O8(w, 0xB0);
    O8(w, 0xB0) = keep;
    return bits;
}

static void __fastcall fly_hook(char *task)
{
    char *w = *(char **)(task + 0xC0);
    proj *q = proj_of(w);

    if (q && q->live)
        proj_update(task, w, q);
    else if ((O8(w, 9) == GUARD_ID || O8(w, 9) == GUARD_STUN_ID || O8(w, 9) == GUARD_WEAK_ID) && O8(w, 0x5E) == GUARD_SLOT &&
             *(char **)(w + 0xC0) == axl_p) {   /* Axl's guard-break bullet */
        float x = *(volatile float *)(w + 0x14), y = *(volatile float *)(w + 0x1C);
        /* High Max (user request 2026-10-27): immune until a full charge (id 0x12) stuns him (0 damage, his table
         * PS1 0x8007523C), then saber ids hurt him: near him unstunned the bullet carries X's full-charge id */
        char *nz = objs_table_near(x, y, 48, NZERO_TABLE);
        objs_iframe_open(x, y, 64);   /* (it ignores the hit immunity: user request 2026-10-27) */
        O8(w, 9) = objs_table_near(x, y, 48, HIGHMAX_TABLE) ? GUARD_STUN_ID : GUARD_ID;
        /* Nightmare Zero (user request 2026-10-27): weak to Zero's saber, but his table gives the guard-break id 0x18
         * only 2: near him the bullet carries an id his table takes 8 from */
        if (nz && O8(nz, 9) == NZERO_TYPE)
            O8(w, 9) = GUARD_WEAK_ID;
        if ((*(volatile unsigned *)(w + 0xC4) || *(volatile signed char *)(w + 0xD4)) &&
            (O8(w, 9) == GUARD_STUN_ID || objs_hurt_near(x, y, 48)))
            KILL(task);   /* a saber id's touch leaves W+0xD4 alone and sets W+0xC4 (0 when made), also when it
                           * passes a saber, and X's code lets the shot fly on, hitting every frame: the bullet passes
                           * sabers and shields but ends at the first enemy it hurts (or stuns), as Axl's others */
        else
            ((task_fn)FLY_ORIG)(task);
    }
    else
        ((task_fn)FLY_ORIG)(task);
}

/* once per player frame: entries whose object no longer runs (freed by the game, stage change) are dropped */
void proj_frame(void)
{
    int i;

    frame_now++;
    if (test_on && (frame_now == 1 || frame_now % 600 == 0))
        test_log("shot states now %p %p (ours %p %p)", SHOT_STATES[0], SHOT_STATES[1], (void *)setup_hook,
                 (void *)fly_hook);
    for (i = 0; i < N_PROJ; i++)
        if (projs[i].w && frame_now - projs[i].seen > 3)
            memset(&projs[i], 0, sizeof projs[i]);
}

int proj_install(void)
{
    DWORD prot;

    if (SHOT_STATES[0] != (void *)SETUP_ORIG || SHOT_STATES[1] != (void *)FLY_ORIG)
        return 0;
    if (!VirtualProtect(SHOT_STATES, 8, PAGE_READWRITE, &prot))
        return 0;
    SHOT_STATES[0] = (void *)setup_hook;
    SHOT_STATES[1] = (void *)fly_hook;
    VirtualProtect(SHOT_STATES, 8, prot, &prot);
    return 1;
}
