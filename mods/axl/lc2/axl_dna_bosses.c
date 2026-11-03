/* The 8 bosses' own attacks for DNA Change on PC: port of PS1 mods/axl/src/axl_dna_bosses.c (constants from the
 * boss code, docs/boss_attacks.md, boss_attack_anims.md) on Axl's projectiles (axl_proj.c). Index = X's weapon
 * number (1 Yammark .. 8 Mijinion); each attack uses that weapon's damage id, so the enemies' and bosses' weakness
 * tables apply as for X's weapon. Positions are the game's float pixels (y down), speeds 16.16 (vy > 0 = up).
 */
#include <windows.h>
#include <math.h>

#include "axl_lc2.h"
#include "axl_proj.h"
#include "axl_data.h"      /* the game-derived tables (axl_data.bin) */
#include "axl_test.h"   /* FX_*: the bosses' projectile anims in Axl's table */

#define FXP(n) ((int)((n) * 65536))
#define PX(p) (*(volatile float *)((p) + 0x14))
#define PY(p) (*(volatile float *)((p) + 0x1C))
#define VX(w) (*(volatile int *)((w) + 0x3C))
#define VY(w) (*(volatile int *)((w) + 0x40))
#define TERR(w) (*(volatile unsigned char *)((w) + 0xB0))
#define FACE(p) ((*(volatile unsigned char *)((p) + 0x29) & 0x40) != 0)
#define SX(p) (FACE(p) ? 1 : -1)
#define CD(n) (cooldown = (n))

static unsigned short cooldown;
static unsigned rng = 0x12345;

static int rnd(void)
{
    rng = rng * 1103515245u + 12345u;
    return (int)(rng >> 16 & 0x7fff);
}

void dna_cool_reset(void)
{
    cooldown = 0;
}

int dna_cool_left(void)
{
    return cooldown;
}

/* gun tip in front of Axl (ground forward aim, PS1 tip_ground) */
static float gun_x(char *p) { return PX(p) + SX(p) * 32; }
static float gun_y(char *p) { return PY(p) - 13; }

/* target point: the nearest enemy within range, else `ahead` px in front at the gun height */
static void aim_point(char *p, int range, int ahead, float *tx, float *ty)
{
    char *e = find_target(gun_x(p), gun_y(p), range);

    if (e) {
        *tx = PX(e);
        *ty = PY(e);
    } else {
        *tx = gun_x(p) + SX(p) * ahead;
        *ty = gun_y(p);
    }
}

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

static int dead_dna(char *w, int k)
{
    if (dna_boss != k) {
        proj_kill(w);
        return 1;
    }
    return 0;
}

/* ---- 1 Commander Yammark: dragonfly options (boss sub-states 5/7 orbit fire, 9 train + bombs) ---------- */
#define Y_OPTS 3
#define Y_HIST 128
static const signed char box_opt[4] = {-16, -6, 32, 12};
static const signed char box_shot[4] = {-7, -7, 14, 14};
static const signed char box_bomb[4] = {-2, -6, 4, 12};
static const signed char box_blast[4] = {-8, -8, 16, 16};
static const unsigned char y_period[Y_OPTS] = {30, 25, 45};
#define Y_HIT_GAP 20   /* frames between an option's body hits (its box on for 2 of them) */   /* 0x80100B68: per-slot fire periods */
static char *y_opt[Y_OPTS];
static short y_hx[Y_HIST], y_hy[Y_HIST];
static unsigned char y_head;

static void y_bomb(char *w, proj *q)
{
    int vy = VY(w) - 0x1000;                      /* vy -1 px/f, -1/16 px/f per frame, at most -3 */
    VY(w) = vy < FXP(-3) ? FXP(-3) : vy;
    if (TERR(w) & 8) {                            /* floor: 3 blasts at (+10,-8) (-16,-4) (+4,-12) */
        static const signed char off[3][2] = {{10, -8}, {-16, -4}, {4, -12}};
        int i;
        char *b;
        for (i = 0; i < 3; i++) {
            b = proj_spawn(1, PX(w) + off[i][0], PY(w) + off[i][1], 0, 0, 1, 30, 0, P_PIERCE | P_WALL | P_NOSCREEN);
            if (b) {
                proj_box(b, box_blast);
                proj_boss_look(b, FX_BOOM);
            }
        }
        proj_kill(w);
    }
}

static void y_option(char *w, proj *q)
{
    char *p = axl_p, *e;
    int i = q->a, ang, h, d;
    float tx, ty;

    if (dead_dna(w, 1))
        return;
    q->life = q->t + 2;                           /* lives while the DNA lasts */
    /* their body hits (user request 2026-10-29: the orbiting ones too - the boss's have no box there - but with a
     * gap, a hit every Y_HIT_GAP frames: piercing every frame they hit many times over) */
    if (q->t % Y_HIT_GAP == 0)
        proj_box(w, box_opt);
    else if (q->t % Y_HIT_GAP == 2)
        proj_nobox(w);
    if (!dna_alt) {                               /* orbit: r 32 px, 3 deg/f */
        q->b += FACE(p) ? -0x22 : 0x22;
        ang = (q->b + i * 0x555) & 0xfff;
        tx = PX(p) + 32 * cosf(ang * 6.2831853f / 4096);
        ty = PY(p) - 24 - 32 * sinf(ang * 6.2831853f / 4096);
    } else {                                      /* train: slot i follows Axl's path (i + 1) * 30 frames back */
        h = (y_head - (i + 1) * 30) & (Y_HIST - 1);
        tx = y_hx[h];
        ty = y_hy[h] - 24;
        if (q->c)
            q->c--;
        else if ((e = find_target(PX(w), PY(w), 160)) != 0 && fabsf(PX(e) - PX(w)) < 12 && PY(e) > PY(w) + 0x30) {
            char *b = proj_spawn(1, PX(w), PY(w), 0, FXP(-1), FACE(p), 0, y_bomb, P_KEEP);
            if (b) {
                proj_box(b, box_bomb);
                proj_boss_look(b, FX_BOMB);
            }
            q->c = 30;                            /* bomb cooldown */
        }
    }
    d = dna_alt ? FXP(3) : FXP(8);               /* toward the spot, at most 3 px/f (train), 8 while orbiting */
    VX(w) = clampi((int)((tx - PX(w)) * 65536), -d, d);
    VY(w) = -clampi((int)((ty - PY(w)) * 65536), -d, d);
    *(volatile unsigned char *)(w + 0x29) = *(volatile unsigned char *)(p + 0x29);
}

static void y_fire(char *p, int t, int fire)
{
    int i;
    char *w, *s;
    proj *q;

    for (i = 0; i < Y_OPTS; i++) {
        w = y_opt[i];
        q = w ? proj_of(w) : 0;
        if (!q || q->fn != y_option || q->a != i) {
            w = proj_spawn(1, PX(p), PY(p) - 24, 0, 0, FACE(p), 0, y_option, P_PIERCE | P_TOUGH | P_WALL | P_NOSCREEN);
            y_opt[i] = w;
            if (!w)
                continue;
            proj_boss_look(w, FX_OPT);
            proj_of(w)->a = i;                    /* (its box: y_option) */
        }
        /* orbit fire: slot i shoots every y_period[i] frames, 3.75 px/f horizontally */
        if (fire && !dna_alt && t % y_period[i] == 0) {
            s = proj_spawn(1, PX(w), PY(w), SX(p) * 0x3c000, 0, FACE(p), 0, 0, 0);
            if (s && i == 0)
                axl_sound(SND_YAM_SHOT);
            if (s) {
                proj_box(s, box_shot);
                proj_boss_look(s, FX_SHOT);
            }
        }
    }
}

void dna_yammark(char *p, int t, int fire, int fire_new)
{
    int i;

    y_head = (y_head + 1) & (Y_HIST - 1);
    y_hx[y_head] = (short)PX(p);
    y_hy[y_head] = (short)PY(p);
    if (t == 1)
        for (i = 0; i < Y_HIST; i++) {
            y_hx[i] = (short)PX(p);
            y_hy[i] = (short)PY(p);
        }
    y_fire(p, t, fire);
}

/* ---- 2 Blizzard Wolfang: ice chunk lands on the target in 46 frames, 3 random shards ------------------- */
static const signed char box_ice[4] = {-10, -10, 20, 20};
static const signed char box_shard[4] = {-6, -6, 12, 12};

static void w_gravity(char *w, proj *q)
{
    VY(w) -= q->a;
}

static void w_chunk(char *w, proj *q)
{
    int i, t, h, d;
    char *s;

    if (TERR(w)) {                                /* landed: 3 shards t 15-24, height 30-59, distance 10-49 */
        axl_sound(SND_WOLF_BREAK);
        for (i = 0; i < 3; i++) {
            t = rnd() % 10 + 15;
            h = rnd() % 30 + 30;
            d = rnd() % 40 + 10;
            s = proj_spawn(q->id, PX(w), PY(w) - 4, (i & 1 ? 1 : -1) * (d << 16) / t, 0, i & 1, 90, w_gravity,
                           q->flags & P_GIGA);   /* (the chunk's damage id: the giga's too) */
            if (s) {
                proj *r = proj_of(s);
                r->a = (h << 16) / (t * t / 2);
                r->vy = r->a * t;                 /* applied at the set-up */
                if (r->live)
                    VY(s) = r->vy;
                proj_boss_look(s, FX_SHARD);
                proj_box(s, box_shard);
            }
        }
        proj_kill(w);
        return;
    }
    VY(w) -= 0x5d17;
}

void dna_wolfang(char *p, int t, int fire, int fire_new)
{
    float tx, ty;
    char *w;

    if (cooldown)
        cooldown--;
    if (!(fire_new || (fire && !cooldown)) || cooldown)
        return;
    aim_point(p, 200, 96, &tx, &ty);
    w = proj_spawn(2, gun_x(p), gun_y(p) - 8, (int)((tx - gun_x(p)) * 65536) / 46, 0x85d11, FACE(p), 200, w_chunk,
                   P_KEEP | P_NOSCREEN);
    if (w) {
        axl_sound(SND_WOLF_THROW);
        proj_boss_look(w, FX_ICE);
        proj_box(w, box_ice);
    }
    CD(40);
}

/* ---- 3 Blaze Heatnix: straight shot 5 px/f through walls, box 16 x 46 (boss BIN 35 sub 0) -------------- */
static const signed char box_hshot[4] = {-8, -23, 16, 46};

void dna_heatnix(char *p, int t, int fire, int fire_new)
{
    char *w;

    if (cooldown)
        cooldown--;
    if (!(fire_new || (fire && !cooldown)) || cooldown)
        return;
    w = proj_spawn(3, PX(p) + SX(p) * 16, PY(p) - 20, SX(p) * FXP(5), 0, FACE(p), 0, 0, P_PIERCE | P_WALL);
    if (w) {
        axl_sound(SND_HEAT_SHOT);
        proj_boss_look(w, FX_FIRE);
        proj_box(w, box_hshot);
    }
    CD(30);
}

/* ---- 4 Metal Shark Player: aimed anchor 4 px/f, sticks 50 frames, bounces 1.28 / 6.4, 10th contact breaks
 * (boss BIN 36 type 0x16) ------------------------------------------------------------------------------- */
static const signed char box_anchor[4] = {-12, -12, 24, 24};

static void k_anchor(char *w, proj *q)
{
    unsigned char c = TERR(w);

    if (q->b == 0) {                              /* flying */
        if (c) {
            q->b = 1;
            q->c = 50;
            VX(w) = VY(w) = 0;
        }
        return;
    }
    if (q->b == 1) {                              /* stuck */
        VX(w) = VY(w) = 0;
        if (--q->c == 0) {
            q->b = 2;
            VX(w) = FACE(w) ? 0x147ae : -0x147ae;
            VY(w) = 0x66666;
        }
        return;
    }
    if (c) {                                      /* bouncing */
        if (++q->d >= 10) {
            proj_kill(w);
            return;
        }
        if (c & 8)
            VY(w) = 0x66666;
        if (c & 4)
            VY(w) = -0x33333;
        if (c & 3)
            VX(w) = -VX(w);
    }
    VY(w) -= 0x4189;
}

void dna_shark(char *p, int t, int fire, int fire_new)
{
    float tx, ty, a;
    char *w;

    if (cooldown)
        cooldown--;
    if (!(fire_new || (fire && !cooldown)) || cooldown)
        return;
    aim_point(p, 220, 120, &tx, &ty);
    a = atan2f(ty - gun_y(p), tx - gun_x(p));
    w = proj_spawn(4, gun_x(p), gun_y(p), (int)(4 * 65536 * cosf(a)), -(int)(4 * 65536 * sinf(a)), FACE(p), 240,
                   k_anchor, P_KEEP);
    if (w) {
        proj_boss_look(w, FX_ANCHOR);
        proj_box(w, box_anchor);
    }
    CD(30);
}

/* ---- 5 Ground Scaravich: ball kicked onto the target in 40 frames, then rolls 3 px/f on the floor ----------- */
static const signed char box_ball[4] = {-23, -23, 46, 46};

static void s_ball(char *w, proj *q)
{
    unsigned char c = TERR(w);

    if (c & 3) {                                  /* wall: breaks */
        proj_spark(w);
        proj_kill(w);
        return;
    }
    if (!q->b) {
        if (c & 8) {                              /* landed: roll */
            axl_sound(SND_SCAR_ROLL);
            q->b = 1;
            VX(w) = FACE(w) ? FXP(3) : FXP(-3);
            VY(w) = 0;
        } else
            VY(w) -= 0x1400;
        return;
    }
    VY(w) = (c & 8) ? 0 : VY(w) - 0x1400;        /* keep to the floor over slopes and ledges */
}

void dna_scaravich(char *p, int t, int fire, int fire_new)
{
    float tx, ty;
    char *w;

    if (cooldown)
        cooldown--;
    if (!(fire_new || (fire && !cooldown)) || cooldown)
        return;
    aim_point(p, 200, 128, &tx, &ty);
    w = proj_spawn(5, gun_x(p), PY(p) - 24, (int)((tx - gun_x(p)) * 65536) / 40, FXP(2.5), FACE(p), 400, s_ball,
                   P_PIERCE | P_KEEP);
    if (w) {
        axl_sound(SND_SCAR_KICK);
        proj_boss_look(w, FX_BALL);
        proj_box(w, box_ball);
    }
    CD(45);
}

/* ---- 6 Rainy Turtloid: rain shots rise 8 px/f, wait 50 frames, fall at 45 degrees 2 px/f -------------- */
static const signed char box_rain[4] = {-14, -14, 28, 28};

void t_rain(char *w, proj *q)
{
    if (q->b == 0) {                              /* rising, no hit box */
        if (q->t >= 18) {
            q->b = 1;
            q->c = 50;
            VY(w) = 0;
        }
        return;
    }
    if (q->b == 1 && --q->c == 0) {
        q->b = 2;
        VX(w) = FACE(w) ? FXP(2) : FXP(-2);
        VY(w) = FXP(-2);
        proj_box(w, box_rain);
        proj_set_anim(w, FX_RAIN);
    }
}

void dna_turtloid(char *p, int t, int fire, int fire_new)
{
    int i;
    char *w;

    if (cooldown)
        cooldown--;
    if (!(fire_new || (fire && !cooldown)) || cooldown)
        return;
    for (i = -1; i <= 1; i++) {
        w = proj_spawn(6, PX(p) + i * 32, PY(p) - 32, 0, FXP(8), FACE(p), 200, t_rain, P_PIERCE | P_WALL | P_NOSCREEN);
        if (w) {
            proj_boss_look(w, FX_RAIN);
            proj_box(w, box_rain);
            proj_nobox(w);                        /* no hit box while rising */
        }
    }
    CD(60);
}

/* ---- 7 Shield Sheldon: 4 shell plates at +-0x26 around Axl; they block enemy bullets and counter-fire ---
 * Damage ids: the plain Guard Shell id 7 passes through common enemies, so the plates use X's charged Guard Shell
 * id 0x10 and the counter shots X's shell counter shot id 0x35 (PS1) */
#define ID_PLATE 0x10
#define ID_COUNTER 0x35
static const signed char box_plate_h[4] = {-16, -8, 32, 16};
static const signed char box_plate_v[4] = {-8, -16, 16, 32};
static const signed char box_counter[4] = {-4, -4, 8, 8};
static const signed char plate_off[4][2] = {{0, -0x26}, {-0x26, 0}, {0, 0x26}, {0x26, 0}};
#define PLATE_CY 6    /* the plates' centre above Axl's origin: Sheldon's middle (24 then 16 put them high, user reports 2026-10-28) */
static char *s_plate[4];

static void s_counter_fire(char *w, int i)
{
    char *c = proj_spawn(ID_COUNTER, PX(w), PY(w), FXP(2) * plate_off[i][0] / 0x26, -FXP(2) * plate_off[i][1] / 0x26,
                         plate_off[i][0] >= 0, 0, 0, P_WALL);
    if (c) {
        proj_box(c, box_counter);
        proj_boss_look(c, FX_COUNTER);
    }
}

static void s_plate_fn(char *w, proj *q)
{
    char *p = axl_p;
    int i = q->a;

    if (dead_dna(w, 7))
        return;
    q->life = q->t + 2;
    VX(w) = (int)((PX(p) + plate_off[i][0] - PX(w)) * 65536);
    VY(w) = -(int)((PY(p) - PLATE_CY + plate_off[i][1] - PY(w)) * 65536);
    /* enemy bullets that touch the plate are blocked; the plate counter-fires 37 frames later */
    if (block_enemy_shots(PX(w), PY(w), 16) && !q->b) {
        q->b = 37;                                /* the boss: plate anim 0x1a 30 frames, then 0x1b event at 7 */
        proj_boss_look(w, FX_PLATE_HIT);
    }
    if (q->b && --q->b == 0) {
        s_counter_fire(w, i);
        proj_boss_look(w, (i & 1) ? FX_PLATE_V : FX_PLATE_H);
    }
}

/* the shells thrown (user request 2026-10-29: his shot with the shell in his hands, as the boss throws them - BIN 39
 * pattern 4, decompiled): the toss (boss_look 'toss' = anims 0x15 then 0x16) lets shell 1 go at S_TOSS1 and shell 2
 * at S_TOSS2. Shell 1 rolls along the floor at 2 px/f (its box over the feet: a crouch passes under it) up to
 * S_ROLL px or a wall, then rolls back to Axl; shell 2 flies at 4 px/f up and forward toward a point S_FLY_X ahead /
 * S_FLY_Y up (the boss's far upper corner), waits S_WAIT frames there, then flies back to him. Caught (gone) near
 * Axl; both tumble (the FX picture at full size, mirrored every 4 frames). Damage
 * id: the plates' (the boss's shells: damage 8, never destroyed); the box {-16,-12,32,28} (0x80100CA0). While one
 * is out he has no shell in his hands (dna_shell_away: the shell-less pictures, no plates). */
#define S_TOSS1 12
#define S_TOSS2 42            /* (6 + 6 + 2 + 12 of 0x15, then 0x16's 10 + 6) */
#define S_TOSS_LEN 64
#define S_ROLL 224            /* (the boss's: 0xE0 from his side to the far wall) */
#define S_FLY_X 208
#define S_FLY_Y 80            /* (his far upper corner: cy + 0x40 from his cy + 0x90) */
#define S_WAIT 30
static const signed char box_shell[4] = {-16, -12, 32, 28};
static char *s_shell[2];
static char *hg_shell;   /* the giga's shell bounce (hg_shell_fn below): his shell out of his hands too */
static void hg_shell_fn(char *w, proj *q);

static void s_shell_gone(char *w, int i)
{
    proj_kill(w);
    s_shell[i] = 0;
    dna_shell_away = s_shell[0] || s_shell[1] || hg_shell;
}

static void s_shell_fn(char *w, proj *q)
{
    char *p = axl_p;
    int i = q->a, bits = *(volatile unsigned char *)(w + 0xB0);
    float dx, dy, d, sp = i ? 4.0f : 2.0f;

    if (dead_dna(w, 7) || !p) {
        s_shell[i] = 0;
        dna_shell_away = s_shell[0] || s_shell[1] || hg_shell;
        return;
    }
    q->life = q->t + 2;
    if (q->t % 4 == 0)
        *(volatile unsigned char *)(w + 0x29) ^= 0x40;   /* (tumbling: the picture mirrored every 4 frames) */
    dx = PX(p) - PX(w);
    dy = (i ? PY(p) - 20 : PY(p) + 6) - PY(w);   /* (back: to his hands; shell 1 along the floor) */
    if (q->b == 0) {                              /* out */
        float gone = (PX(w) - q->x) * (q->x < PX(w) ? 1 : -1);
        if (i == 0 ? gone >= S_ROLL || (bits & 3) : (bits & 7) || q->t >= 52) {   /* (52: 208 px at 4) */
            q->b = i ? 1 : 2;
            q->c = 0;
            VX(w) = VY(w) = 0;
            q->flags |= P_WALL;
        }
        return;
    }
    if (q->b == 1) {                              /* shell 2 waits up there */
        if (++q->c >= S_WAIT)
            q->b = 2;
        return;
    }
    d = sqrtf(dx * dx + dy * dy);
    if (d < 18 || q->t > 600) {                   /* caught (or lost: 10 s) */
        s_shell_gone(w, i);
        return;
    }
    if (i == 0) {                                 /* rolling back: along x, the floor height followed slowly */
        VX(w) = dx > 0 ? FXP(2) : FXP(-2);
        VY(w) = -(int)((dy > 2 ? 2 : dy < -2 ? -2 : dy) * 65536);
    } else {
        VX(w) = (int)(dx / d * sp * 65536);
        VY(w) = -(int)(dy / d * sp * 65536);
    }
}

static void s_shell_throw(char *p, int i)
{
    float vx, vy, d = sqrtf((float)S_FLY_X * S_FLY_X + (float)S_FLY_Y * S_FLY_Y);
    char *w;

    if (i == 0) {
        vx = 2.0f;
        vy = 0;
    } else {
        vx = 4.0f * S_FLY_X / d;
        vy = 4.0f * S_FLY_Y / d;
    }
    w = proj_spawn(ID_PLATE, PX(p) + SX(p) * 16, i ? PY(p) - 20 : PY(p) + 6, SX(p) * (int)(vx * 65536),
                   (int)(vy * 65536), FACE(p), 0, s_shell_fn, P_PIERCE | P_TOUGH | P_KEEP | P_NOSCREEN);
    if (!w)
        return;
    proj_boss_look(w, FX_SHELL);
    proj_box(w, box_shell);
    proj_of(w)->a = i;
    s_shell[i] = w;
    dna_shell_away = 1;
}

void dna_sheldon(char *p, int t, int fire, int fire_new)
{
    int i;
    char *w;
    proj *q;

    if (cooldown)
        cooldown--;
    for (i = 0; i < 2; i++)
        if (s_shell[i] && (!proj_of(s_shell[i]) || proj_of(s_shell[i])->fn != s_shell_fn))
            s_shell[i] = 0;
    if (hg_shell && (!proj_of(hg_shell) || proj_of(hg_shell)->fn != hg_shell_fn))
        hg_shell = 0;
    dna_shell_away = s_shell[0] || s_shell[1] || hg_shell;
    if (dna_toss_t >= 0) {                        /* the toss motion: shell 1, then shell 2 */
        if (dna_toss_t == S_TOSS1)
            s_shell_throw(p, 0);
        else if (dna_toss_t == S_TOSS2)
            s_shell_throw(p, 1);
        if (++dna_toss_t >= S_TOSS_LEN)
            dna_toss_t = -1;
    } else if (!dna_alt && fire_new && !dna_shell_away && !cooldown) {
        dna_toss_t = 0;                           /* (the shell in his hands: the shot throws it) */
        CD(10);
    }
    for (i = 0; i < 4; i++) {
        w = s_plate[i];
        if (!dna_alt) {                           /* the shell held; the DNA button: Guard Shell (plates) on / off */
            if (w && proj_of(w) && proj_of(w)->fn == s_plate_fn)
                proj_kill(w);
            s_plate[i] = 0;
            continue;
        }
        q = w ? proj_of(w) : 0;
        if (!q || q->fn != s_plate_fn || q->a != i) {
            w = proj_spawn(ID_PLATE, PX(p) + plate_off[i][0], PY(p) - PLATE_CY + plate_off[i][1], 0, 0, FACE(p), 0, s_plate_fn,
                           P_PIERCE | P_TOUGH | P_WALL | P_NOSCREEN);
            s_plate[i] = w;
            if (!w)
                continue;
            proj_boss_look(w, (i & 1) ? FX_PLATE_V : FX_PLATE_H);
            proj_box(w, (i & 1) ? box_plate_v : box_plate_h);
            proj_of(w)->a = i;
        }
        if (fire_new && !cooldown)
            s_counter_fire(w, i);
    }
    if (fire_new && !cooldown)
        CD(30);
}

/* ---- 8 Infinity Mijinion: Ray Arrow 2.5 px/f, turns 90 degrees within 8 px of the target's x, sticks ------- */
static const signed char box_ray_h[4] = {-20, -2, 24, 4};
static const signed char box_ray_up[4] = {-2, -20, 4, 24};
static const signed char box_ray_down[4] = {-2, -4, 4, 24};

static void m_arrow(char *w, proj *q)
{
    char *e;

    if (q->b == 2) {                              /* stuck in the terrain */
        VX(w) = VY(w) = 0;
        if (--q->c == 0)
            proj_kill(w);
        return;
    }
    if (TERR(w)) {
        q->b = 2;
        q->c = 20;
        VX(w) = VY(w) = 0;
        return;
    }
    if (q->b == 0 && (e = find_target(PX(w), PY(w), 240)) != 0 && fabsf(PX(e) - PX(w)) <= 8) {
        q->b = 1;                                 /* 90 degrees toward the target */
        VX(w) = 0;
        VY(w) = PY(e) < PY(w) ? FXP(3) : FXP(-3);
        proj_box(w, VY(w) > 0 ? box_ray_up : box_ray_down);
        proj_set_anim(w, FX_RAY_V);
    }
}

void dna_mijinion(char *p, int t, int fire, int fire_new)
{
    char *w;

    if (cooldown)
        cooldown--;
    if (!(fire_new || (fire && !cooldown)) || cooldown)
        return;
    w = proj_spawn(8, gun_x(p), gun_y(p), SX(p) * FXP(2.5), 0, FACE(p), 300, m_arrow, P_PIERCE | P_KEEP);
    if (w) {
        proj_boss_look(w, FX_RAY_H);
        proj_box(w, box_ray_h);
    }
    CD(24);
}

/* ---- giga attacks (the giga button while changed; docs/skills_design.md, plan A, user decision 2026-11-03) -------
 * dna_giga_start (axl_x.c, before X's state code) starts the changed boss's giga when its 1200-frame cooldown is over
 * (axl_skills.c skill_try: refused while cooling, logged); dna_giga_tick (axl_dna.c dna_after) runs it. Wolfang,
 * Scaravich and Mijinion stand still in the attack's pose while they throw / kick / fire (the boss's attack motion,
 * dna_giga_look); Yammark keeps flying, his options do the work. Damage: X's charged weapon ids (k + 9: 0x0A
 * Yanma Option's entry, 0x0B Ice Burst, 0x0E Ground Dash, 0x11 Ray Arrow charged; the weakness tables apply as for
 * X's charged shots), each projectile hitting an enemy once (P_PIERCE) or every 20 frames (P_TOUGH: the big ball).
 * Every giga projectile carries P_GIGA: gone with the change (proj_kill_dna). Batch 2: Heatnix (0x0C), Shark (0x0D),
 * Turtloid (0x0F) and Sheldon (0x10); Heatnix and Turtloid stand still, Shark swims (axl_moves.c moves_giga_swim),
 * Sheldon keeps moving while his shell bounces. */
#define GID_YAM 0x0A
#define GID_WOLF 0x0B
#define GID_HEAT 0x0C
#define GID_SHARK 0x0D
#define GID_SCAR 0x0E
#define GID_TURT 0x0F
#define GID_SHEL 0x10
#define GID_MIJ 0x11
#define GIGA_SEC(k) (HUD_PAT + (k) - 1)   /* the boss's HUD parts section holds his giga pictures (GIGA_*) */

static int giga_k, giga_t, giga_pose = -1;   /* the running giga's boss (0 none), its frame, its atk motion time */

void dna_giga_reset(void)
{
    giga_k = 0;
    giga_t = 0;
    giga_pose = -1;
}

/* the frame of the boss's attack motion (dna_looks atk) to show while a giga holds him, -1 none (axl_dna.c dna_look) */
int dna_giga_look(void)
{
    return giga_k ? giga_pose : -1;
}

/* 1 Yammark: 6 options line up above him (26 px apart, 72 up) in 30 frames, then dive one after another every 8
 * frames, 7 px/f at the nearest enemy below them (none: forward and down), and burst into 3 blasts where they hit the terrain
 * (BIN 33 sub 6, the option line dive the boss keeps for Zero; the 6 options of his last third) */
#define YG_N 6
#define YG_LINE 30
#define YG_GAP 8

static void yg_blasts(char *w)
{
    static const signed char off[3][2] = {{10, -8}, {-16, -4}, {4, -12}};
    int i;
    char *b;
    for (i = 0; i < 3; i++) {
        b = proj_spawn(GID_YAM, PX(w) + off[i][0], PY(w) + off[i][1], 0, 0, 1, 30, 0,
                       P_PIERCE | P_WALL | P_NOSCREEN | P_GIGA);
        if (b) {
            proj_box(b, box_blast);
            proj_boss_look(b, FX_BOOM);
        }
    }
}

static void yg_option(char *w, proj *q)
{
    char *p = axl_p, *e;
    int i = q->a, d;
    float tx, ty, dx, dy, n;

    if (dead_dna(w, 1) || !p)
        return;
    if (q->b == 1) {                              /* diving: straight on until the terrain */
        if (TERR(w)) {
            yg_blasts(w);
            proj_kill(w);
        }
        return;
    }
    if (q->t >= YG_LINE + YG_GAP * i) {           /* its turn: the dive */
        e = find_target(PX(w), PY(w), 260);
        if (e && PY(e) < PY(w) + 16)
            e = 0;                                /* (a dive: only at what is below the line) */
        dx = e ? PX(e) - PX(w) : SX(p) * 40.0f;
        dy = e ? PY(e) - 8 - PY(w) : 80.0f;
        n = sqrtf(dx * dx + dy * dy);
        if (n < 1)
            n = 1;
        VX(w) = (int)(dx / n * 7 * 65536);
        VY(w) = -(int)(dy / n * 7 * 65536);
        q->b = 1;
        q->flags = (q->flags & ~P_WALL) | P_KEEP;
        q->life = q->t + 90;
        proj_box(w, box_opt);
        axl_sound(SND_YAM_SHOT);
        return;
    }
    tx = PX(p) + (i - (YG_N - 1) / 2.0f) * 26;   /* the line above him */
    ty = PY(p) - 72;
    d = FXP(8);
    VX(w) = clampi((int)((tx - PX(w)) * 65536), -d, d);
    VY(w) = -clampi((int)((ty - PY(w)) * 65536), -d, d);
    *(volatile unsigned char *)(w + 0x29) = *(volatile unsigned char *)(p + 0x29);
}

static void giga_yammark(char *p)
{
    int i;
    char *w;
    for (i = 0; i < YG_N; i++) {
        w = proj_spawn(GID_YAM, PX(p), PY(p) - 24, 0, 0, FACE(p), 0, yg_option,
                       P_PIERCE | P_WALL | P_NOSCREEN | P_GIGA);
        if (!w)
            continue;
        proj_boss_look(w, FX_OPT);
        proj_box(w, box_opt);
        proj_nobox(w);                            /* (no hit while lining up) */
        proj_of(w)->a = i;
    }
    giga_k = 0;                                   /* (the options go on by themselves; he keeps flying) */
}

/* 2 Wolfang: 4 ice chunks in a row (BIN 34 sub 0xA: 3-5 throws), one every 30 frames at the throw's frame of his
 * attack motion (30), each onto one of the enemies around (in turn; none: 64 + 40 i px ahead), breaking into 3
 * shards where it lands (w_chunk) */
#define WG_N 4
#define WG_GAP 30
#define WG_LEN (WG_N * WG_GAP)

static void giga_wolfang(char *p, int t)
{
    char *list[8], *w;
    int n, i = t / WG_GAP;
    float tx;

    giga_pose = 16 + t % WG_GAP;                  /* (the motion from picture 4; the throw at its frame 30) */
    if (t % WG_GAP == 14 && i < WG_N) {
        n = objs_targets(PX(p), PY(p), 240, 140, list, 8);
        tx = n ? PX(list[i % n]) : gun_x(p) + SX(p) * (64 + 40 * i);
        w = proj_spawn(GID_WOLF, gun_x(p), gun_y(p) - 8, (int)((tx - gun_x(p)) * 65536) / 46, 0x85d11, FACE(p), 200,
                       w_chunk, P_KEEP | P_NOSCREEN | P_GIGA);
        if (w) {
            axl_sound(SND_WOLF_THROW);
            proj_boss_look(w, FX_ICE);
            proj_box(w, box_ice);
        }
    }
    if (t + 1 >= WG_LEN)
        giga_k = 0;
}

/* 3 Heatnix: 5 fireballs thrown one after another (BIN 35 sub 4: the thrown fireball, 32 x 32, bursting into 3
 * shards at 5 px/f in the upper half where it lands; extra throws 0-2 there, 5 here), one every 16 frames at the
 * shot's frame of his attack motion (18), each onto one of the enemies around in turn (none: 40 + 28 i px ahead) in
 * HG_T frames - a higher arc than the boss's (he throws from high up; gravity 0x2000, his 0x1000) */
#define HG_N 5
#define HG_GAP 16
#define HG_T 40
#define HG_LEN (HG_N * HG_GAP + 8)
static const signed char box_fireball[4] = {-16, -16, 32, 32};
static const signed char box_fshard[4] = {-6, -6, 12, 12};

static void hg_fireball(char *w, proj *q)
{
    int used = 0, n = 0, k;
    float a;
    char *s;

    if (dead_dna(w, 3))
        return;
    if (!(TERR(w) & 0xB) && q->t < 150) {         /* (a floor or a wall: the burst; the ceiling is passed) */
        VY(w) -= 0x2000;
        return;
    }
    while (n < 3) {                               /* 3 shards, at k * 15 degrees, k 1-11 all different */
        k = 1 + rnd() % 11;
        if (used >> k & 1)
            continue;
        used |= 1 << k;
        n++;
        a = k * 15 * 0.0174533f;
        s = proj_spawn(GID_HEAT, PX(w), PY(w) - 8, (int)(cosf(a) * 5 * 65536), (int)(sinf(a) * 5 * 65536), k < 6, 40, 0,
                       P_PIERCE | P_GIGA);
        if (s) {
            proj_boss_look(s, FX_FIREBALL);
            proj_box(s, box_fshard);
        }
    }
    proj_kill(w);
}

static void giga_heatnix(char *p, int t)
{
    char *list[8], *w;
    int n, i = t / HG_GAP, vx, vy;
    float x, y, tx, ty;

    giga_pose = 10 + t % HG_GAP;                  /* (the motion from picture 5; the shot's frame 18 at the throw) */
    if (t % HG_GAP == 8 && i < HG_N) {
        x = PX(p) + SX(p) * 16;
        y = PY(p) - 24;
        n = objs_targets(PX(p), PY(p), 150, 100, list, 8);   /* (on the screen: the bursts seen) */
        tx = n ? PX(list[i % n]) : x + SX(p) * (40 + 28 * i);
        ty = n ? PY(list[i % n]) - 8 : PY(p) - 8;
        vx = clampi((int)((tx - x) * 65536) / HG_T, FXP(-6), FXP(6));
        /* up speed so that it is at ty after HG_T frames: (y - ty) / T + g T / 2 (at most 4: a target high up made
         * it leave the screen, 2026-11-03 e2e) */
        vy = clampi((int)(((y - ty) / HG_T + 0.125f * HG_T / 2) * 65536), FXP(1.5), FXP(4));
        w = proj_spawn(GID_HEAT, x, y, vx, vy, FACE(p), 0,
                       hg_fireball, P_KEEP | P_NOSCREEN | P_GIGA);
        if (w) {
            axl_sound(SND_HEAT_SHOT);
            proj_boss_look(w, FX_FIREBALL);
            proj_box(w, box_fireball);
        }
        if (test_on)
            test_log("giga 3 throw %d at %.0f", i, tx);
    }
    if (t + 1 >= HG_LEN)
        giga_k = 0;
}

/* 4 Shark: the junk swim (BIN 36 sub 7: the fin only, 5 px/f at the target, passing it and back) for KG_LEN frames,
 * invincible, the fin's box hitting (axl_moves.c moves_giga_swim), and the junk raining around it: every KG_GAP
 * frames a piece of scrap (the anchor's picture, tumbling) falls from KG_UP px above, ahead of the fin or onto an
 * enemy near it, and breaks on the floor (the junk of his sub 8 assembly, without its picture) */
#define KG_LEN 150
#define KG_GAP 10
#define KG_UP 150
#define KG_THROUGH 18   /* frames the scrap falls through the terrain (it starts above the ceiling, maybe) */
static int kg_started;

static void kg_scrap(char *w, proj *q)
{
    if (dead_dna(w, 4))
        return;
    if (q->t % 4 == 0)
        *(volatile unsigned char *)(w + 0x29) ^= 0x40;   /* (tumbling) */
    if (q->t == KG_THROUGH)
        q->flags = (q->flags & ~P_WALL) | P_KEEP;   /* (the terrain from now: its floor) */
    if (q->t > KG_THROUGH && (TERR(w) & 8)) {
        proj_spark(w);
        proj_kill(w);
        return;
    }
    if (VY(w) > FXP(-7))
        VY(w) -= 0x4189;                          /* (the anchor's gravity) */
}

static void giga_shark(char *p, int t)
{
    char *e, *w;
    float x;

    giga_pose = -1;                               /* (the swim's own look: axl_dna.c dna_look, moves_look 1) */
    if (t == 0) {
        kg_started = moves_giga_swim(p, GID_SHARK, KG_LEN);
        if (test_on)
            test_log("giga 4 swim %d", kg_started);
    }
    if (!kg_started || (t > 0 && !moves_look())) {
        giga_k = 0;                               /* (the swim over) */
        return;
    }
    if (t % KG_GAP == 5) {
        e = find_target(PX(p), PY(p), 160);
        x = e && rnd() % 3 ? PX(e) + (rnd() % 33 - 16) : PX(p) + SX(p) * (24 + rnd() % 72);
        w = proj_spawn(GID_SHARK, x, PY(p) - KG_UP, 0, FXP(-1), rnd() & 1, 120, kg_scrap,
                       P_PIERCE | P_WALL | P_NOSCREEN | P_GIGA);
        if (w) {
            proj_boss_look(w, FX_ANCHOR);
            proj_box(w, box_anchor);
            if (t % (KG_GAP * 3) == 5)
                axl_sound(SND_WOLF_THROW);
        }
    }
}

/* 6 Turtloid: the turret volley (BIN 38 sub 5: every 8 frames a random turret fires, 10-22 shots; each goes out
 * 1 px/f for 24 frames at 0 / 40 / 80 degrees up, stops, and at 40 frames rushes 4 px/f at the target, following
 * its height 0.5 px/f; {-14,-5,28,10}): TG_N shots one every TG_GAP frames from his 2 turrets (the vents on his
 * shell, TG_TX px each side and TG_TY up; at random), the rain's drop picture. The rush is aimed (4 px/f, then
 * a little homing on the height): level as the boss's, from the shell's top they passed over the enemies on the
 * ground (2026-11-03 e2e). He stands in his shot's pose (dna_looks fire: the shell, its vents) meanwhile. */
#define TG_N 16
#define TG_GAP 6
#define TG_LEN (TG_N * TG_GAP + 4)
#define TG_TX 35
#define TG_TY 88
static const signed char box_turret[4] = {-14, -5, 28, 10};

static void tg_shot(char *w, proj *q)
{
    char *e;
    float ty, dx, dy, d;

    if (dead_dna(w, 6))
        return;
    if (q->t == 24) {
        VX(w) = VY(w) = 0;
    } else if (q->t == 40) {                      /* the rush: at the nearest target (none: level, the way it faced) */
        e = find_target(PX(w), PY(w), 320);
        dx = e ? PX(e) - PX(w) : (q->right ? 1 : -1);
        dy = e ? PY(e) - 12 - PY(w) : 0;
        d = sqrtf(dx * dx + dy * dy);
        if (d < 1)
            d = 1;
        VX(w) = (int)(dx / d * 4 * 65536);
        VY(w) = -(int)(dy / d * 4 * 65536);
        *(volatile unsigned char *)(w + 0x29) = (*(volatile unsigned char *)(w + 0x29) & ~0x40) | (dx > 0 ? 0x40 : 0);
        proj_box(w, box_turret);
    } else if (q->t > 40 && (e = find_target(PX(w), PY(w), 320)) != 0) {   /* toward its height: 1/32 px/f more a frame */
        ty = PY(e) - 12;
        VY(w) += ty < PY(w) - 1 ? 0x800 : ty > PY(w) + 1 ? -0x800 : 0;
        VY(w) = clampi(VY(w), FXP(-4), FXP(4));
    }
}

static void giga_turtloid(char *p, int t)
{
    static const unsigned char ang[3] = {0, 40, 80};
    int i = t / TG_GAP, side;
    float a, x;
    char *w;

    giga_pose = t;                                /* (his shot's pose, held: dna_look's fire motion) */
    if (t % TG_GAP == 0 && i < TG_N) {
        side = rnd() & 1;                         /* the front or the back turret */
        x = PX(p) + SX(p) * (side ? TG_TX : -TG_TX);
        a = ang[i % 3] * 0.0174533f;
        w = proj_spawn(GID_TURT, x, PY(p) - TG_TY, SX(p) * (int)(cosf(a) * 65536), (int)(sinf(a) * 65536), FACE(p),
                       150, tg_shot, P_PIERCE | P_WALL | P_NOSCREEN | P_GIGA);
        if (w) {
            proj_boss_look(w, FX_RAIN);
            proj_box(w, box_turret);
            proj_nobox(w);                        /* (no hit until the rush, as the rain while it rises) */
            if (i % 2 == 0)
                axl_sound(SND_COPY);
        }
    }
    if (t + 1 >= TG_LEN)
        giga_k = 0;
}

/* 7 Sheldon: the shell bounce (BIN 39 sub 3: the closed shell goes round 8 fixed points of the screen at 6, 6, 7, 7,
 * 8, 8, 9, 9 px/f, points 1 and 4 at the player's x half the time, a sound at each wall; it blocks every shot): his
 * shell (the thrown shell's picture, tumbling - faster from the third point) round 8 points fixed where he stood
 * (both sides HXW px off, 3 heights; the first and the fourth at an enemy's x when there is one), blocking the enemy
 * bullets it touches, then back into his hands. He keeps moving, shell-less meanwhile (dna_shell_away: no plates, no
 * toss). */
#define HXW 112
static const signed char hg_pt[8][2] = {{1, -24}, {-1, -100}, {1, -100}, {-1, -24}, {1, -62}, {-1, -62}, {0, -120}, {-1, -24}};
static float hg_x[8], hg_y[8];

static void hg_shell_fn(char *w, proj *q)
{
    char *p = axl_p;
    float tx, ty, dx, dy, d, sp;

    if (dead_dna(w, 7) || !p) {
        hg_shell = 0;
        return;
    }
    q->life = q->t + 2;
    if (q->t % (q->a >= 2 ? 2 : 4) == 0)
        *(volatile unsigned char *)(w + 0x29) ^= 0x40;
    if (block_enemy_shots(PX(w), PY(w), 18) && test_on)
        test_log("giga 7 shell blocked a shot");
    if (q->a < 8) {
        tx = hg_x[q->a];
        ty = hg_y[q->a];
        sp = 6 + q->a / 2;
    } else {                                      /* back into his hands */
        tx = PX(p);
        ty = PY(p) - 20;
        sp = 9;
    }
    dx = tx - PX(w);
    dy = ty - PY(w);
    d = sqrtf(dx * dx + dy * dy);
    if (q->a >= 8 && (d < 18 || q->t > 600)) {
        if (test_on)
            test_log("giga 7 shell back at t %d", q->t);
        proj_kill(w);
        hg_shell = 0;
        dna_shell_away = s_shell[0] || s_shell[1];
        return;
    }
    if (q->a < 8 && d <= sp) {                    /* the point: the next one */
        axl_sound(SND_SCAR_KICK);
        q->a++;
        VX(w) = (int)(dx * 65536);
        VY(w) = -(int)(dy * 65536);
        return;
    }
    VX(w) = (int)(dx / d * sp * 65536);
    VY(w) = -(int)(dy / d * sp * 65536);
}

static void giga_sheldon(char *p)
{
    char *e = find_target(PX(p), PY(p), 240);
    int i;

    for (i = 0; i < 8; i++) {
        hg_x[i] = PX(p) + SX(p) * hg_pt[i][0] * HXW;
        hg_y[i] = PY(p) + hg_pt[i][1];
        if ((i == 0 || i == 3) && e && rnd() & 1)
            hg_x[i] = PX(e);
    }
    dna_alt = 0;                                  /* (the plates go: the shell is out) */
    hg_shell = proj_spawn(GID_SHEL, PX(p) + SX(p) * 16, PY(p) - 20, 0, 0, FACE(p), 0, hg_shell_fn,
                          P_PIERCE | P_TOUGH | P_WALL | P_NOSCREEN | P_GIGA);
    if (hg_shell) {
        proj_boss_look(hg_shell, FX_SHELL);
        proj_box(hg_shell, box_shell);
        proj_of(hg_shell)->a = 0;
        dna_shell_away = 1;
    }
    giga_k = 0;                                   /* (the shell goes on by itself; he keeps moving) */
}

/* 5 Scaravich: the size-4 ball (BIN 37: 100 x 100, damage 16) made in front of him, kicked at the kick's frame of his
 * attack motion (23), rolling 3 px/f along the floor through the enemies (each again every 20 frames); a wall (or
 * 6 s) breaks it into 4 balls flung up */
#define SG_KICK 23
#define SG_LEN 38
static const signed char box_big[4] = {-50, -50, 100, 100};

static void sg_break(char *w)
{
    int i;
    char *s;
    axl_sound(SND_SCAR_ROLL);
    for (i = 0; i < 4; i++) {
        s = proj_spawn(GID_SCAR, PX(w) + (i - 1.5f) * 20, PY(w) - 10, (int)((i - 1.5f) * FXP(1.5)), FXP(3 + (i & 1)), i >= 2,
                       60, w_gravity, P_PIERCE | P_NOSCREEN | P_GIGA);
        if (s) {
            proj_of(s)->a = 0x2000;
            proj_boss_look(s, FX_BALL);
            proj_box(s, box_ball);
        }
    }
    proj_spark(w);
    proj_kill(w);
}

/* its terrain tests are its own (P_WALL): the 100 px box reads wall bits where it stands too (2 on the intro's flat
 * floor, 2026-11-03 e2e), so a wall is a wall bit SG_LIFT px higher that the spot 8 px behind does not have (as
 * axl_moves.c wall_ahead); the floor where it is (sunk in: lifted out, at most 8 px a frame) */
#define SG_LIFT 12

static void sg_ball(char *w, proj *q)
{
    float y = PY(w);
    int f = GIGA_BALL + (q->t / 5) % GIGA_BALL_N, wall, floor, n;

    if (dead_dna(w, 5))
        return;
    if (q->c != f) {                              /* rolling: its 3 pictures (held frames of the HUD section) */
        q->c = f;
        proj_pose_look(w, HUD_TEX_INDEX, GIGA_SEC(5), f);
    }
    PY(w) = y - SG_LIFT;
    wall = proj_terrain(w) & 3;
    PX(w) -= q->a * 8;
    wall &= ~proj_terrain(w);
    PX(w) += q->a * 8;
    PY(w) = y;
    floor = proj_terrain(w) & 8;
    for (n = 0; floor && n < 8 && (proj_terrain(w) & 8); n++)
        PY(w) -= 1;                               /* (out of the floor: its bottom on it) */
    if (n)
        PY(w) += 1;
    if (q->b && (wall || q->t > 360)) {
        if (test_on)
            test_log("giga 5 ball breaks at t %d (wall %x) x %.0f", q->t, wall, PX(w));
        sg_break(w);
        return;
    }
    VX(w) = q->b ? q->a * FXP(3) : 0;
    VY(w) = floor ? 0 : VY(w) - 0x1400;           /* on the floor, falling off ledges */
}

static char *sg_the_ball;

static void giga_scaravich(char *p, int t)
{
    proj *q;

    giga_pose = t;                                /* the kick motion as it is */
    if (t == 0) {
        sg_the_ball = proj_spawn(GID_SCAR, PX(p) + SX(p) * 58, PY(p) - 26, 0, 0, 0, 0, sg_ball,
                                 P_PIERCE | P_TOUGH | P_WALL | P_GIGA);
        if (sg_the_ball) {
            q = proj_of(sg_the_ball);
            q->a = SX(p);
            q->c = -1;
            proj_box(sg_the_ball, box_big);
            proj_pose_look(sg_the_ball, HUD_TEX_INDEX, GIGA_SEC(5), GIGA_BALL);
        }
    }
    if (t == SG_KICK && sg_the_ball && (q = proj_of(sg_the_ball)) != 0 && q->fn == sg_ball) {
        axl_sound(SND_SCAR_KICK);
        axl_sound(SND_SCAR_ROLL);
        q->b = 1;
        if (test_on)
            test_log("giga 5 kick");
    }
    if (t + 1 >= SG_LEN) {
        sg_the_ball = 0;
        giga_k = 0;
    }
}

/* 8 Mijinion: the energy gathers (18 frames), then 4 waves of the 8-way shot (BIN 40 subs 7-10: 0-315 degrees, 6 px/f,
 * {-4,-4,8,8}, several times), every other wave turned 22.5 degrees, one every 24 frames at the shot's frame of his
 * attack motion (31) */
#define MG_GATHER 20
#define MG_N 4
#define MG_GAP 24
#define MG_LEN (MG_GATHER + MG_N * MG_GAP)
static const signed char box_orb[4] = {-4, -4, 8, 8};

static void mg_pic(char *w, proj *q)
{
    int f = q->a + (q->t / 3) % q->b;
    if (dead_dna(w, 8))
        return;
    if (q->c != f) {
        q->c = f;
        proj_pose_look(w, HUD_TEX_INDEX, GIGA_SEC(8), f);
    }
}

static void giga_mijinion(char *p, int t)
{
    int i, w = (t - MG_GATHER - 7) / MG_GAP;
    float a, x = PX(p), y = PY(p) - 16;
    char *s;
    proj *q;

    giga_pose = t < MG_GATHER ? t : 24 + (t - MG_GATHER) % MG_GAP;
    if (t == 0 && (s = proj_spawn(GID_MIJ, x, y, 0, 0, 0, 3 * GIGA_GATHER_N, mg_pic, P_WALL | P_NOSCREEN | P_GIGA))) {
        q = proj_of(s);
        q->a = GIGA_GATHER;
        q->b = GIGA_GATHER_N;
        q->c = -1;
        proj_nobox(s);
        proj_pose_look(s, HUD_TEX_INDEX, GIGA_SEC(8), GIGA_GATHER);
    }
    if (t >= MG_GATHER + 7 && (t - MG_GATHER - 7) % MG_GAP == 0 && w < MG_N) {
        axl_sound(SND_COPY);
        for (i = 0; i < 8; i++) {
            a = (i * 45 + (w & 1) * 22.5f) * 0.0174533f;
            s = proj_spawn(GID_MIJ, x, y, (int)(cosf(a) * 6 * 65536), (int)(sinf(a) * 6 * 65536), 0, 70, mg_pic,
                           P_PIERCE | P_GIGA);
            if (!s)
                continue;
            q = proj_of(s);
            q->a = GIGA_ORB;
            q->b = GIGA_ORB_N;
            q->c = -1;
            proj_box(s, box_orb);
            proj_pose_look(s, HUD_TEX_INDEX, GIGA_SEC(8), GIGA_ORB);
        }
        if (test_on)
            test_log("giga 8 wave %d", w);
    }
    if (t + 1 >= MG_LEN)
        giga_k = 0;
}

/* before X's state code, changed (axl_x.c): the giga button. Returns 1 while the giga holds the boss still. */
int dna_giga_start(char *p, int giga_new)
{
    int k = dna_boss, st;

    if (giga_k && giga_k != k)
        dna_giga_reset();
    if (giga_k)
        return giga_k != 1;
    if (!giga_new || k < 1 || k > 8)
        return 0;
    st = *(volatile unsigned char *)(p + 0x0D);
    if (moves_look() || dna_toss_t >= 0 || (k == 7 && dna_shell_away) ||
        (k == 4 && st != 2 && st != 3 && st != 4 && st != 8)) {
        /* a move or second attack on, Sheldon's shell thrown, Shark off the ground (his swim starts there): not now */
        if (test_on)
            test_log("giga %d: busy (move look %d)", k, moves_look());
        return 0;
    }
    if (!skill_try(SK_GIGA))
        return 0;
    giga_k = k;
    giga_t = 0;
    giga_pose = -1;
    if (test_on)
        test_log("giga %d start", k);
    return k != 1 && k != 7;
}

/* every changed frame after X's state code (axl_dna.c dna_after) */
void dna_giga_tick(char *p)
{
    int t;

    if (!giga_k)
        return;
    if (giga_k != dna_boss) {
        dna_giga_reset();
        return;
    }
    t = giga_t++;
    switch (giga_k) {
    case 1: giga_yammark(p); break;
    case 2: giga_wolfang(p, t); break;
    case 3: giga_heatnix(p, t); break;
    case 4: giga_shark(p, t); break;
    case 5: giga_scaravich(p, t); break;
    case 6: giga_turtloid(p, t); break;
    case 7: giga_sheldon(p); break;
    case 8: giga_mijinion(p, t); break;
    default: giga_k = 0; break;
    }
    if (!giga_k) {
        giga_pose = -1;
        if (test_on)
            test_log("giga end (t %d)", t);
    }
}
