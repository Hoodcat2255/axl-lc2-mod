/* Ground Scaravich's kit while changed (user design 2026-11-14: "처음 변신했을 때, 작은 구체를 굴리고 걸을 수록 구체가
 * 커지게 ... 한 방향으로만 움직일 수 있고 대시키로 구체 반대로 굴러서 방향을 전환하고 공격키로 구체를 굴리는거야. 클수록
 * 데미지가 크고 굴리는 구체에 닿아도 적들 데미지를 입고. 특수공격은 손에서 탄환을 발사 ... 기가어택은 단일적을 대상으로한
 * 지폭천성"; then: the next ball drops from above a moment after a kick, the game pauses while the ball grows, the
 * distance walked grows it (back and forth in a boss room too), a jump keeps the ball with him, the giga fires the
 * ball's core and plays the crush as a cut-scene).
 *
 * The ball (sizes 1-4: the original's 46 / 78 / 110 / 150 px pictures, lc2_dna.scar_looks frames SC_BALL..) stands on the floor
 * in front of him (at the original's distance: sc_ahead; its bottom at his feet; in the air it keeps that place).
 * Held: he walks only the way he faces (the other way's button masked), slower as it grows (sc_walk); every
 * SC_GROW_PX px walked on the ground it grows a size while the world stands still SC_FREEZE frames (axl_menu.c
 * world_freeze); a wall in front of it stops him. It hits what it touches (X's Ground Dash id, again every REHIT
 * frames), the loss scaled by its size (scar_hit_loss: x1, 1.5, 2, 4 - the original's damage 4 / 6 / 8 / 16).
 * The dash: he rolls up (his anim 4 pictures) behind the ball to its other side and turns round.
 * The shot: his kick (anim 3, the ball sent at its frame 23: axl_dna.c atk) rolls it away along the floor at
 * sc_roll_v[size] px/f; a wall breaks it, off the screen it is gone. SC_RESPAWN frames later the next size-1 ball
 * falls in front of him from above the screen. With no ball he walks both ways and the dash is the rolled-up charge
 * (axl_moves.c move 5).
 * The special: his fist's purple orb (anim 2; BIN 37 pictures 28-31), aimed at the nearest enemy.
 * The giga: he kicks the ball's core (anim 3) at the nearest enemy, homing; on its hit the world stands still for a
 * cut-scene (about 10 s, user design 2026-11-16): dark bars slide in, the target rises, rock chunks pile on a ball
 * growing round it to full size, pressed smaller and shaking, "Chibaku Tensei" - then
 * the game's explosions on it: a common enemy dies, a boss loses CG_BOSS_DMG hit points (down to 1 at most) and falls
 * back; the rocks fly out and the bars leave.
 */
#include <windows.h>
#include <math.h>

#include "axl_lc2.h"
#include "axl_proj.h"
#include "axl_data.h"      /* the game-derived tables (axl_data.bin) */
#include "axl_test.h"

#define O8(p, o) (*(volatile unsigned char *)((p) + (o)))
#define O16(p, o) (*(volatile unsigned short *)((p) + (o)))
#define O32(p, o) (*(volatile unsigned *)((p) + (o)))
#define PX(p) (*(volatile float *)((p) + 0x14))
#define PY(p) (*(volatile float *)((p) + 0x1C))
#define VX(w) (*(volatile int *)((w) + 0x3C))
#define VY(w) (*(volatile int *)((w) + 0x40))
#define FACE(p) ((O8(p, 0x29) & 0x40) != 0)
#define SX(p) (FACE(p) ? 1 : -1)
#define STATE(p) O8(p, 0x0D)
#define HELD(p) O16(p, 0xC0)
#define NEWP(p) O16(p, 0xC4)
#define B_RIGHT 0x1
#define B_LEFT 0x2
#define CAM(i) (*(char **)0x041D8CD0 + 0xA0A8 + 0xB4 * (i))   /* the game's cameras: x +0x14, y +0x1C = the view's
                                                                  * top left (axl_dna_bosses.c MS_CAM) */
void world_freeze(int frames, void (*fn)(int t));   /* axl_menu.c */

#define SC_ID 5            /* X's Ground Dash: the boss's weapon's damage id */
#define GID_SCAR 0x0E      /* its charged id (the giga's) */
#define KILL_ID 0x16       /* (axl_dna_bosses.c MS_KILL_ID) */
#define SC_FEET 22         /* Axl's feet below his origin */
#define SC_GROW_PX 160
#define SC_FREEZE 24
#define SC_RESPAWN 30
#define SC_FALL_G 0x3000   /* the dropped ball's gravity (16.16 px/f per frame), at most SC_FALL_MAX */
#define SC_FALL_MAX 0x80000
#define SC_ROLLED 4
#define SC_TURN_LEN 8      /* frames of the dash's turn with no ball */

static const int sc_r[5] = {0, 23, 39, 55, 76};   /* the pictures' radii (46 / 78 / 110 / 150 px) */
static const signed char sc_box[5][4] = {{0}, {-20, -20, 40, 40}, {-34, -34, 68, 68}, {-48, -48, 96, 96},
                                         {-63, -63, 127, 127}};
static const float sc_walk[5] = {0, 1.0f, 0.9f, 0.8f, 0.65f};
static const float sc_roll_v[5] = {0, 4.0f, 3.5f, 3.0f, 2.5f};
static const unsigned char sc_mul2[5] = {0, 2, 3, 4, 8};   /* the loss x sc_mul2 / 2 */
static const signed char box_orb[4] = {-6, -6, 12, 12};
static const signed char box_crush[4] = {-24, -24, 48, 48};

enum { SC_NONE, SC_DROP, SC_HELD };
static int sc_st, sc_size = 1, sc_wait, sc_first = 1, sc_flip, sc_flip_len, sc_flip_dir, sc_grow_from;
static float sc_dist, sc_roll, sc_x0, sc_y0, sc_flip_step;
static int sc_push;                   /* this frame's walk button the way he faces (scar_before) */
static char *sc_ball;                 /* the ball dropping or held */
static char *sc_rolled[SC_ROLLED];    /* balls kicked away */
int scar_grows, scar_kicks, scar_flips, scar_orbs, scar_crushes;   /* test */

static int on_ground(int st) { return st == 2 || st == 3 || st == 4 || st == 8; }

static proj *ours(char *w, proj_fn fn)   /* w still one of ours with that behaviour (else 0) */
{
    proj *q = w ? proj_of(w) : 0;
    return q && q->w == w && q->fn == fn ? q : 0;
}

/* the ball's picture: size s, turned by the distance rolled */
#define SC_LAYER 3   /* draw layer W+0x2A (smaller drawn over: X's shot 1, the player 2): the ball behind him - his
                      * feet over it (user report 2026-11-14: "스카라비치가 앞에 그려져야해. 발이 가려져") */
static void sc_look(char *w, proj *q, int s, float roll)
{
    int i = (int)floorf(roll / (sc_r[s] * 0.5f)), f = SC_BALL + SC_TURN * (s - 1) + ((i % SC_TURN) + SC_TURN) % SC_TURN;
    O8(w, 0x2A) = SC_LAYER;
    if (q->c != f) {
        q->c = f;
        proj_pose_look(w, SCAR_TEX_INDEX, SCAR_PAT, f);
        O8(w, 0x66) = (unsigned char)f;   /* (also while the world stands still: no anim step then) */
    }
}

/* the ball's centre ahead of his origin: the original's own (BIN 37 size table 0x800FDC18: dx 0x1C / 0x2A / 0x30 /
 * 0x2E, the ball held at boss x -+ dx; docs/boss_attacks.md) - the big ones pressed into his front, drawn behind him
 * (user report 2026-11-14: the widest point at his front left a gap at his fists, about 50 px with size 4) */
static const unsigned char sc_dx[5] = {0, 0x1C, 0x2A, 0x30, 0x2E};

static float sc_ahead(int s)
{
    return (float)sc_dx[s];
}

/* in the air (his jump picture, BIN 37 picture 25: his fists 20 px ahead of his origin, 2 above it) the ball hangs
 * on his fists - its centre SC_AIR_IN of its radius in from them, ahead and up (user request 2026-11-15: "점프 했을 때,
 * 공 위치랑 더 붙어있으면 해. 메달려있는거 처럼"; on the floor's place it stood off his body there) */
#define SC_AIR_FIST_X 16
#define SC_AIR_FIST_Y 5     /* (his look 7 px lower than Axl's origin: feet +15 vs +22) */
#define SC_AIR_IN 0.6f
static int sc_in_air(char *p)
{
    int st = STATE(p);
    return st == 6 || st == 7;
}

static int sc_wall(char *w, int dir, int s);
#define SC_SQUEEZE 4   /* px a step the ball is pulled in from a wall */

static void sc_place(char *p, char *w, int s)
{
    if (sc_in_air(p)) {
        float k = sc_r[s] * SC_AIR_IN * 0.7071f;
        PX(w) = PX(p) + SX(p) * (SC_AIR_FIST_X + k);
        PY(w) = PY(p) + SC_AIR_FIST_Y - k;
        VX(w) = VY(w) = 0;
        O8(w, 0x2A) = SC_LAYER;
        return;
    }
    PX(w) = PX(p) + SX(p) * sc_ahead(s);
    PY(w) = PY(p) + SC_FEET - sc_r[s];
    VX(w) = VY(w) = 0;
    O8(w, 0x2A) = SC_LAYER;
    {   /* a wall or a door at its front: the ball pulled in against his body (drawn behind him), he walks on to it
         * himself - a door opens at his touch (user report 2026-11-15: "공 때문에 문에 못들어가는거 같아"; the ball
         * stopped him short of it); at most to his origin */
        float in;
        for (in = 0; in < sc_ahead(s) && sc_wall(w, SX(p), s); in += SC_SQUEEZE)
            PX(w) -= SX(p) * SC_SQUEEZE;
    }
}

/* a wall ahead of the ball going dir: terrain at its front probe (sc_front) */
/* the terrain at the ball's front: an SC_PROBE px square on its front edge at its centre's height (its own box for
 * the terrain test meanwhile). The whole ball's box read the floor's bumps and slopes and low ceilings as walls -
 * a size-1 ball stopped on uneven floor, a size-4 one under a ceiling (play log 2026-11-15: 'scar push blocked (the
 * ball's wall test)', his own height unchanged) */
#define SC_PROBE 8
static int sc_front(char *w, int dir, int s)
{
    static signed char probe[4];
    int bits;
    probe[0] = (signed char)(dir > 0 ? sc_r[s] - SC_PROBE : -sc_r[s]);
    probe[1] = -SC_PROBE / 2;
    probe[2] = probe[3] = SC_PROBE;
    proj_box(w, probe);
    bits = proj_terrain(w) & 0xF;
    proj_box(w, sc_box[s]);
    return bits;
}

static int sc_wall(char *w, int dir, int s)
{
    return sc_front(w, dir, s) != 0;
}

static void sc_held_fn(char *w, proj *q)
{
    (void)q;
    VX(w) = VY(w) = 0;
}

static void sc_drop_fn(char *w, proj *q)
{
    (void)q;
    VX(w) = 0;
    if (VY(w) > -SC_FALL_MAX)
        VY(w) -= SC_FALL_G;
}

static void sc_roll_fn(char *w, proj *q)
{
    int s = q->b, floor, n;
    char *cam;
    float y = PY(w);

    if (dna_boss != 5) {
        proj_kill(w);
        return;
    }
    if (q->t > 2 && sc_wall(w, q->a, s)) {          /* a wall breaks it */
        if (test_on)
            test_log("scar ball size %d breaks on a wall at %.0f", s, PX(w));
        axl_sound(SND_SCAR_ROLL);
        proj_spark(w);
        proj_kill(w);
        return;
    }
    cam = *(char **)0x041D8CD0 ? CAM(0) : 0;     /* off the screen: gone */
    if (cam && (PX(w) - *(volatile float *)(cam + 0x14) < -sc_r[s] - 16 ||
                PX(w) - *(volatile float *)(cam + 0x14) > 320 + sc_r[s] + 16)) {
        proj_kill(w);
        return;
    }
    floor = proj_terrain(w) & 8;
    for (n = 0; floor && n < 8 && (proj_terrain(w) & 8); n++)
        PY(w) -= 1;                               /* (out of the floor: its bottom on it) */
    if (n)
        PY(w) += 1;
    (void)y;
    VX(w) = (int)(q->a * sc_roll_v[s] * 65536);
    VY(w) = floor ? 0 : VY(w) - 0x1400;           /* on the floor, falling off ledges */
    q->tx += sc_roll_v[s];
    sc_look(w, q, s, q->a > 0 ? q->tx : -q->tx);
}

/* kicked at an enemy (user request 2026-11-15: "찼을 때, 적에게 날아가도록 해야해"): straight at the point aimed at
 * when kicked (the nearest enemy ahead, objs_aim_point), sc_fly_v[size] px/f, no gravity, through the enemies; the
 * terrain at its centre (sc_front's probe there) breaks it, off the screen it is gone */
static const float sc_fly_v[5] = {0, 6.0f, 5.5f, 5.0f, 4.5f};
#define SC_KICK_RANGE 320

static void sc_fly_fn(char *w, proj *q)
{
    int s = q->b;
    char *cam;
    static signed char probe[4] = {-SC_PROBE / 2, -SC_PROBE / 2, SC_PROBE, SC_PROBE};

    if (dna_boss != 5) {
        proj_kill(w);
        return;
    }
    if (q->t > 4) {
        int bits;
        proj_box(w, probe);
        bits = proj_terrain(w) & 0xF;
        proj_box(w, sc_box[s]);
        if (bits) {
            if (test_on)
                test_log("scar ball size %d (kicked at an enemy) breaks on the terrain (%x) at %.0f %.0f", s, bits,
                         PX(w), PY(w));
            axl_sound(SND_SCAR_ROLL);
            proj_spark(w);
            proj_kill(w);
            return;
        }
    }
    cam = *(char **)0x041D8CD0 ? CAM(0) : 0;
    if (cam) {
        float sx = PX(w) - *(volatile float *)(cam + 0x14), sy = PY(w) - *(volatile float *)(cam + 0x1C);
        if (sx < -sc_r[s] - 16 || sx > 320 + sc_r[s] + 16 || sy < -sc_r[s] - 16 || sy > 240 + sc_r[s] + 16) {
            proj_kill(w);
            return;
        }
    }
    VX(w) = q->vx;
    VY(w) = q->vy;
    q->tx += sc_fly_v[s];
    sc_look(w, q, s, q->a > 0 ? q->tx : -q->tx);
}

static char *sc_spawn(int s, float x, float y, proj_fn fn)
{
    char *w = proj_spawn(SC_ID, x, y, 0, 0, 0, 0, fn, P_PIERCE | P_TOUGH | P_WALL | P_NOSCREEN);
    if (w) {
        proj *q = proj_of(w);
        q->b = s;
        q->c = -1;
        proj_box(w, sc_box[s]);
        sc_look(w, q, s, 0);
    }
    return w;
}

/* the change starts / ends (axl_moves.c moves_reset) */
void scar_reset(void)
{
    sc_st = SC_NONE;
    sc_size = 1;
    sc_wait = 0;
    sc_first = 1;
    sc_flip = 0;
    sc_dist = sc_roll = 0;
    sc_ball = 0;
    memset(sc_rolled, 0, sizeof sc_rolled);
}

int scar_has_ball(void)
{
    return dna_boss == 5 && sc_st == SC_HELD && !sc_flip;
}

int scar_waiting(void)
{
    return dna_boss == 5 && sc_st != SC_HELD && !sc_flip;
}

int scar_flipping(void)
{
    return dna_boss == 5 && sc_flip;
}

/* before X's state code (axl_moves.c moves_before, changed into Scaravich): the held ball's way only; the dash rolls
 * him round it. Returns 1 while he rolls (the input is his). */
int scar_before(char *p, int *dash_new)
{
    sc_x0 = PX(p);
    sc_y0 = PY(p);
    sc_push = (HELD(p) & (FACE(p) ? B_RIGHT : B_LEFT)) != 0;
    if (sc_flip)
        return 1;
    /* only the dash turns him (user request 2026-11-15: "대시로만 방향전환이 됐으면 해. 공이 없으면 이동이 불가하게.
     * 공중에서 대시해도 기존 대시 기능이 아닌거로"): the other way's button masked always, both with no ball held; the
     * dash - on the floor or in the air - rolls him round the ball, with none turns him where he is (no charge) */
    HELD(p) &= ~(FACE(p) ? B_LEFT : B_RIGHT);
    NEWP(p) &= ~(FACE(p) ? B_LEFT : B_RIGHT);
    if (sc_st != SC_HELD) {
        HELD(p) &= ~(B_LEFT | B_RIGHT);
        NEWP(p) &= ~(B_LEFT | B_RIGHT);
    }
    if (!*dash_new)
        return 0;
    *dash_new = 0;
    if (moves_look() != 0)
        return 0;
    sc_flip = 1;
    sc_flip_dir = SX(p);
    if (sc_st == SC_HELD && ours(sc_ball, sc_held_fn)) {
        sc_flip_len = 12 + 3 * sc_size;
        sc_flip_step = 2.0f * fabsf(PX(sc_ball) - PX(p)) / sc_flip_len;   /* (to the same place on its other side) */
    } else {
        sc_flip_len = SC_TURN_LEN;
        sc_flip_step = 0;
    }
    scar_flips++;
    axl_sound(SND_SCAR_ROLL);
    if (test_on)
        test_log("scar flip size %d%s: %d frames, %.1f px/f", sc_size, sc_st == SC_HELD ? "" : " (no ball: a turn)",
                 sc_flip_len, sc_flip_step);
    return 1;
}

/* the world stands still while the ball grows: it swells, the old size and the new one in turn */
static void sc_grow_tick(int t)
{
    char *p = axl_p, *w = sc_ball;
    proj *q = ours(w, sc_held_fn);
    int s = t >= SC_FREEZE - 8 || (t / 3) & 1 ? sc_size : sc_grow_from;
    if (!p || !q)
        return;
    sc_place(p, w, s);
    sc_look(w, q, s, sc_roll);
}

/* test (the play log, user request 2026-11-14: "중간중간에 움직임이 막히는데, 로그도 남겨줘. 바닥 높낮이 때문인지 다른
 * 원인지"): the walk button held the way he faces with the ball, he did not move - why: the ball's wall test (its
 * front probe and its whole box's bits), or X's own update did not move him (his own terrain bits: a wall, a
 * slope); his height change, the ball's place. Logged when it starts, every 30 frames while it lasts, when it ends */
static void sc_block_log(char *p, float x_moved, int ball_wall)
{
    static int blocked, n;
    int now = sc_push && on_ground(STATE(p)) && (ball_wall || fabsf(x_moved) < 0.01f);
    char *w = sc_ball;
    if (now) {
        if (!blocked || ++n % 30 == 0) {
            int b_all = proj_terrain(w), b_front = sc_front(w, SX(p), sc_size), a_now = proj_terrain(p);
            test_log("scar push blocked (%s, %s): size %d axl %.1f %.1f (dy %.1f, state %d sub %d, terrain %x) ball %.1f "
                     "%.1f r %d: front probe %x, whole box %x", blocked ? "still" : "starts",
                     ball_wall ? "the ball's front test" : "X did not move", sc_size, PX(p), PY(p), PY(p) - sc_y0,
                     STATE(p), O8(p, 0x0E), a_now, PX(w), PY(w), sc_r[sc_size], b_front, b_all);
        }
        blocked = 1;
    } else {
        if (blocked && test_on)
            test_log("scar push free again after %d frames: axl %.1f %.1f", n + 1, PX(p), PY(p));
        blocked = 0;
        n = 0;
    }
}

/* after X's state code */
void scar_after(char *p)
{
    proj *q;
    float dx;
    int i;

    if (dna_boss != 5 || dna_pose)
        return;
    for (i = 0; i < SC_ROLLED; i++)
        if (sc_rolled[i] && !ours(sc_rolled[i], sc_roll_fn) && !ours(sc_rolled[i], sc_fly_fn))
            sc_rolled[i] = 0;
    if (sc_st != SC_NONE && !ours(sc_ball, sc_st == SC_DROP ? sc_drop_fn : sc_held_fn)) {
        if (test_on)
            test_log("scar ball lost (state %d)", sc_st);
        sc_st = SC_NONE;
        sc_ball = 0;
        sc_flip = 0;
        sc_wait = SC_RESPAWN;
        sc_size = 1;
        sc_dist = 0;
    }
    if (sc_flip) {                                /* rolling round the ball (it stays) or turning */
        int moved = !sc_flip_step || moves_step(p, sc_flip_dir * sc_flip_step);
        if (!moved || ++sc_flip > sc_flip_len) {
            O8(p, 0x29) ^= 0x40;                  /* turned round: the ball in front again */
            sc_flip = 0;
            if (sc_st == SC_HELD && sc_ball)
                sc_place(p, sc_ball, sc_size);
            if (test_on)
                test_log("scar flip end x %.0f, facing %d", PX(p), SX(p));
        }
        sc_x0 = PX(p);                            /* (the roll is no walk: no growth, no slowing) */
    }
    switch (sc_st) {
    case SC_NONE:
        if (sc_wait > 0) {
            sc_wait--;
            break;
        }
        if (proj_free() < 4)
            break;
        if (sc_first) {                           /* the change: the first ball in front of him at once */
            sc_ball = sc_spawn(1, PX(p) + SX(p) * sc_ahead(1), PY(p) + SC_FEET - sc_r[1], sc_held_fn);
            if (sc_ball) {
                sc_first = 0;
                sc_st = SC_HELD;
            }
        } else {                                  /* then from above the screen */
            char *cam = *(char **)0x041D8CD0 ? CAM(0) : 0;
            float top = cam ? *(volatile float *)(cam + 0x1C) : PY(p) - 160;
            sc_ball = sc_spawn(1, PX(p) + SX(p) * sc_ahead(1), top - sc_r[1] - 8, sc_drop_fn);
            if (sc_ball)
                sc_st = SC_DROP;
        }
        sc_size = 1;
        sc_dist = sc_roll = 0;
        if (test_on && sc_ball)
            test_log("scar new ball (%s) at %.0f %.0f", sc_st == SC_HELD ? "held" : "dropping", PX(sc_ball), PY(sc_ball));
        break;
    case SC_DROP:
        q = proj_of(sc_ball);
        PX(sc_ball) = PX(p) + SX(p) * sc_ahead(1);
        if (PY(sc_ball) + sc_r[1] >= PY(p) + SC_FEET) {   /* down at his feet: his */
            q->fn = sc_held_fn;
            sc_st = SC_HELD;
            sc_place(p, sc_ball, sc_size);
            axl_sound(SND_SCAR_ROLL);
            if (test_on)
                test_log("scar ball caught at %.0f %.0f", PX(sc_ball), PY(sc_ball));
        }
        break;
    case SC_HELD:
        q = proj_of(sc_ball);
        if (sc_flip)                              /* (rolling round it: sc_flip_tick) */
            break;
        dx = PX(p) - sc_x0;
        if (test_on) {                            /* (the play log: why a push stopped, sc_block_log) */
            float d = dx && on_ground(STATE(p)) ? dx * sc_walk[sc_size] : dx;
            int ball_wall;
            PX(p) = sc_x0 + d;
            sc_place(p, sc_ball, sc_size);
            ball_wall = d * SX(p) > 0 && sc_wall(sc_ball, SX(p), sc_size);
            sc_block_log(p, dx, ball_wall);
            PX(p) = sc_x0 + dx;
        }
        if (dx && on_ground(STATE(p))) {          /* slower as it grows */
            dx *= sc_walk[sc_size];
            PX(p) = sc_x0 + dx;
        }
        sc_place(p, sc_ball, sc_size);            /* (a wall at the ball's front: it is pulled in, sc_place) */
        if (dx && on_ground(STATE(p))) {
            sc_dist += fabsf(dx);
            sc_roll += dx;
        }
        sc_look(sc_ball, q, sc_size, sc_roll);
        if (sc_size < 4 && sc_dist >= SC_GROW_PX) {
            sc_grow_from = sc_size++;
            sc_dist -= SC_GROW_PX;
            proj_box(sc_ball, sc_box[sc_size]);
            q->b = sc_size;
            scar_grows++;
            axl_sound(SND_SCAR_KICK);
            world_freeze(SC_FREEZE, sc_grow_tick);
            if (test_on)
                test_log("scar ball grows to %d", sc_size);
        }
        break;
    }
}

/* the kick's frame (axl_dna_bosses.c dna_scaravich): the held ball rolls away */
void scar_kick(char *p)
{
    proj *q = ours(sc_ball, sc_held_fn);
    int i;

    if (sc_st != SC_HELD || sc_flip || !q)
        return;
    q->fn = sc_roll_fn;
    q->a = SX(p);
    q->b = sc_size;
    q->tx = q->a > 0 ? sc_roll : -sc_roll;
    {   /* an enemy ahead: at it (sc_fly_fn); none: rolling along the floor */
        float x = PX(sc_ball), y = PY(sc_ball), tx, ty, dx, dy, d;
        char *e = find_target(x, y, SC_KICK_RANGE);
        if (e) {
            objs_aim_point(e, &tx, &ty);
            dx = tx - x;
            dy = ty - y;
            d = sqrtf(dx * dx + dy * dy);
            if (dx * q->a > 0 && d > 1) {
                q->fn = sc_fly_fn;
                q->vx = (int)(dx / d * sc_fly_v[sc_size] * 65536);
                q->vy = -(int)(dy / d * sc_fly_v[sc_size] * 65536);
                VX(sc_ball) = q->vx;
                VY(sc_ball) = q->vy;
                if (test_on)
                    test_log("scar kick at enemy %02x at %.0f %.0f (from %.0f %.0f)", O8(e, 9), tx, ty, x, y);
            }
        }
    }
    for (i = 0; i < SC_ROLLED; i++)
        if (!sc_rolled[i]) {
            sc_rolled[i] = sc_ball;
            break;
        }
    scar_kicks++;
    axl_voice(SND_SCAR_KICK_VO);
    axl_sound(SND_SCAR_KICK);
    axl_sound(SND_SCAR_ROLL);
    if (test_on)
        test_log("scar kick size %d at %.0f", sc_size, PX(sc_ball));
    sc_ball = 0;
    sc_st = SC_NONE;
    sc_wait = SC_RESPAWN;
    sc_size = 1;
    sc_dist = sc_roll = 0;
}

/* an enemy's loss (axl_dna_bosses.c dna_hit_loss): a ball's on it - scaled by the ball's size */
int scar_hit_loss(char *e, int loss)
{
    char *balls[SC_ROLLED + 1];
    int i, n = 0;
    proj *q;

    if (sc_st == SC_HELD || sc_st == SC_DROP)
        balls[n++] = sc_ball;
    for (i = 0; i < SC_ROLLED; i++)
        if (sc_rolled[i])
            balls[n++] = sc_rolled[i];
    for (i = 0; i < n; i++) {
        float r;
        if (!balls[i] || !(q = proj_of(balls[i])) || q->w != balls[i] || q->b < 1 || q->b > 4)
            continue;
        r = sc_r[q->b] + 24.0f;
        if (fabsf(PX(balls[i]) - PX(e)) <= r && fabsf(PY(balls[i]) - PY(e)) <= r) {
            int more = loss * sc_mul2[q->b] / 2;
            if (test_on)
                test_log("scar ball size %d hit %d -> %d", q->b, loss, more);
            return more;
        }
    }
    return loss;
}

/* ---- the special: the fist's purple orb ---- */
static void sc_orb_fn(char *w, proj *q)
{
    int f = SC_ORB + (q->t / 3) % 4;
    if (q->c != f) {
        q->c = f;
        proj_pose_look(w, SCAR_TEX_INDEX, SCAR_PAT, f);
    }
}

/* his arm cannon's muzzle (anim 2, picture 9: the purple flash at boss x -24..-16, y -5..5; his look 7 px below Axl's
 * origin): the orb starts there (user report 2026-11-15: it came out off his arm - 8 px ahead, 9 px high) */
#define SC_MUZZLE_X 22
#define SC_MUZZLE_Y 7
void scar_orb(char *p)
{
    float x = PX(p) + SX(p) * SC_MUZZLE_X, y = PY(p) + SC_MUZZLE_Y, tx, ty, dx, dy, d;
    char *e = find_target(x, y, 240), *w;

    if (e)
        objs_aim_point(e, &tx, &ty);
    else {
        tx = x + SX(p) * 100;
        ty = y;
    }
    dx = tx - x;
    dy = ty - y;
    d = sqrtf(dx * dx + dy * dy);
    if (d < 1) {
        dx = (float)SX(p);
        dy = 0;
        d = 1;
    }
    w = proj_spawn(SC_ID, x, y, (int)(dx / d * 5 * 65536), -(int)(dy / d * 5 * 65536), FACE(p), 120, sc_orb_fn, 0);
    if (w) {
        proj_of(w)->c = -1;
        proj_box(w, box_orb);
        sc_orb_fn(w, proj_of(w));
        scar_orbs++;
        axl_voice(SND_SCAR_ORB_VO);
        axl_sound(SND_HEAT_SHOT);
    }
}

/* ---- the giga: the core, then the crush as a cut-scene ---- */
#define CG_KICK_AT 23      /* the kick's frame of anim 3 */
#define CG_POSE_END 37     /* its last frame (held) */
#define CG_FLY_MAX 90
#define CG_CORE_V 6.0f
/* The cut-scene (user design 2026-11-16, deep interview .omc/specs/deep-interview-scar-giga.md; about 10 s since the
 * user's "10초 정도로", the world still): the target rises (CG_RISE); rock chunks fly in from all round, faster and
 * faster, and stick on a ball that grows round the target size by size - layer on layer (to CG_GATHER); the view
 * size-4 ball stands there (CG_SWAP .. CG_WIPE); it is pressed 3 -> 2 -> 1 shaking harder
 * (to CG_PRESS); "Chibaku Tensei" (to CG_LEN) - then, the world moving again, the game's explosions in a chain on it
 * with the damage (CG_OUT; no rocks flung out), a boss falls back to where it was. */
#define CG_RISE 60         /* frames: the target rises CG_LIFT px */
#define CG_LIFT 50.0f
#define CG_GATHER 360      /* the rocks gather and the ball grows (from CG_RISE: 5 s) */
#define CG_SWAP CG_GATHER  /* the size-4 ball there (no black blink before it: user request 2026-11-16 "깜빡이는거 없어도
                            * 될 거 같아" - a wipe, then three blinks, then one had been tried) */
#define CG_WIPE 400        /* (held to here) */
#define CG_PRESS_STEP 30   /* 3 -> 2 -> 1, CG_PRESS_STEP frames each (from CG_WIPE) */
#define CG_PRESS (CG_WIPE + 3 * CG_PRESS_STEP)
#define CG_LEN 590         /* the voice (84 frames) and a moment (= the still world's frames) */
#define CG_OUT 40
#define CG_BOOMS 6         /* the explosions, one every CG_BOOM_GAP frames */
#define CG_BOOM_GAP 5
#define CG_FALL 20         /* frames a boss takes back down */
#define CG_ROCKS 16
#define CG_CYCLE 40.0f     /* a rock's round (fly in, stuck on the ball), in the gathering's time */
#define CG_FLY_PART 0.65f  /* (of the round: the flight) */
#define CG_SPEEDUP 300.0f  /* (the time v = g + g^2 / CG_SPEEDUP: 3 times the pace at the end) */
#define CG_BOSS_DMG 16
#define CG_BOSS_HP 32      /* (axl_dna_bosses.c MS_BOSS_HP: more hit points - a boss) */
#define CG_BAR_H 32
enum { CG_KICK, CG_FLY, CG_CUT, CG_GONE };
static int cg_st, cg_t, cg_ft;
static char *cg_core, *cg_tg, *cg_rock[CG_ROCKS], *cg_big, *cg_bar[4];
static float cg_ey0;     /* the target's y when the core hit (it rises, a boss comes back down) */
static float cg_y_top;   /* the ball's y at its height (the explosions there) */
void axl_boom_fn(char *w, proj *q);   /* axl_x.c: the game's explosion pictures */
static int cg_hp;   /* the target's hit points when the core left */
static const signed char box_core[4] = {-8, -8, 16, 16};
#define CG_TOUCH 6.0f   /* (a target its id does not hurt: the core this near its aim point counts as the hit) */
static float cg_x, cg_y;
static int sg;   /* the ball's size while the rocks gather (cg_draw) */

static void sc_core_fn(char *w, proj *q)
{
    char *e = cg_tg;
    float tx, ty, dx, dy, d;
    if (q->c != SC_CORE) {
        q->c = SC_CORE;
        proj_pose_look(w, SCAR_TEX_INDEX, SCAR_PAT, SC_CORE);
    }
    if (!e || !O8(e, 8) || cg_st != CG_FLY)
        return;
    objs_aim_point(e, &tx, &ty);
    dx = tx - PX(w);
    dy = ty - PY(w);
    d = sqrtf(dx * dx + dy * dy);
    if (d > 1) {
        VX(w) = (int)(dx / d * CG_CORE_V * 65536);
        VY(w) = -(int)(dy / d * CG_CORE_V * 65536);
    }
}

static char *cg_obj(int frame)
{
    char *w = proj_spawn(GID_SCAR, cg_x, cg_y, 0, 0, 0, 0, 0, P_HUD | P_WALL | P_NOSCREEN | P_GIGA);
    if (w) {
        proj_nobox(w);
        proj_pose_look(w, SCAR_TEX_INDEX, SCAR_PAT, frame);
        O8(w, 0x0B) = 0;
    }
    return w;
}

static void cg_free(void)
{
    int i;
    for (i = 0; i < CG_ROCKS; i++)
        if (cg_rock[i] && proj_of(cg_rock[i]))
            proj_kill(cg_rock[i]);
    for (i = 0; i < 4; i++)
        if (cg_bar[i] && proj_of(cg_bar[i]))
            proj_kill(cg_bar[i]);
    if (cg_big && proj_of(cg_big))
        proj_kill(cg_big);
    if (cg_core && ours(cg_core, sc_core_fn))
        proj_kill(cg_core);
    memset(cg_rock, 0, sizeof cg_rock);
    memset(cg_bar, 0, sizeof cg_bar);
    cg_big = cg_core = 0;
}

static void cg_show(char *w, float x, float y, int frame, int on)
{
    if (!w || !proj_of(w))
        return;
    PX(w) = x;
    PY(w) = y;
    VX(w) = VY(w) = 0;
    O32(w, 0x54) = SCAR_TEX_INDEX;
    O32(w, 0x58) = SCAR_PAT;
    O8(w, 0x66) = (unsigned char)frame;
    O8(w, 0x29) = 0;
    O8(w, 0x0B) = (unsigned char)(on != 0);
}

static float ease(float u) { return u <= 0 ? 0 : u >= 1 ? 1 : u * u * (3 - 2 * u); }

static void cg_screen(char *w, float sx, float sy, int on)   /* a band piece: screen coordinates, in front */
{
    if (!w || !proj_of(w))
        return;
    cg_show(w, sx, sy, SC_BAR, on);
    O8(w, 0x28) = 0xFF;
    O8(w, 0x2A) = 0;
    *(volatile float *)(w + 0x60) = 0.5f;
}

/* the cut-scene's picture at frame f (0 .. CG_LEN - 1 still, then CG_OUT frames as the world moves again) */
static void cg_draw(int f)
{
    int i, out = f >= CG_LEN, s, shown;
    char *cam = *(char **)0x041D8CD0 ? CAM(0) : 0;
    float slide = out ? 1 - ease((f - CG_LEN) / 10.0f) : ease(f / 10.0f), x = cg_x, y;

    /* the target up (still) or a boss back down (moving again) */
    float top = cam ? *(volatile float *)(cam + 0x1C) + CG_BAR_H + sc_r[4] : -1e9f;   /* (the full ball under the bar) */
    float most = cg_y - CG_LIFT >= top ? CG_LIFT : cg_y - top > 0 ? cg_y - top : 0;    /* (a target high up: less) */
    float lift = out ? most * (1 - ease((f - CG_LEN) / (float)CG_FALL)) : most * ease(f / (float)CG_RISE);
    y = cg_y - lift;
    if (!out)
        cg_y_top = y;
    if (cg_tg && O8(cg_tg, 8) && (!out || f - CG_LEN <= CG_FALL))
        PY(cg_tg) = cg_ey0 - lift;

    for (i = 0; i < 4; i++)                       /* the bars */
        cg_screen(cg_bar[i], 80.0f + 160 * (i & 1),
                  i < 2 ? -CG_BAR_H / 2 + CG_BAR_H * slide : 240 + CG_BAR_H / 2 - CG_BAR_H * slide, slide > 0);
    shown = out || f >= CG_SWAP;                  /* (the full ball there) */

    /* the ball while they gather (user report 2026-11-16: "겹겹이 쌓이는 느낌이 없어"): none, then sizes 1, 2, 3 as
     * the rocks pile on */
    {
        float p = (f - CG_RISE) / (float)(CG_GATHER - CG_RISE);
        sg = out || f < CG_RISE ? 0 : p < 0.15f ? 0 : p < 0.45f ? 1 : p < 0.75f ? 2 : 3;
    }
    if (cg_core && proj_of(cg_core))              /* the core on the target until the ball closes round it */
        cg_show(cg_core, x, y, SC_CORE, !shown && !sg);
    for (i = 0; i < CG_ROCKS; i++) {
        float a = i * 6.2831853f / CG_ROCKS + 0.3f * (i % 3), r = 0, u;
        float rs = (sg ? sc_r[sg] : 8) + 4.0f * (i % 3);   /* (the ball's surface now: they ride it as it grows) */
        int on = 0;
        if (!out && !shown && f >= CG_RISE) {
            /* each rock flies in from the edge, roughly (steps of 3 frames), slow then fast, sticks on the ball,
             * and goes round again from another side - faster and faster - until the blink (user request
             * 2026-11-16 "빠르게 깜빡이기 전까지 계속 모였으면") */
            int g = f - CG_RISE, cyc;
            float v, ph;
            g -= g % 3;
            v = g + (float)g * g / CG_SPEEDUP + i * (CG_CYCLE / (float)CG_ROCKS);
            cyc = (int)(v / CG_CYCLE);
            ph = (v - cyc * CG_CYCLE) / CG_CYCLE;
            a += 2.4f * cyc;                      /* (another side each round) */
            if (ph < CG_FLY_PART) {
                u = ph / CG_FLY_PART;
                u *= u;
                r = 220 - (220 - rs) * u;
            } else
                r = rs + (float)(((f / 3 + i) & 1) ? 1 : -1);
            on = 1;
        }
        cg_show(cg_rock[i], x + r * cosf(a), y - r * sinf(a), SC_ROCK + i % 8, on);
        if (cg_rock[i] && proj_of(cg_rock[i]))
            *(volatile float *)(cg_rock[i] + 0x60) = 1.0f;   /* (on the ball) */
    }
    /* the ball: growing under the rocks, then size 4, pressed 3 -> 2 -> 1 shaking harder (user report
     * 2026-11-16: "1단계까지 작아지는 느낌도 없고"), size 1 trembling under the voice */
    s = !shown ? sg : f < CG_WIPE ? 4 : f < CG_PRESS ? 3 - (f - CG_WIPE) / CG_PRESS_STEP : 1;
    if (cg_big && proj_of(cg_big)) {
        int k = !shown || f < CG_WIPE ? 0 : f < CG_PRESS ? 1 + (f - CG_WIPE) / CG_PRESS_STEP : 4;   /* (how hard) */
        float shake = k ? (float)((f & 2) ? 1 : -1) * (k < 4 ? k : 2) : 0;
        int blink = k >= 2 && k < 4 && (f & (k == 2 ? 4 : 2));
        cg_show(cg_big, x + shake, y + (k == 4 ? (float)((f & 1) ? 1 : -1) : 0),
                SC_BALL + SC_TURN * ((s ? s : 1) - 1) + (f / 4) % SC_TURN, s && !out && !blink);
        *(volatile float *)(cg_big + 0x60) = 1.5f;   /* (over the target, under the rocks) */
    }
}

static void cg_cut_tick(int t)
{
    static int last_sg;
    cg_ft = t;
    if (t == 0)
        last_sg = 0;
    if (t == CG_RISE)
        axl_sound(SND_SCAR_ROLL);
    cg_draw(t);
    if (sg != last_sg && sg)                      /* (the ball a size bigger) */
        axl_sound(SND_SCAR_KICK);
    last_sg = sg;
    if (t == CG_SWAP || t == CG_WIPE || t == CG_WIPE + CG_PRESS_STEP || t == CG_WIPE + 2 * CG_PRESS_STEP)
        axl_sound(SND_SCAR_KICK);
    if (t == CG_PRESS) {
        axl_voice(SND_SCAR_GIGA_VO);
        if (test_on)
            test_log("scar giga: the voice (frame %d)", t);
    }
    if (test_on && (t == CG_RISE || t == CG_GATHER || t == CG_WIPE))
        test_log("scar giga: phase at frame %d", t);
}

/* every frame after the units moved (axl_menu.c move_hook): the cut-scene's pictures as the world moves again */
void scar_late(void)
{
    if (cg_st == CG_GONE && cg_t < CG_OUT)
        cg_draw(CG_LEN + cg_t);
    else if (cg_st == CG_FLY) {                   /* (the cut-scene's pictures, made ahead, hidden) */
        int i;
        for (i = 0; i < CG_ROCKS; i++)
            if (cg_rock[i] && proj_of(cg_rock[i]))
                O8(cg_rock[i], 0x0B) = 0;
        for (i = 0; i < 4; i++)
            if (cg_bar[i] && proj_of(cg_bar[i]))
                O8(cg_bar[i], 0x0B) = 0;
        if (cg_big && proj_of(cg_big))
            O8(cg_big, 0x0B) = 0;
    }
}

static int cg_is_boss(char *o)
{
    unsigned char t = O8(o, 9);
    return t == 0x30 || t == 0x37 || (t && strchr("\x31\x32\x33\x34\x36\x38\x39\x3A\x3D\x3E\x3F", t)) ||
           O8(o, 0x84) > CG_BOSS_HP;
}

static void cg_hit(char *o, int id)
{
    char *w = proj_spawn(id, PX(o), PY(o), 0, 0, 1, 3, 0, P_PIERCE | P_WALL | P_NOSCREEN | P_HIDDEN | P_GIGA);
    if (w)
        proj_box(w, box_crush);
}

static void cg_crush(void)
{
    char *e = cg_tg;
    if (!e || !O8(e, 8))
        return;
    scar_crushes++;
    if (cg_is_boss(e)) {   /* big damage, not a kill (as the Magma Stream's: the body, down to 1 at most) */
        if (!O8(e, 10) && O8(e, 0x84) > 1 && O8(e, 0x84) < 127)
            O8(e, 0x84) = (unsigned char)(O8(e, 0x84) > CG_BOSS_DMG ? O8(e, 0x84) - CG_BOSS_DMG : 1);
        cg_hit(e, GID_SCAR);
    } else {
        if (O8(e, 0x84) > 1)
            O8(e, 0x84) = 1;
        cg_hit(e, GID_SCAR);
        cg_hit(e, KILL_ID);
    }
    if (test_on)
        test_log("scar giga crush: %s %02x hp %d", cg_is_boss(e) ? "boss" : "enemy", O8(e, 9), O8(e, 0x84));
}

/* the giga (axl_dna_bosses.c giga_scaravich), frame t: *pose = his kick motion's frame; 0 = over */
int scar_giga(char *p, int t, int *pose)
{
    proj *q;
    int i;

    *pose = t < CG_POSE_END ? t : CG_POSE_END;
    if (t == 0) {
        cg_st = CG_KICK;
        cg_t = 0;
        cg_tg = 0;
        cg_free();
    }
    switch (cg_st) {
    case CG_KICK:
        if (t >= CG_KICK_AT) {
            float x = PX(p) + SX(p) * 36, y = PY(p) + SC_FEET - 16;
            cg_tg = find_target(x, y, 400);
            cg_core = proj_spawn(GID_SCAR, x, y, SX(p) * (int)(CG_CORE_V * 65536), 0, 0, 0, sc_core_fn,
                                 P_PIERCE | P_WALL | P_NOSCREEN | P_GIGA);
            if (cg_core) {
                proj_of(cg_core)->c = -1;
                proj_box(cg_core, box_core);      /* (it hits: the cut-scene starts with its hit, CG_FLY) */
                sc_core_fn(cg_core, proj_of(cg_core));
            }
            cg_hp = cg_tg ? O8(cg_tg, 0x84) : 0;
            cg_x = x;
            cg_y = y;
            for (i = 0; i < CG_ROCKS; i++)       /* the cut-scene's pictures made now, hidden (P_HUD: made in the
                                                  * move pass, so not on the frame of the hit) */
                cg_rock[i] = cg_obj(SC_ROCK + i % 8);
            cg_big = cg_obj(SC_BALL);
            for (i = 0; i < 4; i++)
                cg_bar[i] = cg_obj(SC_BAR);
            axl_sound(SND_SCAR_KICK);
            cg_st = CG_FLY;
            cg_t = 0;
            if (test_on)
                test_log("scar giga core %s (target %p)", cg_core ? "out" : "refused", cg_tg);
        }
        return 1;
    case CG_FLY:
        q = ours(cg_core, sc_core_fn);
        if (!q || ++cg_t > CG_FLY_MAX || (cg_tg && !O8(cg_tg, 8))) {
            if (test_on)
                test_log("scar giga: the core missed (t %d)", cg_t);
            cg_free();
            return 0;
        }
        if (cg_tg) {   /* its hit (user request 2026-11-15: "코어가 적에게 맞았을 때 연출이 시작돼야해" - it started
                        * 20 px short of the target, the core had no attack box): the target's hit points down,
                        * the core's hit flag, or (an id it does not hurt) the core on its aim point */
            float tx, ty;
            int hit;
            objs_aim_point(cg_tg, &tx, &ty);
            hit = O8(cg_tg, 0x84) < cg_hp || q->got > 0 ||
                  (fabsf(PX(cg_core) - tx) <= CG_TOUCH && fabsf(PY(cg_core) - ty) <= CG_TOUCH);
            if (hit) {
                cg_x = tx;
                cg_y = ty;
                VX(cg_core) = VY(cg_core) = 0;
                proj_nobox(cg_core);
                cg_st = CG_CUT;
                cg_ey0 = PY(cg_tg);
                cg_draw(0);
                world_freeze(CG_LEN, cg_cut_tick);
                if (test_on)
                    test_log("scar giga: the core hit target %02x (hp %d -> %d, flag %d) at %.0f %.0f: the cut-scene",
                             O8(cg_tg, 9), cg_hp, O8(cg_tg, 0x84), q->got, PX(cg_core), PY(cg_core));
            }
        }
        return 1;
    case CG_CUT:                                  /* (the world moves again) */
        /* the rocks and the core done with: their slots free for the crush's hits and the explosions
         * (2026-11-16: all spawns refused there - 'proj spawn ... -> 00000000', no explosions seen) */
        for (i = 0; i < CG_ROCKS; i++)
            if (cg_rock[i] && proj_of(cg_rock[i]))
                proj_kill(cg_rock[i]);
        if (cg_core && ours(cg_core, sc_core_fn))
            proj_kill(cg_core);
        memset(cg_rock, 0, sizeof cg_rock);
        cg_core = 0;   /* (the crush and the explosions from the next frame: the slots are given back then) */
        cg_st = CG_GONE;
        cg_t = 0;
        return 1;
    case CG_GONE:
        if (cg_t == 1)
            cg_crush();
        if (cg_t >= 1 && cg_t - 1 < CG_BOOMS * CG_BOOM_GAP && (cg_t - 1) % CG_BOOM_GAP == 0) {   /* the explosions' chain, on the ball's
                                                                          * place (no hit: the damage is the crush) */
            static const signed char off[CG_BOOMS][2] = {{0, 0}, {-18, -12}, {16, 10}, {-10, 16}, {14, -16}, {0, 0}};
            int k = (cg_t - 1) / CG_BOOM_GAP;
            char *b = proj_spawn(GID_SCAR, cg_x + off[k][0], cg_y_top + off[k][1], 0, 0, 1, 34, axl_boom_fn,
                                 P_PIERCE | P_WALL | P_NOSCREEN | P_GIGA);
            if (b) {
                proj_nobox(b);
                axl_boom_fn(b, proj_of(b));
            }
            if (k == 0)
                axl_sound(SND_SCAR_KICK);
        }
        if (++cg_t >= CG_OUT) {
            cg_free();
            return 0;
        }
        return 1;
    }
    return 0;
}

/* test values (axl_dna_bosses.c dna_value) */
int scar_value(const char *name, int *ok)
{
    *ok = 1;
    if (!strcmp(name, "scar_size"))
        return sc_st == SC_HELD ? sc_size : 0;
    if (!strcmp(name, "scar_state"))
        return sc_st;
    if (!strcmp(name, "scar_dist"))
        return (int)sc_dist;
    if (!strcmp(name, "scar_grows"))
        return scar_grows;
    if (!strcmp(name, "scar_kicks"))
        return scar_kicks;
    if (!strcmp(name, "scar_flips"))
        return scar_flips;
    if (!strcmp(name, "scar_orbs"))
        return scar_orbs;
    if (!strcmp(name, "scar_crushes"))
        return scar_crushes;
    if (!strcmp(name, "scar_cut"))
        return cg_st == CG_CUT ? 100 + cg_ft : cg_st;
    *ok = 0;
    return 0;
}
