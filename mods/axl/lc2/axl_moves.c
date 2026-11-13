/* The bosses' own moves while changed (user design 2026-10-26, from the bosses' movement patterns in
 * docs/boss_attacks.md): the dash button is the boss's move, the DNA button (giga) its second attack.
 *
 *   boss        dash (move)                                   DNA button (second attack)
 *   Yammark     X-style dash: 8 ways 6 px/f while held, at most   the next formation of his 6 options
 *               30 frames, afterimages (yd_*, below; 2026-11-09)  (axl_dna_bosses.c yam_next_form)
 *   Wolfang     ice-spike dash 6 px/f, spikes, body hit, afterimages  leap 20 degrees up 6 px/f, onto a wall
 *               walls and ceiling (wolf_*): a wall touched in the air holds him (jump: leap off, down: let go, DNA
 *               button: ice up and down the wall); up + jump: up to the ceiling, hanging (shot: 4 aimed shards, dash:
 *               the ceiling dash leaving icicles, down / jump: let go)
 *   Heatnix     tap: X-style dash 20 f (yd_*, hd_input);       the 3-step blade \\ / X 5 px/f through walls (the shot's until 2026-11-12)
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
/* at most CRESCENT_MAX of it out (user requests 2026-11-12: "one shot only", then "two: one felt cramped", then 3
 * for the 3-step blade): the special does nothing while that many fly */
#define CRESCENT_MAX 3
/* the 3-step blade (user request 2026-11-12: "\\", "/", then X): the special pressed again within SLASH_WINDOW
 * frames of the last throw is the next step, else the first; the pictures lc2_dna.blade_pictures (boss 3's HUD
 * section: GIGA_SLASH / _R facing right, 2 per step, every SLASH_STEP frames); the X hits as the charged id */
#define SLASH_WINDOW 40
/* the blades' pace (user request 2026-11-13: "too short a gap"): a press within SLASH_GAP frames of the last blade
 * waits (kept, not lost) and goes when the gap is over - mashing gave a blade every 9 frames; now one in 20 + its wind-up */
#define SLASH_GAP 20
static int slash_queued;
#define SLASH_STEP 3
static int slash_step = 2, slash_idle = 1000;
static const signed char box_slash[3][4] = {{-14, -30, 28, 60}, {-14, -30, 28, 60}, {-18, -30, 36, 60}};
static char *crescent[CRESCENT_MAX];
static void crescent_fn(char *w, proj *q)
{
    int n = GIGA_SLASH_N / 3, f = (q->t / SLASH_STEP) % n;

    if (q->c != f) {
        q->c = f;
        /* (facing right the mirrored pictures swap \\ and /: steps 0 / 1 swapped there, so the screen shows \\ / X
         * either way) */
        int s = VX(w) > 0 && q->b < 2 ? 1 - q->b : q->b;
        proj_pose_look(w, HUD_TEX_INDEX, HUD_PAT + 2, (VX(w) > 0 ? GIGA_SLASH_R : GIGA_SLASH) + n * s + f);
    }
    O8(w, 0x29) &= ~0x40;   /* (the picture is turned already: the game's own mirror would turn it back - user report
                             * 2026-11-12, it flew backwards; as Yammark's turned drones) */
}
static int crescent_out(void)
{
    int i, n = 0;
    proj *q;
    for (i = 0; i < CRESCENT_MAX; i++) {
        q = crescent[i] ? proj_of(crescent[i]) : 0;
        if (q && q->fn == crescent_fn)
            n++;
        else
            crescent[i] = 0;
    }
    return n >= CRESCENT_MAX;
}
static void crescent_add(char *w)
{
    int i;
    for (i = 0; w && i < CRESCENT_MAX; i++)
        if (!crescent[i]) {
            crescent[i] = w;
            return;
        }
}
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
    float ex = 0, ey = 0, dx, dy, d;
    if (e)
        objs_aim_point(e, &ex, &ey);   /* (where it was last hurt, else its hurt box: axl_objs.c) */
    dx = e ? ex - x : SX(p) * 100.0f;
    dy = e ? ey - y : 0.0f;
    d = sqrtf(dx * dx + dy * dy);
    if (d < 1)
        d = 1;
    *vx = (int)(dx / d * speed * 65536);
    *vy = -(int)(dy / d * speed * 65536);
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

/* Yammark's dash (user decisions 2026-11-09: X's dash, 8 ways, no invincibility, no cooldown): the dash button
 * starts it toward the held direction (none: where he faces) at YD_V px/f, on while held up to YD_LEN frames, the
 * direction steered by the keys meanwhile; the shot and the special button stay his (moves_before returns 0). X's
 * own left / right are taken off the held keys for its frames (his air control would add his speed), the move is
 * done here after X's state code like Shark's leap (walls, ceiling, floor; up slopes on the ground). Afterimages
 * (user request 2026-11-09: as X's and Zero's dash, green, not blinking - the game's own ones cannot draw the boss):
 * his picture every YD_GHOST_GAP frames, YD_GHOST_LIFE frames each, in flat green (axl_dna.c dna_ghost_palette:
 * fainter as they age). It ends at once with the change's leaving blink (dna_slide) */
#define YD_LEN 30
#define YD_V 6.0f
#define YD_GHOST_GAP 4
#define YD_GHOST_LIFE 12
#define YD_GHOSTS 3
static int yd_left, yd_n, yd_dx, yd_dy, yd_pal = -1;
static char *yd_ghost[YD_GHOSTS];

int moves_yam_dash(void)
{
    return yd_left > 0;
}

/* the dash's direction (axl_dna.c motion_seq: its look is the flight's that way, user report 2026-11-10) */
int moves_yam_dash_dir(int *dy)
{
    *dy = yd_dy;
    return yd_dx;
}

static void yd_ghost_fn(char *w, proj *q)
{
    *(volatile float *)(w + 0x1C) = q->y;
    VX(w) = VY(w) = 0;
    O8(w, 0x2A) = 3;                              /* behind him (user report 2026-11-09: they were drawn over him) */
    O8(w, 8) |= 0x10;                             /* translucent (added to the screen, as X's: axl_dna.c
                                                   * dna_ghost_palette) */
    if (yd_pal >= 0)
        *(volatile unsigned short *)(w + 0x5E) = (unsigned short)(yd_pal + 4 * (2 - q->t * 3 / YD_GHOST_LIFE));   /* (the darkest first, paler
                                                   * as it goes: user request 2026-11-10) */
    else if (q->t & 1)                            /* (no free palettes: the old blink) */
        *(volatile float *)(w + 0x1C) = -10000.0f;
}

int moves_yam_ghosts(void)   /* test: the dash's afterimages up now */
{
    int i, n = 0;
    for (i = 0; i < YD_GHOSTS; i++)
        n += yd_ghost[i] && proj_of(yd_ghost[i]) && proj_of(yd_ghost[i])->fn == yd_ghost_fn;
    return n;
}

/* every changed frame (moves_after): the afterimages' palettes while any is out */
static void yd_ghosts_frame(void)
{
    yd_pal = dna_ghost_palette((dna_boss == 1 || dna_boss == 3) && moves_yam_ghosts() > 0);
}

static void yd_input(char *p, int dash_new, int dash_held)
{
    unsigned short h = *(volatile unsigned short *)(p + 0xC0);   /* (HELD: 1 right, 2 left, 4 up, 8 down) */
    int dx = h & 1 ? 1 : h & 2 ? -1 : 0, dy = h & 4 ? -1 : h & 8 ? 1 : 0;

    if (dash_new && !dna_leaving()) {
        if (!dx && !dy)
            dx = SX(p);
        yd_left = YD_LEN;
        yd_n = 0;
        yd_dx = dx;
        yd_dy = dy;
        axl_sound(SND_DASH);
        if (test_on)
            test_log("yammark dash %d %d", dx, dy);
    } else if (yd_left && !dash_held)
        yd_left = 0;
    if (!yd_left)
        return;
    if (dx || dy) {                               /* steered (the keys let go: on the way it went) */
        yd_dx = dx;
        yd_dy = dy;
    }
    *(volatile unsigned short *)(p + 0xC0) = h & ~3;
    if (yd_dx)
        O8(p, 0x29) = (O8(p, 0x29) & ~0x40) | (yd_dx > 0 ? 0x40 : 0);
}

/* Heatnix's dash (user request 2026-11-12: "the dash charged lets out the God Bird, not charged the plain dash"):
 * a press is Yammark's X-style dash (yd_*) toward the held direction for HD_LEN frames at once; the button held on
 * charges (X's glow and charge sound, axl_dna_bosses.c heat_charge_stage; user request 2026-11-12: "as X's charge
 * shot, the press dashes, the hold charges"); let go full, the firebird rush (move 3: 8 px/f, invincible, the
 * firebird's body hit). A giga or a move under way breaks the charge off. */
#define HD_FULL 60
#define HD_LEN 20
#define HD_SOUND_AT 25
static int hd_hold;
static void move_start(char *p, int k);

int moves_dash_charge(void)
{
    return dna_boss == 3 ? hd_hold : 0;
}

static void hd_input(char *p, int dash_new, int dash_held)
{
    unsigned short h = *(volatile unsigned short *)(p + 0xC0);   /* (HELD: 1 right, 2 left, 4 up, 8 down) */
    int dx = h & 1 ? 1 : h & 2 ? -1 : 0, dy = h & 4 ? -1 : h & 8 ? 1 : 0, was = hd_hold, full, busy;

    busy = move_k || dna_giga_look() >= 0;   /* (the God Bird, a giga: no dash now) */
    if (dna_leaving()) {
        hd_hold = 0;
    } else if (dash_new) {   /* a press: the plain dash at once (not during the God Bird / a giga), the hold from
                              * here charges (as X's buster: a shot at the press, the charge after - user request
                              * 2026-11-12; pressed again in the God Bird it charged nothing, user report: now it does) */
        if (!busy && axl_part_full() && skill_try(SK_DASH)) {   /* the Ultimate Buster: the God Bird at once (user
                                                                  * request 2026-11-12) */
            yd_left = 0;
            move_start(p, 3);
            axl_voice(SND_HEAT_GODBIRD);
            hd_hold = 0;
            return;
        }
        if (!busy) {
            if (!dx && !dy)
                dx = SX(p);
            yd_left = HD_LEN;
            yd_n = 0;
            yd_dx = dx;
            yd_dy = dy;
            axl_sound(SND_DASH);
        }
        hd_hold = !axl_part_full();   /* (the Ultimate Buster: the God Bird comes at the press - no charge, no glow,
                                       * user report 2026-11-14) */
        if (test_on)
            test_log("heat dash %d %d%s", dx, dy, busy ? " (charge only)" : "");
    } else if (dash_held) {
        if (hd_hold && hd_hold < HD_FULL)
            hd_hold++;
    } else if (hd_hold) {   /* let go: full, the God Bird (the plain dash, if still on, cut short) */
        full = hd_hold >= HD_FULL;
        hd_hold = 0;
        if (test_on)
            test_log("heat dash let go (%s)", full ? (busy ? "full, busy" : "god bird") : "short");
        if (full && !busy && skill_try(SK_DASH)) {
            yd_left = 0;
            move_start(p, 3);
            axl_voice(SND_HEAT_GODBIRD);   /* "God Bird!" (user request 2026-11-12) */
        }
    }
    if (was < HD_SOUND_AT && hd_hold >= HD_SOUND_AT)
        axl_sound(SND_CHARGE);
    else if (was >= HD_SOUND_AT && hd_hold < HD_SOUND_AT)
        axl_sound_stop(SND_CHARGE);
    if (!yd_left)
        return;
    *(volatile unsigned short *)(p + 0xC0) = h & ~3;   /* (its own way: left / right do not walk him meanwhile) */
    if (yd_dx)
        O8(p, 0x29) = (O8(p, 0x29) & ~0x40) | (yd_dx > 0 ? 0x40 : 0);
}

static void yd_after(char *p)
{
    float v = yd_dx && yd_dy ? YD_V * 0.70710678f : YD_V;
    int i;

    if ((dna_boss != 1 && dna_boss != 3) || dna_pose || dna_leaving()) {
        yd_left = 0;
        return;
    }
    VX(p) = 0;
    if (yd_dx) {
        if (on_ground(STATE(p)) && !yd_dy)
            moves_step(p, yd_dx * v);
        else {
            PX(p) += yd_dx * v;
            if (wall_ahead(p, yd_dx))
                PX(p) -= yd_dx * v;
        }
    }
    if (yd_dy) {
        PY(p) += yd_dy * v;
        if ((yd_dy < 0 && (proj_terrain(p) & 4)) || (yd_dy > 0 && (proj_terrain(p) & 8)))
            PY(p) -= yd_dy * v;
    }
    if (yd_n++ % YD_GHOST_GAP == 1)   /* (from its second frame: the picture drawn last is the dash's by then - the
                                        * first showed the pose before it, Heatnix's wings up, user report 2026-11-12) */
        for (i = 0; i < YD_GHOSTS; i++)
            if (!yd_ghost[i] || !proj_of(yd_ghost[i]) || proj_of(yd_ghost[i])->fn != yd_ghost_fn) {
                char *w = proj_spawn(0, PX(p), PY(p), 0, 0, FACE(p), YD_GHOST_LIFE, yd_ghost_fn,
                                     P_PIERCE | P_TOUGH | P_WALL | P_NOSCREEN);
                yd_ghost[i] = 0;
                if (w) {
                    proj_nobox(w);
                    if (dna_ghost(w))
                        yd_ghost[i] = w;
                    else
                        proj_kill(w);
                }
                break;
            }
    if (--yd_left <= 0)
        yd_left = 0;
}

static void clone_fn(char *w, proj *q)
{
    char *e = find_target(PX(w), PY(w), 200);
    float dx, dy, d, ex, ey;
    if (!e)
        return;
    objs_aim_point(e, &ex, &ey);
    dx = ex - PX(w);
    dy = ey - PY(w);
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
    slash_queued = 0;
    yd_left = 0;
    if (hd_hold >= HD_SOUND_AT)
        axl_sound_stop(SND_CHARGE);
    hd_hold = 0;
    yd_pal = dna_ghost_palette(0);   /* (the afterimages' palettes given back) */
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
    if (dna_boss == 3 && yd_left)                 /* Heatnix's plain dash: his wings-level picture (look cdash) */
        return 7;
    if (seq_n && seq_k == 3 && slash_step < 2)   /* Heatnix's wing swings (the 3-step blade's \\ and /) */
        return slash_step == 0 ? 6 : 8;
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
    if (slash_idle < 1000)
        slash_idle++;
    if (k == 1)
        yd_input(p, dash_new, dash_held);
    else if (k == 3)
        hd_input(p, dash_new, dash_held);
    else
        yd_left = 0;
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
    if (!move_k && dash_new && k != 1 && k != 3 && !(k == 4 && !on_ground(STATE(p))) && !(k == 8 && on_ground(STATE(p))) &&
        skill_try(SK_DASH))
        move_start(p, k);
    if (k == 3 && (alt_new || slash_queued)) {   /* (Heatnix's blade: SLASH_GAP after the last one, the press kept) */
        if (seq_n || slash_idle < SLASH_GAP) {
            slash_queued = alt_new || slash_queued;
            alt_new = 0;
        } else {
            alt_new = 1;
            slash_queued = 0;
        }
    }
    if (alt_new && k != 1 && k != 2 && k != 7 && !seq_n && !((k == 4 || k == 6) && move_k) &&
        !(k == 3 && crescent_out()) && skill_try(SK_SPECIAL)) {
        /* the second attack (Wolfang's: wolf_input; Shark's leap and Turtloid's bounce not during a move) */
        seq_k = k;
        seq_t = 0;
        seq_n = k == 2 ? 4 : k == 8 ? 3 : k == 5 ? 3 : k == 4 ? 0 : 1;
        if (k == 3) {   /* the 3-step blade's step, its motion and its moment (user requests 2026-11-12): \\ from his
                         * left wing, / from his right one (the user's drawings: look cshot / leap, the blade at t =
                         * 8), the X with anim 6 (look alt, t = 18) */
            slash_step = slash_idle <= SLASH_WINDOW ? (slash_step + 1) % 3 : 0;
            slash_idle = 0;
            seq_t = slash_step < 2 ? 8 : 18;
        }
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
        case 3: {   /* the 3-step blade's next step */
                    char *w;
                    crescent_out();   /* (its slots of the gone ones freed) */
                    slash_idle = 0;
                    w = shot(slash_step == 2 ? 0x0C : 3, PX(p) + SX(p) * 16, PY(p) - 20, SX(p) * FXP(5), 0, FACE(p), 0,
                             crescent_fn, P_PIERCE | P_WALL | (slash_step == 2 ? P_GIGA : 0), FX_FIRE,
                             box_slash[slash_step]);
                    if (w) {
                        proj_of(w)->b = slash_step;
                        proj_of(w)->c = -1;
                        crescent_fn(w, proj_of(w));
                    }
                    crescent_add(w);
                    if (test_on)
                        test_log("heat blade step %d", slash_step);
                } axl_sound(SND_HEAT_SHOT); axl_voice(SND_HEAT_TEYA); break;   /* (BIN 35 sub 0: 5 px/f
                                                   * through walls; "Teya!", user request 2026-11-12) */
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
    if (yd_left)
        yd_after(p);
    yd_ghosts_frame();
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
