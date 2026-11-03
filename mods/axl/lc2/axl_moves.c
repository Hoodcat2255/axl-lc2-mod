/* The bosses' own moves while changed (user design 2026-10-26, from the bosses' movement patterns in
 * docs/boss_attacks.md): the dash button is the boss's move, the DNA button (giga) its second attack.
 *
 *   boss        dash (move)                                   DNA button (second attack)
 *   Yammark     fast flight (axl_x.c flight)                  option train (axl_dna_bosses.c, dna_alt)
 *   Wolfang     ice-spike dash 6 px/f, spikes, body hit, afterimages  leap 20 degrees up 6 px/f, onto a wall
 *               walls and ceiling (wolf_*): a wall touched in the air holds him (jump: leap off, down: let go, DNA
 *               button: ice up and down the wall); up + jump: up to the ceiling, hanging (shot: 4 aimed shards, dash:
 *               the ceiling dash leaving icicles, down / jump: let go)
 *   Heatnix     firebird rush 8 px/f, invincible, big body    thrown fireball, 3 shards at 5 px/f where it lands
 *   Shark       junk swim: under the floor, the fin only,     leap 8 px/f above the target, hover, pound 5 px/f
 *               5 px/f left / right while dash is held, invincible (walking: the prone fin, boss_look walk)
 *   Scaravich   charge 5 px/f, body hit                       small balls 3 px/f
 *   Turtloid    shell roll up to 5 px/f, invincible, rain     shell bounce x3, invincible
 *   Sheldon     teleport 80 px forward (8 px steps up to a wall)  shell plates on / off (dna_alt)
 *   Mijinion    drop 9 px/f (in the air), body hit, landing:  3 small clones homing 1.5 px/f
 *               laser rain (2 emitters x 4 beams 16 px/f)
 * (2026-10-27, docs/boss_patterns.md: Heatnix's and Shark's moves had each other's numbers - boss_attacks.md has
 * BIN 35 / 36 swapped; the speeds, boxes and landing follow the original code now)
 *
 * A move takes Axl's input while it lasts; after X's state code it moves him itself (x, y floats) and undoes a step
 * the terrain test (FUN_023cef30 on the player's own terrain box) finds in a wall (bits 1 / 2) or, going down, the
 * floor (bit 8). The body hit is a hidden projectile following him with the boss's damage id; invincible = no hurt
 * box (axl_box asks moves_invincible).
 */
#include <windows.h>
#include <math.h>

#include "axl_lc2.h"
#include "axl_proj.h"
#include "axl_data.h"      /* the game-derived tables (axl_data.bin) */
#include "axl_test.h"

#define O8(p, o) (*(volatile unsigned char *)((p) + (o)))
#define PX(p) (*(volatile float *)((p) + 0x14))
#define PY(p) (*(volatile float *)((p) + 0x1C))
#define VX(w) (*(volatile int *)((w) + 0x3C))
#define VY(w) (*(volatile int *)((w) + 0x40))
#define GRAVITY(p) (*(volatile int *)((p) + 0x48))
#define FACE(p) ((O8(p, 0x29) & 0x40) != 0)
#define SX(p) (FACE(p) ? 1 : -1)
#define STATE(p) O8(p, 0x0D)
#define FXP(n) ((int)((n) * 65536))

void t_rain(char *w, proj *q);   /* axl_dna_bosses.c */

static int move_k, move_t, move_len, move_inv, move_dir, bounces;
#define TELE_AT 12   /* Sheldon's teleport: frames out (boss_look: anim 0xD shortened), the warp, frames in (0xC) */
static float move_v, move_vy;
static char *body;
static int wolf_body;   /* (a Wolfang leap / ceiling dash keeps the body hit) */
static int seq_k, seq_n, seq_t;   /* a second attack fired over several frames */

static const signed char box_body[4] = {-20, -40, 40, 44};
/* the bosses' own boxes, moved to Axl's origin (the look's feet on his: the boss origin dy_all above / below) */
static const signed char box_wolf[4] = {-64, -33, 96, 32};     /* dash picture 25 {-64,-16,96,32}, origin 17 up */
static const signed char box_bird[4] = {-96, -67, 127, 96};    /* the firebird {-96,-48,128,96}, origin 19 up */
static const signed char box_fin[4] = {-8, 6, 16, 16};         /* the swim's fin out of the floor (16 x 16) */
static const signed char box_shark[4] = {-16, -49, 32, 56};    /* {-16,-24,32,56}, origin 25 up */
static const signed char box_shell[4] = {-60, -98, 120, 120};  /* the spinning shell {-60,-60,120,120} (0x800FE834) */
static const signed char box_fire[4] = {-16, -16, 32, 32};
static const signed char box_beam[4] = {-4, -16, 8, 32};
#define X_GRAVITY(p) (*(volatile int *)((p) + 0x2A0))
#define MIJ_LAND 50   /* frames of Mijinion's landing (boss_look land: anim 4 shortened) */
static float move_tx, move_ty;   /* Shark: the leap's point */
static int move_ph, move_ph_t, move_hold;   /* (move_hold: the dash button held - Shark's swim) */
static int move_auto;   /* Shark's giga swim (moves_giga_swim): steers itself at the enemies, no dash button */
#define SWIM_MAX 180   /* Shark's swim at most (BIN 36 sub 7: 180 frames) */
static unsigned rnd = 0x1234567;

static int rand_n(int n)
{
    rnd = rnd * 1103515245u + 12345u;
    return (int)((rnd >> 16) % (unsigned)n);
}
static const signed char box_small[4] = {-6, -6, 12, 12};
static const signed char box_spike[4] = {-5, -15, 10, 30};   /* the floor spike once grown (BIN 34: {-5,-15,10,30}) */
static const signed char box_anchor[4] = {-12, -12, 24, 24};
static const signed char box_ball[4] = {-12, -12, 24, 24};
static const signed char box_ray[4] = {-8, -8, 16, 16};

static int on_ground(int st) { return st == 2 || st == 3 || st == 4 || st == 8; }

/* a wall ahead of Axl going dir: wall bits (1 / 2) where he is now that the spot 8 px behind him does not have (the
 * feet in the floor set wall bits too; against a wall already, comparing with the bits before the step let him go
 * on into it - user report 2026-10-27, the changed bosses got stuck in walls). Both tested WALL_LIFT px higher: on
 * the ground the floor's bits were in both, so a roll started against a wall never saw it and pushed him into the
 * wall until the terrain push-out sank him into the floor and crushed him (Turtloid, 2026-10-28 e2e moves_intro) */
#define WALL_LIFT 6
static int wall_ahead(char *p, int dir)
{
    int now, back;
    PY(p) -= WALL_LIFT;
    now = proj_terrain(p) & 3;
    PX(p) -= dir * 8;
    back = proj_terrain(p) & 3;
    PX(p) += dir * 8;
    PY(p) += WALL_LIFT;
    return (now & ~back) != 0;
}

/* a slope going up met by a move on the ground (user report 2026-10-28: Shark's swim stopped at the foot of an
 * upward slope): up to CLIMB px higher, the first height with no wall ahead and no ceiling, else where it was (0) */
#define CLIMB 8
static int climb(char *p, int dir)
{
    float y0 = PY(p);
    int d;

    if (!on_ground(STATE(p)) || move_k & 0x200)
        return 0;
    for (d = 1; d <= CLIMB; d++) {
        PY(p) = y0 - d;
        if (!wall_ahead(p, dir) && !(proj_terrain(p) & 4))
            return 1;
    }
    PY(p) = y0;
    return 0;
}

/* stuck in the terrain: the terrain box meets walls on both sides or the floor and the ceiling at once */
static int embedded(char *p)
{
    int b = proj_terrain(p) & 0xF;
    return (b & 3) == 3 || (b & 0xC) == 0xC;
}

/* the hidden body hit follows Axl while the move lasts */
static void body_fn(char *w, proj *q)
{
    char *p = axl_p;
    if (!p || (!move_k && !wolf_body)) {
        proj_kill(w);
        return;
    }
    q->life = q->t + 2;
    VX(w) = (int)((PX(p) - PX(w)) * 65536);
    VY(w) = -(int)((PY(p) - PY(w)) * 65536);
}

static void body_on(char *p, int k, const signed char *box)
{
    body = proj_spawn(k, PX(p), PY(p), 0, 0, FACE(p), 0, body_fn, P_PIERCE | P_TOUGH | P_WALL | P_NOSCREEN | P_HIDDEN);
    if (body) {
        proj_boss_look(body, FX_SHOT);
        proj_box(body, box);
    }
}

static char *shot(int k, float x, float y, int vx, int vy, int right, int life, proj_fn fn, int flags, int anim,
                  const signed char *box)
{
    char *w = proj_spawn(k, x, y, vx, vy, right, life, fn, flags);
    if (w) {
        proj_boss_look(w, anim);
        proj_box(w, box);
    }
    return w;
}

static void gravity_fn(char *w, proj *q)
{
    VY(w) -= q->a;
}

/* a shard flung out: random up speed and side */
static void shards(int k, float x, float y, int n)
{
    int i;
    char *s;
    for (i = 0; i < n; i++) {
        s = shot(k, x, y - 4, (i & 1 ? 1 : -1) * FXP(1 + i % 3), FXP(3 + i % 2), i & 1, 60, gravity_fn, 0, FX_SHARD,
                 box_small);
        if (s)
            proj_of(s)->a = 0x3000;
    }
}

/* aim at the nearest enemy within range, else straight ahead */
static void aim(char *p, float x, float y, float speed, int *vx, int *vy)
{
    char *e = find_target(x, y, 240);
    float dx = e ? PX(e) - x : SX(p) * 100.0f, dy = e ? (PY(e) - 16) - y : 0.0f, d = sqrtf(dx * dx + dy * dy);
    if (d < 1)
        d = 1;
    *vx = (int)(dx / d * speed * 65536);
    *vy = -(int)(dy / d * speed * 65536);
}

/* Heatnix's thrown fireball (BIN 35 sub 4): vx = dx * 0x400 (at most 3 px/f), vy -1/16, gravity 0x1000; where it
 * lands (or after a while) 3 shards at 5 px/f, at different angles k * 15 degrees of the upper half */
static void fire_burst(float x, float y)
{
    int used = 0, n = 0, k;
    float a;
    while (n < 3) {
        k = 1 + rand_n(11);
        if (used >> k & 1)
            continue;
        used |= 1 << k;
        n++;
        a = k * 15 * 0.0174533f;
        shot(3, x, y - 8, (int)(cosf(a) * 5 * 65536), (int)(sinf(a) * 5 * 65536), 1, 40, 0, 0, FX_FIREBALL, box_small);
    }
}

static void fireball_fn(char *w, proj *q)
{
    if ((O8(w, 0xB0) & 8) || q->t >= 150) {
        fire_burst(PX(w), PY(w));
        proj_kill(w);
        return;
    }
    VY(w) -= 0x1000;
}

static void fireball(char *p)
{
    float x = PX(p) + SX(p) * 16, y = PY(p) - 24, dx;
    char *e = find_target(x, y, 240);
    int vx;

    dx = e ? PX(e) - x : SX(p) * 96.0f;
    vx = (int)(dx * 0x400);
    vx = vx > FXP(3) ? FXP(3) : vx < FXP(-3) ? FXP(-3) : vx;
    shot(3, x, y, vx, -0x1000, FACE(p), 0, fireball_fn, P_KEEP, FX_FIREBALL, box_fire);
}

/* Mijinion's laser rain (BIN 40 sub 6, at the landing): 2 emitters moving -3 / +3 px/f, each 4 beams 12 frames
 * apart from above the screen, falling 16 px/f ({-4,-16,8,32}) */
static void emitter_fn(char *w, proj *q)
{
    if (q->a > 0 && q->t % 12 == 1) {
        q->a--;
        shot(8, PX(w), PY(w) - 200, 0, FXP(-16), 1, 15, 0, P_PIERCE | P_WALL | P_NOSCREEN, FX_BEAM, box_beam);
    }
}

static void laser_rain(char *p)
{
    int i;
    char *w;
    for (i = 0; i < 2; i++) {
        w = proj_spawn(8, PX(p), PY(p), i ? FXP(3) : FXP(-3), 0, 1, 50, emitter_fn,
                       P_PIERCE | P_TOUGH | P_WALL | P_NOSCREEN | P_HIDDEN);
        if (w) {
            proj_nobox(w);
            proj_of(w)->a = 4;
        }
    }
}

/* Wolfang's floor spikes (BIN 34 type 0x36 sub 0x10; user request 2026-10-27): out of the floor every 32-63 px of
 * the dash, anim 0x1F (lc2_dna.SPIKE_PICS: the burst, then the spike, whose box comes on), 300 frames, then they
 * shatter into 3 shards. Drawn in Wolfang's palettes: gone with the change */
#define SPIKE_LIFE 300
static float spike_x;
static int spike_gap;

static void spike_fn(char *w, proj *q)
{
    int t = q->t, i;
    if (dna_boss != 2) {
        proj_kill(w);
        return;
    }
    if (t >= SPIKE_LIFE - 1) {
        shards(2, PX(w), PY(w), 3);
        proj_kill(w);
        return;
    }
    for (i = 0; i < SPIKE_SEQ_N - 1 && t >= spike_seq[i][0]; i++)
        t -= spike_seq[i][0];
    proj_fx_look(w, SPIKE_BASE + spike_seq[i][1]);
    if (i == SPIKE_SEQ_N - 1 && q->b == 0) {      /* grown: its box */
        q->b = 1;
        proj_box(w, box_spike);
    }
    VX(w) = VY(w) = 0;
}

static void spike(char *p)
{
    char *w = proj_spawn(2, PX(p), PY(p) + 7, 0, 0, 1, SPIKE_LIFE, spike_fn, P_PIERCE | P_TOUGH | P_WALL | P_NOSCREEN);
    if (w) {
        proj_nobox(w);
        proj_fx_look(w, SPIKE_BASE + spike_seq[0][1]);
    }
    spike_x = PX(p);
    spike_gap = 32 + rand_n(32);
}

/* Wolfang's dash afterimages (BIN 34: one every 8 frames): the boss's picture of the moment, blinking out */
static void ghost_fn(char *w, proj *q)
{
    *(volatile float *)(w + 0x1C) = (q->t & 1) ? -10000.0f : q->y;
    VX(w) = VY(w) = 0;
}

static void ghost(char *p)
{
    char *w = proj_spawn(0, PX(p), PY(p), 0, 0, FACE(p), 12, ghost_fn, P_PIERCE | P_TOUGH | P_WALL | P_NOSCREEN);
    if (w) {
        proj_nobox(w);
        if (!dna_ghost(w))
            proj_kill(w);
    }
}

static void clone_fn(char *w, proj *q)
{
    char *e = find_target(PX(w), PY(w), 200);
    float dx, dy, d;
    if (!e)
        return;
    dx = PX(e) - PX(w);
    dy = (PY(e) - 16) - PY(w);
    d = sqrtf(dx * dx + dy * dy);
    if (d < 1)
        return;
    VX(w) += (int)(dx / d * 0x1800) - VX(w) / 16;   /* turns toward the target, about 1.5 px/f */
    VY(w) += -(int)(dy / d * 0x1800) - VY(w) / 16;
}

/* ---- Wolfang's walls and ceiling (user plan 2026-10-27; BIN 34 subs 7 / 9 wall cling + leap, 9 the wall ice, 0xB
 * up to the ceiling and hanging, 0xC the ceiling shots, 0xD the ceiling dash + icicles) ---- */
enum { W_NONE, W_CLING, W_CJUMP, W_CEIL, W_CDASH, W_LEAP };
#define WOLF_CEIL_DY 30   /* Axl's box top on the ceiling -> the hang pictures' top on it (91: 29 + 17 above his origin) */
#define WOLF_WALL_DX 15   /* away from the wall: the cling picture's front (23: 23 px ahead of him) on it */
#define WOLF_LEAP 6.0f    /* px per frame, 20 degrees (BIN 34 FAA08: cos / sin * 0x60) */
#define WB_R 1
#define WB_L 2
#define WB_U 4
#define WB_D 8
#define WB_SHOT 0x20
#define WB_JUMP 0x80
#define W_HELD(p) (*(volatile unsigned short *)((p) + 0xC0))
#define W_NEWP(p) (*(volatile unsigned short *)((p) + 0xC4))
static int wolf_st, wolf_t, wolf_dir;   /* wolf_dir: the wall's side (cling) / the way (leap, ceiling dash) */
static float wolf_y, wolf_ux, wolf_uy;
static float icicle_x;
static int icicle_gap;

/* a wall to cling to: a wall ahead at the feet and 24 px higher too (a slope stops the feet only - user report
 * 2026-10-27, Wolfang tried to cling to slopes) */
static int wall_tall(char *p, int dir)
{
    int high;
    if (!wall_ahead(p, dir))
        return 0;
    PY(p) -= 24;
    high = wall_ahead(p, dir);
    PY(p) += 24;
    return high;
}

static void face(char *p, int dir)
{
    O8(p, 0x29) = (O8(p, 0x29) & ~0x40) | (dir > 0 ? 0x40 : 0);
}

static void wolf_end(char *p)
{
    wolf_st = 0;
    wolf_body = 0;
    GRAVITY(p) = X_GRAVITY(p);
}

static void wolf_leap(char *p, int dir)
{
    wolf_st = W_LEAP;
    wolf_t = 0;
    wolf_dir = dir;
    wolf_ux = dir * 0.9397f;                      /* cos 20 */
    wolf_uy = -0.3420f;                           /* sin 20, up */
    face(p, dir);
    wolf_body = 1;
    body_on(p, 2, box_wolf);
}

static void wolf_cling(char *p, int wall)        /* wall: the wall's side */
{
    wolf_st = W_CLING;
    wolf_t = 0;
    wolf_dir = wall;
    wolf_body = 0;
    PX(p) -= wall * WOLF_WALL_DX;
    wolf_y = PY(p);
    face(p, wall);                                /* (facing the wall: user report 2026-10-27, it was mirrored) */
}

/* the icicle: hangs until a target is below it (|dx| <= 32) or its time is up, falls (gravity 0x4CCC), shatters */
static void icicle_fn(char *w, proj *q)
{
    char *e;
    if (dna_boss != 2) {
        proj_kill(w);
        return;
    }
    proj_fx_look(w, ICICLE_BASE + (q->t >= 6));
    if (q->t == 6)
        proj_box(w, box_spike);
    if (!q->a) {
        VX(w) = VY(w) = 0;
        e = find_target(PX(w), PY(w) + 80, 120);
        if ((e && fabsf(PX(e) - PX(w)) <= 32 && PY(e) > PY(w)) || q->t >= SPIKE_LIFE - 60)
            q->a = 1;
        return;
    }
    VY(w) -= 0x4CCC;
    if ((O8(w, 0xB0) & 8) || q->t >= SPIKE_LIFE) {
        shards(2, PX(w), PY(w), 3);
        proj_kill(w);
    }
}

static void icicle(char *p)
{
    float ceil_y = PY(p) - WOLF_CEIL_DY - 16;     /* (Axl's box top -16 was on the ceiling) */
    char *w = proj_spawn(2, PX(p), ceil_y + 15, 0, 0, 1, SPIKE_LIFE + 60, icicle_fn, P_PIERCE | P_TOUGH | P_KEEP);
    if (w) {
        proj_nobox(w);
        proj_fx_look(w, ICICLE_BASE);
    }
    icicle_x = PX(p);
    icicle_gap = 32 + rand_n(32);
}

/* the wall ice: 2 generators up / down the wall, a wall spike every 8 frames 16 px apart (BIN 34 sub 0x20 / 0x21) */
static const signed char box_wspike[4] = {-15, -5, 30, 10};

static void wspike_fn(char *w, proj *q)
{
    if (dna_boss != 2) {
        proj_kill(w);
        return;
    }
    if (q->t >= SPIKE_LIFE - 1) {
        shards(2, PX(w), PY(w), 3);
        proj_kill(w);
        return;
    }
    proj_fx_look(w, WALL_SPIKE_BASE + (q->t >= 6));
    if (q->t == 6)
        proj_box(w, box_wspike);
    VX(w) = VY(w) = 0;
}

static void wgen_fn(char *w, proj *q)          /* q->a = -1 up / 1 down, q->b = the wall's side */
{
    char *s;
    if (q->t % 8 != 1)
        return;
    s = proj_spawn(2, PX(w), PY(w) + q->a * 16 * (q->t / 8), 0, 0, q->b < 0, SPIKE_LIFE, wspike_fn,
                   P_PIERCE | P_TOUGH | P_WALL | P_NOSCREEN);
    if (s) {
        proj_nobox(s);
        proj_fx_look(s, WALL_SPIKE_BASE);
    }
}

static void wall_ice(char *p)
{
    int i;
    float x = PX(p) + wolf_dir * (WOLF_WALL_DX + 8) - wolf_dir * 15;   /* (the spike's base 15 px off its origin) */
    char *w;
    for (i = 0; i < 2; i++) {
        w = proj_spawn(2, x, PY(p), 0, 0, 1, 8 * 5 + 2, wgen_fn, P_PIERCE | P_TOUGH | P_WALL | P_NOSCREEN | P_HIDDEN);
        if (w) {
            proj_nobox(w);
            proj_of(w)->a = i ? 1 : -1;
            proj_of(w)->b = wolf_dir;
        }
    }
}

/* before X's state code: 1 = Wolfang's own state holds Axl */
static int wolf_input(char *p, int dash_new, int alt_new)
{
    unsigned short held = W_HELD(p), newp = W_NEWP(p), lr = held & (WB_R | WB_L);
    int st = STATE(p), air = st == 6 || st == 7;

    switch (wolf_st) {
    case W_NONE:
        if (move_k)
            return 0;
        if ((newp & WB_JUMP) && (held & WB_U)) {  /* up + jump: up to the ceiling (sub 0xB) */
            wolf_st = W_CJUMP;
            wolf_t = 0;
            return 1;
        }
        if (alt_new && skill_try(SK_SPECIAL)) {   /* the DNA button: the leap (sub 9; cooldown, axl_skills.c) */
            wolf_leap(p, SX(p));
            return 1;
        }
        if (air && (lr == WB_R || lr == WB_L) && wall_tall(p, lr == WB_R ? 1 : -1)) {   /* a wall: the cling */
            wolf_cling(p, lr == WB_R ? 1 : -1);
            return 1;
        }
        return 0;
    case W_CLING:
        if (newp & WB_JUMP)
            wolf_leap(p, -wolf_dir);              /* off the wall, away from it */
        else if ((held & WB_D) || lr == (wolf_dir > 0 ? WB_L : WB_R))   /* down, or away from the wall: he lets go
                                                   * and falls (user decision 2026-10-28, as X's cling) */
            wolf_end(p);
        else if (alt_new && skill_try(SK_SPECIAL))   /* (the special's cooldown as the leap's) */
            wall_ice(p);
        return wolf_st != 0;
    case W_CEIL:
        if (lr == WB_R || lr == WB_L)
            face(p, lr == WB_R ? 1 : -1);
        if ((newp & WB_SHOT) && !seq_n) {         /* the ceiling shots (sub 0xC): 4 aimed, 7 px/f */
            seq_k = 2;
            seq_n = 4;
            seq_t = 18;
        } else if (dash_new) {                    /* the ceiling dash (sub 0xD) */
            wolf_st = W_CDASH;
            wolf_t = 0;
            wolf_dir = SX(p);
            wolf_body = 1;
            body_on(p, 2, box_wolf);
            icicle(p);
        } else if ((held & WB_D) || (newp & WB_JUMP))
            wolf_end(p);
        return wolf_st != 0;
    default:
        return 1;
    }
}

/* after X's state code */
static void wolf_after(char *p)
{
    int t = wolf_t++;

    VY(p) = 0;
    GRAVITY(p) = 0;
    switch (wolf_st) {
    case W_CLING:
    case W_CEIL:
        PY(p) = wolf_y;
        break;
    case W_CJUMP:                                 /* straight up at 6 px/f to the ceiling (none in 60 frames: falls) */
        PY(p) -= 6;
        if (proj_terrain(p) & 4) {
            PY(p) += 6;
            while (!(proj_terrain(p) & 4) && t < 200)
                PY(p) -= 1, t++;
            PY(p) += 1 + WOLF_CEIL_DY;
            wolf_y = PY(p);
            wolf_st = W_CEIL;
            wolf_t = 0;
        } else if (t >= 60)
            wolf_end(p);
        break;
    case W_CDASH:                                 /* along the ceiling at 6 px/f, an icicle every 32-63 px */
        PY(p) = wolf_y;
        PX(p) += wolf_dir * 6;
        if (wall_ahead(p, wolf_dir)) {
            PX(p) -= wolf_dir * 6;
            if (wall_tall(p, wolf_dir))
                wolf_cling(p, wolf_dir);          /* a wall: onto it */
            else
                wolf_st = W_CEIL;                 /* (a low step on the ceiling: the dash stops) */
            break;
        }
        if (fabsf(PX(p) - icicle_x) >= icicle_gap)
            icicle(p);
        if (t >= 60) {
            wolf_st = W_CEIL;
            wolf_body = 0;
        }
        break;
    case W_LEAP:                                  /* 20 degrees up at 6 px/f: a wall -> cling, the ceiling -> hang */
        PX(p) += wolf_ux * WOLF_LEAP;
        if (wall_ahead(p, wolf_dir)) {
            PX(p) -= wolf_ux * WOLF_LEAP;
            if (wall_tall(p, wolf_dir))
                wolf_cling(p, wolf_dir);
            else
                wolf_end(p);                      /* (a slope / step: the leap ends, he falls) */
            break;
        }
        PY(p) += wolf_uy * WOLF_LEAP;
        if (proj_terrain(p) & 4) {
            PY(p) -= wolf_uy * WOLF_LEAP;
            PY(p) += WOLF_CEIL_DY;
            wolf_y = PY(p);
            wolf_st = W_CEIL;
            wolf_t = 0;
            wolf_body = 0;
        } else if (t >= 40)
            wolf_end(p);
        break;
    }
}

/* the change starts / ends / the player dies: no move, second attack or Wolfang state carried over (user report
 * 2026-10-27: dead mid-move, the next change shot him up into the ceiling) */
void moves_reset(char *p)
{
    /* (any move that took the gravity away: Turtloid's bounce, Shark's leap - cut short by the end of the change they
     * left Axl hanging in his fall pose, user report 2026-10-28; a flyer's flight takes it again, axl_x.c) */
    if (p && X_GRAVITY(p) && (wolf_st || move_k || !GRAVITY(p)))
        GRAVITY(p) = X_GRAVITY(p);
    move_k = 0;
    seq_n = 0;
    seq_t = 0;
    wolf_st = 0;
    wolf_body = 0;
    move_auto = 0;
    skill_reset();   /* (every skill ready at the change's start: user decision 2026-11-03) */
    dna_giga_reset();
}

/* one step of a move done outside here (axl_x.c's roll): along x, up a slope, stopped by a wall. 1 = moved */
int moves_step(char *p, float dx)
{
    int dir = dx > 0 ? 1 : -1;
    PX(p) += dx;
    if (wall_ahead(p, dir) && !climb(p, dir)) {
        PX(p) -= dx;
        return 0;
    }
    return 1;
}

int moves_invincible(void)
{
    return move_k && move_inv;
}

/* which look the boss shows (axl_dna.c dna_look): 1 its dash-button move, 2 its second attack (the shots of the DNA
 * button, Heatnix's dive, Turtloid's bounce), 0 neither */
int moves_look(void)
{
    if (wolf_st)                                  /* Wolfang's walls and ceiling (axl_dna.c dna_look: 4-9) */
        return wolf_st == W_CLING ? 4 : wolf_st == W_CEIL ? (seq_n ? 6 : 5) : wolf_st == W_CDASH ? 7 :
               wolf_st == W_LEAP ? 8 : 9;
    if (move_k & 0x800)                           /* (a move's landing: Mijinion's drop) */
        return 3;
    if (move_k & 0x300 || seq_n)
        return 2;
    return move_k ? 1 : 0;
}

static void move_start(char *p, int k)
{
    move_k = k;
    move_t = 0;
    move_inv = 0;
    move_auto = 0;
    move_dir = SX(p);
    move_vy = 0;
    switch (k) {
    case 2: move_len = 24; move_v = 6; body_on(p, 2, box_wolf); break;     /* sub 8: contact 6, picture 25's box */
    case 3: move_len = 40; move_v = 8; move_inv = 1; body_on(p, 3, box_bird); break;   /* sub 8 firebird: +-8, invincible */
    case 4: move_len = SWIM_MAX; move_v = 0; move_inv = 1; body_on(p, 4, box_fin); break;   /* sub 7 junk swim */
    case 5: move_len = 30; move_v = 5; body_on(p, 5, box_body); break;
    case 6: move_len = 60; move_v = 1; move_inv = 1; body_on(p, 6, box_shell); break;
    case 7: move_len = 2 * TELE_AT + 1; move_v = 0; move_inv = 1; break;
    case 8: move_len = 60; move_v = 0; move_vy = -9; body_on(p, 8, box_body); break;   /* sub 6 drop: 9 px/f */
    default: move_k = 0; return;
    }
    if (test_on)
        test_log("move %d start", k);
}

/* Shark's giga (axl_dna_bosses.c, BIN 36 sub 7): the junk swim for len frames, steering itself at the nearest enemy
 * (passing it, back again), invincible, the fin's body hit with the giga's damage id (a P_GIGA projectile). 0 = not
 * now (in the air, a move on) */
int moves_giga_swim(char *p, int id, int len)
{
    if (move_k || wolf_st || !on_ground(STATE(p)))
        return 0;
    move_start(p, 4);
    if (body)
        proj_kill(body);
    body_on(p, id, box_fin);
    if (body)
        proj_of(body)->flags |= P_GIGA;
    move_len = len;
    move_auto = 1;
    return 1;
}

/* before X's state code (changed, pose over). Returns 1 while a move holds Axl. */
int moves_before(char *p, int dash_new, int dash_held, int alt_new)
{
    int k = dna_boss;
    float x, y;
    int vx, vy;

    if (!k || dna_pose) {
        move_k = 0;
        if (wolf_st)
            wolf_end(p);
        return 0;
    }
    if (k != 2 && wolf_st)
        wolf_end(p);
    if (k == 2 && wolf_input(p, dash_new, alt_new)) {
        if (seq_n && seq_t-- <= 0) {              /* (the ceiling shots, from the hang) */
            int vx, vy;
            float x = PX(p) + SX(p) * 24, y = PY(p) - 8;
            aim(p, x, y, 7, &vx, &vy);
            shot(2, x, y, vx, vy, FACE(p), 120, 0, 0, FX_SHARD, box_small);
            seq_t = 8;
            seq_n--;
        }
        return 1;
    }
    /* (the dash and the second attack wait out their cooldowns, axl_skills.c: user decision 2026-11-02) */
    if (!move_k && dash_new && k != 1 && !(k == 4 && !on_ground(STATE(p))) && !(k == 8 && on_ground(STATE(p))) &&
        skill_try(SK_DASH))
        move_start(p, k);
    if (alt_new && k != 1 && k != 2 && k != 7 && !seq_n && !((k == 4 || k == 6) && move_k) && skill_try(SK_SPECIAL)) {
        /* the second attack (Wolfang's: wolf_input; Shark's leap and Turtloid's bounce not during a move) */
        seq_k = k;
        seq_t = 0;
        seq_n = k == 2 ? 4 : k == 8 ? 3 : k == 5 ? 3 : k == 4 ? 0 : 1;
        if (k == 3)
            seq_t = 26;                           /* Heatnix's throw: the fireball at t = 26 of anim 5 */
        if (k == 4 && !move_k) {                  /* Shark's leap and pound (sub 6): to the target's x, 0x50 above */
            char *e = find_target(PX(p), PY(p), 240);
            move_k = 4 | 0x100;
            move_t = move_ph = 0;
            move_len = 200;
            move_inv = 0;
            move_tx = e ? PX(e) : PX(p) + SX(p) * 64;
            move_ty = (e ? PY(e) : PY(p)) - 0x50;
            move_dir = move_tx >= PX(p) ? 1 : -1;
        }
        if (k == 6 && !move_k) {                  /* Turtloid's shell bounce */
            move_k = 6 | 0x200;
            move_t = 0;
            move_len = 200;
            move_inv = 1;
            move_dir = SX(p);
            move_v = 3;
            move_vy = 3;
            bounces = 0;
            body_on(p, 6, box_shell);
        }
        if (test_on)
            test_log("second attack %d", k);
    }
    if (seq_n && seq_t-- <= 0) {
        x = PX(p) + SX(p) * 24;
        y = PY(p) - 24;
        switch (seq_k) {
        case 2: aim(p, x, y, 7, &vx, &vy); shot(2, x, y, vx, vy, FACE(p), 120, 0, 0, FX_SHARD, box_small); seq_t = 8; break;
        case 3: fireball(p); break;
        case 5: shot(5, x, y + 12, SX(p) * FXP(3), 0, FACE(p), 120, 0, 0, FX_BALL, box_ball); seq_t = 10; break;
        case 8: shot(8, PX(p), PY(p) - 30 - 12 * seq_n, SX(p) * FXP(1), FXP(1), FACE(p), 180, clone_fn, P_PIERCE, FX_CLONE,
                     box_ray); seq_t = 6; break;
        }
        seq_n--;
    }
    move_hold = dash_held;
    if (move_k == 4 && move_auto) {               /* the giga swim: at the nearest enemy, 5 px/f */
        char *e = find_target(PX(p), PY(p), 240);
        move_v = 5;
        if (e && fabsf(PX(e) - PX(p)) > 24)       /* (over it: on, then back - it passes the target as the boss's) */
            move_dir = PX(e) > PX(p) ? 1 : -1;
    } else if (move_k == 4) {                     /* the swim: left / right steer it (5 px/f), still without */
        unsigned short lr = *(volatile unsigned short *)(p + 0xC0) & 3;   /* (HELD: 1 right, 2 left) */
        move_v = lr == 1 || lr == 2 ? 5 : 0;
        if (lr == 1 || lr == 2)
            move_dir = lr == 1 ? 1 : -1;
    }
    if (!move_k)
        return 0;
    O8(p, 0x29) = (O8(p, 0x29) & ~0x40) | (move_dir > 0 ? 0x40 : 0);
    return 1;
}

/* after X's state code: the move's own motion */
void moves_after(char *p)
{
    int k = move_k & 0xff, t, before;
    float dx, dy, step;
    char *s;

    if (wolf_st && dna_boss == 2) {
        wolf_after(p);
        return;
    }
    if (!move_k || !dna_boss) {
        move_k = 0;
        return;
    }
    t = move_t++;
    if (move_k & 0x800) {                         /* Mijinion's landing: held while anim 4 plays */
        if (move_t >= move_len)
            move_k = 0;
        return;
    }
    if (move_k == (4 | 0x100)) {                  /* Shark's leap and pound */
        VY(p) = 0;
        GRAVITY(p) = 0;
        if (move_ph == 0) {                       /* straight at 8 px/f to the point, no gravity */
            float ddx = move_tx - PX(p), ddy = move_ty - PY(p), d = sqrtf(ddx * ddx + ddy * ddy);
            if (d <= 8 || t >= 45) {
                move_ph = 1;
                move_ph_t = t;
            } else {
                PX(p) += ddx / d * 8;
                if (wall_ahead(p, ddx > 0 ? 1 : -1))
                    PX(p) -= ddx / d * 8;
                PY(p) += ddy / d * 8;
                if (ddy < 0 && (proj_terrain(p) & 4))   /* (a ceiling) */
                    PY(p) -= ddy / d * 8;
            }
        } else if (move_ph == 1) {                /* hovers 30 frames */
            if (t - move_ph_t >= 30) {
                move_ph = 2;
                body_on(p, 4, box_shark);         /* (the pound: damage 8 in the original) */
            }
        } else {                                  /* the pound: 5 px/f down to the floor */
            PY(p) += 5;
            if (proj_terrain(p) & 8) {
                PY(p) -= 5;
                while (!(proj_terrain(p) & 8) && PY(p) < 100000)
                    PY(p) += 1;
                move_k = 0;
                GRAVITY(p) = X_GRAVITY(p);
                if (test_on)
                    test_log("shark pound landed");
                return;
            }
        }
        if (move_t >= move_len) {
            move_k = 0;
            GRAVITY(p) = X_GRAVITY(p);
        }
        return;
    }
    if (move_k == 4 && !move_hold && !move_auto && t > 8) {     /* the swim: out when the dash button is let go */
        move_k = 0;
        return;
    }
    if (k == 2 && t % 8 == 4)
        ghost(p);
    if (k == 7 && t < TELE_AT) {                  /* teleport (user request 2026-10-27: its look): out, */
        return;
    }
    if (k == 7 && t > TELE_AT) {                  /* in again */
        if (t >= 2 * TELE_AT)
            move_k = 0;
        return;
    }
    if (k == 7) {                                 /* the warp: 8 px steps forward while there is room */
        int ground = on_ground(STATE(p)), d;
        float y0 = PY(p);
        for (step = 0; step < 80; step += 8) {
            PX(p) += move_dir * 8;
            if (wall_ahead(p, move_dir)) {
                PX(p) -= move_dir * 8;
                break;
            }
        }
        /* on the floor where he lands (2026-10-28 e2e: the warp kept his height - a floor 3 px lower left him falling
         * a few frames, a jump pressed then was lost; a higher one would have put him in it): within 16 px, the
         * nearest height whose terrain box meets the floor (bit 8) and neither the ceiling nor both walls */
        if (ground)
            for (d = 0; d <= 16; d++) {
                int b;
                PY(p) = y0 + d;
                b = proj_terrain(p) & 0xF;
                if ((b & 8) && !(b & 4) && (b & 3) != 3)
                    break;
                PY(p) = y0 - d;
                b = proj_terrain(p) & 0xF;
                if ((b & 8) && !(b & 4) && (b & 3) != 3)
                    break;
                PY(p) = y0;
            }
        if (test_on)
            test_log("move 7 teleport %.0f px", step);
        return;
    }
    if (k == 6 && !(move_k & 0x200) && move_v < 5)
        move_v += 1;                              /* the roll speeds up */
    dx = move_dir * move_v;
    dy = -move_vy;
    if (move_k & 0x200) {                         /* shell bounce: falls, bounces on the floor 3 times */
        move_vy -= 0.25f;
        dy = -move_vy;
    }
    PX(p) += dx;
    if (dx && embedded(p) && !(move_k == 4)) {   /* (a step into the terrain: back, the move over - never a crush) */
        PX(p) -= dx;
        if (test_on)
            test_log("move %d embedded at t %d: stopped", k, t);
        move_k = 0;
        return;
    }
    if (dx && wall_ahead(p, dx > 0 ? 1 : -1) && !climb(p, dx > 0 ? 1 : -1)) {
        if (test_on)
            test_log("move %d wall at t %d", k, t);
        PX(p) -= dx;
        if (move_k == 4) {                        /* (the swim stops at a wall, stays under; the giga's turns) */
            dx = 0;
            if (move_auto)
                move_dir = -move_dir;
        }
        else if (!(move_k & 0x200)) {
            move_k = 0;                           /* a wall ends the move */
            return;
        } else
            move_dir = -move_dir;
    }
    if (dy) {
        PY(p) += dy;
        VY(p) = 0;
        GRAVITY(p) = 0;
        if (dy > 0 && (proj_terrain(p) & 8)) {    /* lands */
            PY(p) -= dy;
            while (dy > 1 && !(proj_terrain(p) & 8)) {   /* settle on the floor */
                PY(p) += 1;
                dy -= 1;
            }
            if (move_k & 0x200 && ++bounces < 3) {
                move_vy = 3;
            } else if (k == 8) {                  /* Mijinion: the landing (anim 4) and the laser rain */
                move_k = 8 | 0x800;
                move_len = move_t + MIJ_LAND;
                laser_rain(p);
                return;
            } else {
                move_k = 0;
                GRAVITY(p) = X_GRAVITY(p);
                if (test_on)
                    test_log("move %d landed", k);
                return;
            }
        }
    }
    if (k == 2 && (t == 0 || fabsf(PX(p) - spike_x) >= spike_gap))   /* Wolfang: floor spikes left behind */
        spike(p);
    if (k == 6 && !(move_k & 0x200) && t % 30 == 15) {   /* Turtloid rolling: rain */
        s = proj_spawn(6, PX(p), PY(p) - 32, 0, FXP(8), move_dir > 0, 200, t_rain, P_PIERCE | P_WALL | P_NOSCREEN);
        if (s) {
            proj_boss_look(s, FX_RAIN);
            proj_nobox(s);
        }
    }
    if (move_t >= move_len) {
        move_k = 0;
        if (dy)                                   /* (a move with its own fall: the gravity back) */
            GRAVITY(p) = X_GRAVITY(p);
        if (test_on)
            test_log("move %d end x %.0f y %.0f", k, PX(p), PY(p));
    }
}
