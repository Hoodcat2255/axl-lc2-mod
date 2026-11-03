/* Axl's play on PC: the X main hooks, a port of PS1 mods/axl/src/axl_main.c (axl_frame / axl_post).
 *
 * PS1 X object field -> PC (P = *(this + 0xD4), the player work; re/lc2/codemap.md section 3):
 *   +4 init 0x0C, +5 state 0x0D, +0x15 facing 0x29, +0x17 anim 0x2B, +0x20 vx 0x3C, +0x24 vy 0x40 (16.16 ints),
 *   +0x2C gravity 0x48, +0x100 X's gravity 0x2A0, +0x08 / +0x0C x / y (16.16) -> floats 0x14 / 0x1C, +0x7C held 0xC0,
 *   +0x80 new 0xC4, +0x84 dash jump 0xC8, +0x8C afterimages 0x114, +0xD3 input lock 0x1EC.
 * Not ported yet: the copy shot's green look, White
 * Axl palettes, DNA Change, hover flames and grey afterimages (PS1 GPU packets), sounds.
 */
#include <windows.h>
#include <math.h>

#include "axl_lc2.h"
#include "axl_data.h"      /* the game-derived tables (axl_data.bin) */
#include "axl_proj.h"
#include "axl_test.h"   /* PS1 build constants: tip_crouch, tip_wall, tip_rope, ROPE_AIM_ANIM (copied by lc2_hook.py) */

typedef unsigned char u8;
typedef unsigned short u16;
typedef int s32;

#define O8(p, o) (*(volatile u8 *)((p) + (o)))
#define O16(p, o) (*(volatile u16 *)((p) + (o)))
#define O32(p, o) (*(volatile s32 *)((p) + (o)))
#define INIT(p) O8(p, 0x0C)
#define STATE(p) O8(p, 0x0D)
#define FACING(p) O8(p, 0x29)
#define ANIM(p) O8(p, 0x2B)
#define VY(p) O32(p, 0x40)
#define GRAVITY(p) O32(p, 0x48)
#define X_GRAVITY(p) O32(p, 0x2A0)
#define POS_Y(p) (*(volatile float *)((p) + 0x1C))   /* PS1 +0x0C (16.16) -> float pixels */
#define HELD(p) O16(p, 0xC0)
#define NEWP(p) O16(p, 0xC4)
#define DASH_JUMP(p) O8(p, 0xC8)
#define AFTERIMAGE(p) O8(p, 0x114)
#define LOCK(p) O8(p, 0x1EC)
#define FACE_RIGHT 0x40

#define B_RIGHT 0x1
#define B_LEFT 0x2
#define B_UP 0x4
#define B_DOWN 0x8
#define B_SPECIAL 0x10
#define B_SHOT 0x20
#define B_GIGA 0x40
#define B_R 0x200   /* X's next-weapon button (0x200: R on the pad, user report 2026-10-29): the DNA button (second attack) while changed; L alone opens the ring */
#define B_JUMP 0x80
#define B_DASH 0x100

#define RAPID 6               /* frames between shots while the button is held */
#define AIM_GROUND 193        /* + 0 up, 1 up-diagonal, 2 forward, 3 down-diagonal */
#define AIM_GROUND_DOWN 203   /* straight down standing (PC only: frame 243, lc2_hook.PC_SCRIPTS) */
#define AIM_WALL 229          /* + 0 up-diagonal, 1 down-diagonal on a wall (PC only: frames 244 / 245) */
#define ANIM_CLING 13         /* X's wall cling */
#define ANIM_CLING_SHOT 125   /* X's wall cling shooting (forward) */
#define AIM_AIR 197           /* + 0 up, 1 up-diagonal, 2 forward, 3 down-diagonal, 4 down */
#define ANIM_IDLE 5
#define ANIM_FALL 11
#define ANIM_DASH 16
#define ANIM_LAND 12          /* X's landing */
#define ANIM_AIR_DASH 250    /* lc2_hook.AIR_DASH_ANIM: anim 16's dash frames, no standing start */
#define ANIM_HOVER 202
#define ANIM_WALK 8     /* X's walk loop */
#define ST_CROUCH 26
#define ST_ULT_HOVER 33       /* the Ultimate armour's hover (armour 4) */
#define C_WALLS 0x3           /* the wall bits 1 / 2 of the terrain / object contact flags (P+0xB0 / B1 / F0, PS1 +0x70) */
#define ST_WALL_KICK 10        /* the kick off a wall (9 cling, 11 slide) */
static int adash_kick;         /* the air dash was started in a wall kick */
static int adash_right;        /* the air dash's way */
#define ST_LADDER 15          /* on a ladder (PS1 FUN_8003b7b4; 14 = getting on / off its top) */
#define ANIM_LADDER_SHOT 92   /* X's ladder shot (frame 150) */
#define ANIM_LADDER_AIM 183   /* + 0 up, 1 up-diagonal, 2 down-diagonal, 3 down: bank B 74-77 (lc2_hook.PC_SCRIPTS) */
#define HOVER_MAX 50
#define GLIDE_DIV 4
#define AIR_DASH 20

/* X's on-screen limit per shot id (FUN_02426980; the second table with the parts bit 0x40000) */
#define SHOT_LIMIT ((volatile u8 *)0x03483BA8)
#define SHOT_LIMIT2 ((volatile u8 *)0x03483BE8)
#define RAPID_LIMIT 16   /* Axl's shots out at once (X: 3; 8 until 2026-10-27, user: more - High Max) */

/* PS1 FUN_800179a4 = FUN_023c1730(obj, anim): thiscall with two stack args, ecx unused */
typedef void(__fastcall *set_anim_t)(char *ecx, void *edx, char *obj, int anim);
#define SET_ANIM ((set_anim_t)0x023C1730)

enum { D_UP, D_UPDIAG, D_FWD, D_DOWNDIAG, D_DOWN };

typedef void(__stdcall *x_anim_bank_t)(char *p, int anim);
#define X_ANIM_BANK ((x_anim_bank_t)0x023AEA40)

static void set_anim(char *p, int anim)
{
    if (anim < N_X_ANIMS)
        X_ANIM_BANK(p, anim);   /* X's own anims: with their bank (SET_ANIM keeps the last one: a bank B air shot
                                 * left the air dash's frames read from bank B, a standing picture - 2026-10-27) */
    else
        SET_ANIM(p, 0, p, anim);
}

/* PS1 FUN_8003b3b8 start_cling = FUN_0253eb70(this): wall cling (anim 13), speeds 0, state 9 */
typedef void(__fastcall *x_method_t)(char *self);
#define START_CLING ((x_method_t)0x0253EB70)
/* PS1 +0x8A cling timer = float counter B+0x38 (P+0xF4), set by FUN_023c0530(B, value) */
typedef void(__fastcall *set_byte_t)(char *obj, void *edx, int v);
#define SET_CLING_TIMER ((set_byte_t)0x023C0530)

/* rope (PS1 FUN_80039984): 27 = under a bar, 28 = on a rope. X's rope anims 144-168 (bank B) -> pose (build_axl.py
 * ZIP_X: 0 bar, 1 / 2 slope, 3 level; 0xff none). Shooting, up / down aim the diagonals: Axl's anims
 * ROPE_AIM_ANIM + 2 * pose (+1 down); forward is X's own buster anim */
static const u8 rope_pose_of[25] = {0, 0, 0, 0, 3, 1, 2, 3, 1, 2, 3, 1, 2, 0xff, 0xff, 0xff,
                                    0, 0, 0, 3, 1, 2, 3, 1, 2};
static int rope_pose, rope_anim = 145;


static unsigned frame_count;
static u16 last_shot;
static int shooting, aim, crouch, wall, rope, ladder, was_shooting, was_crouch_shot;
static float wall_y;
/* gun tip from the origin (x forward, y down), measured on the aim pictures (PS1 axl_main.c) */
static const signed char tip_ground[5][2] = {{2, -40}, {21, -33}, {32, -13}, {28, 4}, {11, 16}};   /* down: frame 243 */
static const signed char tip_air[5][2] = {{1, -39}, {19, -30}, {32, -11}, {21, 14}, {8, 24}};
static const signed char *tip = tip_ground[D_FWD];
#define CHARGE_STAGE(p) O8(p, 0x155)   /* 1 / 2 (X's: at 30 / 90 frames held) */
#define CHARGE_ID(p) O8(p, 0x14E)      /* the buster shot of the release: 9 / 0x12 */
#define SHOT_COUNT(p) O8(p, 0xBC + 0x95)   /* X's shots out (axl_proj.c OWNER_COUNT) */
static int shot_lent;      /* ours taken off it for X's code (axl_frame -> axl_post) */
static int copy_pose;      /* frames of Axl's shot pose for the copy shot (shoot_before: aiming, no shot for X) */
static int copy_fire_now;  /* the copy shot goes out this frame, from the tip shoot_before picks */
static int hover_t, hover_used, hovering, gliding, adash_t, adash_used;
static u8 limit_orig[2];
static int limit_saved;

static int armor(void)
{
    char *g = *(char **)0x041D8C8C;
    return g ? g[0x70 + 0x9A] : 0;
}

static int on_ground(int st)
{
    return st == 2 || st == 3 || st == 4 || st == 8;
}

static int in_air(int st)
{
    return st == 6 || st == 7;
}

static int on_rope(int st)
{
    return st == 27 || st == 28;
}

/* the shot limit table is shared with the armoured X and Zero's: 8 for Axl, the game's own otherwise */
static void shot_limit(int on)
{
    if (!limit_saved) {
        limit_orig[0] = SHOT_LIMIT[0];
        limit_orig[1] = SHOT_LIMIT2[0];
        limit_saved = 1;
    }
    SHOT_LIMIT[0] = on ? RAPID_LIMIT : limit_orig[0];
    SHOT_LIMIT2[0] = on ? RAPID_LIMIT : limit_orig[1];
}

/* X's muzzle flash sits at X's hand: x = X +- (hand x of the frame), y = X + hand y, from the per-frame table
 * 0x031DBF10 (bank A; FUN_023f0d20). For Axl's shooting frames the hand becomes the gun tip (hand x = -tip x);
 * the table is shared with the armoured X: patched only while Axl plays (PS1 moved the flash at draw time) */
#define HAND_A ((volatile signed char *)0x031DBF10)
typedef struct { u8 frame; const signed char *tip; } hand_t;
/* gun tips on a ladder (user request 2026-10-27: rapid fire and aims there too): forward = X's ladder shot frame 150,
 * then the aims (bank B 74-77, lc2_build.ladder_frames); measured on the built pl02 2026-10-27 (down: by eye, the
 * gun lies along the body) */
static const signed char tip_ladder_fwd[2] = {29, -18};
static const signed char tip_ladder[4][2] = {{0, -40}, {18, -33}, {22, 4}, {0, 14}};
#define LADDER_AIM_B 74
/* gun tips of the wall aims (frames 244 / 245 placed on X's wall shot, lc2_build.extra_frames; measured on the built
 * pl02 as build_axl.gun_tip, which gives frame 62 tip_wall's (40, -8) too; 2026-10-27) */
static const signed char tip_wall_aim[2][2] = {{33, -26}, {35, 13}};
static const hand_t hands[] = {
    {247, tip_ground[0]}, {248, tip_ground[1]}, {249, tip_ground[2]}, {250, tip_ground[3]},
    {243, tip_ground[4]},
    {251, tip_air[0]}, {252, tip_air[1]}, {253, tip_air[2]}, {254, tip_air[3]}, {242, tip_air[4]},
    {212, tip_crouch}, {56, tip_wall}, {62, tip_wall}, {64, tip_wall},
    {244, tip_wall_aim[0]}, {245, tip_wall_aim[1]},
    {150, tip_ladder_fwd}, {155, tip_ladder_fwd},
};
#define N_HANDS (sizeof hands / sizeof hands[0])
static signed char hand_orig[N_HANDS][2];
/* bank B's hand table (FUN_023f0d20, codemap): the rope aims (bank B frames 66 + 2 * pose: up / down diagonal) had
 * X's hand, the flash at Axl's hips (user report 2026-10-27) */
#define HAND_B ((volatile signed char *)0x031DC110)
#define ZIP_AIM_B 66
static signed char hand_b_orig[12][2];   /* the 8 rope aims, then the 4 ladder aims */
static int hand_state = -1;   /* -1 unknown, 0 X's, 1 Axl's */

/* X's bank table (FUN_023aea40: anim -> 0 bank A, 1 bank B = texture 10, PAT section 18): Axl's victory anim 39
 * uses bank B frames 47-55 (lc2_build.VICTORY_B, lc2_hook.PC_SCRIPTS); X's armours keep theirs */
#define X_BANK ((volatile unsigned char *)0x034806B8)
static const unsigned char victory_anims[1] = {39};   /* X's victory (183-186); 95 / 97 were wrong */

static void hand_table(int on)
{
    unsigned i;
    DWORD prot;

    if (hand_state == on)
        return;
    if (hand_state < 0)
        VirtualProtect((void *)X_BANK, 256, PAGE_READWRITE, &prot);
    for (i = 0; i < sizeof victory_anims; i++)
        X_BANK[victory_anims[i]] = (unsigned char)(on != 0);
    if (hand_state < 0) {
        VirtualProtect((void *)HAND_B, 512, PAGE_READWRITE, &prot);
        for (i = 0; i < 12; i++) {
            hand_b_orig[i][0] = HAND_B[2 * (ZIP_AIM_B + i)];
            hand_b_orig[i][1] = HAND_B[2 * (ZIP_AIM_B + i) + 1];
        }
    }
    for (i = 0; i < 12; i++) {   /* rope: pose i / 2, up (even) / down (odd) diagonal; then 74-77 the ladder aims */
        const signed char *t = i < 8 ? tip_rope[i / 2][i & 1 ? 2 : 0] : tip_ladder[i - 8];
        HAND_B[2 * (ZIP_AIM_B + i)] = on ? -t[0] : hand_b_orig[i][0];
        HAND_B[2 * (ZIP_AIM_B + i) + 1] = on ? t[1] : hand_b_orig[i][1];
    }
    if (hand_state < 0) {
        VirtualProtect((void *)HAND_A, 512, PAGE_READWRITE, &prot);
        for (i = 0; i < N_HANDS; i++) {
            hand_orig[i][0] = HAND_A[2 * hands[i].frame];
            hand_orig[i][1] = HAND_A[2 * hands[i].frame + 1];
        }
    }
    for (i = 0; i < N_HANDS; i++) {
        HAND_A[2 * hands[i].frame] = on ? -hands[i].tip[0] : hand_orig[i][0];
        HAND_A[2 * hands[i].frame + 1] = on ? hands[i].tip[1] : hand_orig[i][1];
    }
    hand_state = on;
}

static int aim_from(u16 held, int ground)
{
    int h = held & (B_LEFT | B_RIGHT), v = (held & B_UP) ? 1 : (held & B_DOWN) ? -1 : 0;

    if (v > 0)
        return h ? D_UPDIAG : D_UP;
    if (v < 0)
        return h ? D_DOWNDIAG : D_DOWN;   /* (straight down on the ground too: 2026-10-27) */
    return D_FWD;
}

static int ladder_index(int a)   /* ANIM_LADDER_AIM + i / tip_ladder[i] of an aim that is not forward */
{
    return a == D_UP ? 0 : a == D_UPDIAG ? 1 : a == D_DOWNDIAG ? 2 : 3;
}

static void shoot_before(char *self, char *p)
{
    u16 held = HELD(p), newp = NEWP(p);
    int st = STATE(p), ground = on_ground(st);

    /* no special weapons for Axl (PS1: the special button is the DNA Change / copy shot) */
    held &= ~B_SPECIAL;
    newp &= ~B_SPECIAL;
    crouch = st == ST_CROUCH || (ground && (held & B_DOWN));
    wall = st >= 9 && st <= 11;
    rope = on_rope(st);
    ladder = st == ST_LADDER;
    if (copy_pose)
        copy_pose--;
    shooting = !LOCK(p) && ((held & B_SHOT) || copy_pose) && (ground || in_air(st) || crouch || wall || rope || ladder);
    if (shooting && crouch && st != ST_CROUCH && !was_crouch_shot)   /* standing, shooting with down held: Axl
        * stays up and aims down, X gets no down (user request 2026-10-27: 8 directions on the ground); already
        * crouching when the shooting starts: the crouch shot */
        crouch = 0, ground = 1;
    was_crouch_shot = shooting && crouch;
    wall_y = POS_Y(p);
    if (dna_boss && shooting) {   /* changed: the shot button is the boss's attack, X fires nothing, and he moves as
        * he likes - no stand-still aiming (user report 2026-10-29: Yammark stopped going forward while attacking) */
        HELD(p) = held & ~B_SHOT;
        NEWP(p) = newp & ~B_SHOT;
        return;
    }
    if (!shooting) {   /* (no hold either: X charges from the held shot button wherever Axl cannot shoot - a
                        * cutscene, his hurt or other states; user report 2026-10-27) */
        HELD(p) = held & ~B_SHOT;
        NEWP(p) = newp;
        return;
    }
    if (wall && ((O8(p, 0xB0) | O8(p, 0xB1) | O8(p, 0xF0)) & C_WALLS)) {   /* shooting on a wall: X's cling (state 9
        * with its timer) instead of the slide (11, sparks, dust) - while a wall is still there (user report
        * 2026-11-02: a wall rising away in the Metal Shark stage left him clinging in the air; the timer runs out and
        * X lets go) */
        if (st == 11)
            START_CLING(self);
        if (STATE(p) == 9)
            SET_CLING_TIMER(p + 0xBC, 0, 8);
    }
    aim = ladder ? aim_from(held, 0) : crouch ? D_FWD : wall ? ((held & B_UP) ? D_UPDIAG : (held & B_DOWN) ? D_DOWNDIAG : D_FWD)   /* (user
        * request 2026-10-27: up / down on a wall aim the diagonals, the sheet's steepest wall shots) */
        : rope ? ((held & B_UP) ? D_UPDIAG : (held & B_DOWN) ? D_DOWNDIAG : D_FWD)
                                 : aim_from(held, ground);
    tip = ladder ? (aim == D_FWD ? tip_ladder_fwd : tip_ladder[ladder_index(aim)]) : crouch ? tip_crouch : wall ? (aim == D_FWD ? tip_wall : tip_wall_aim[aim == D_DOWNDIAG]) : ground ? tip_ground[aim] : tip_air[aim];
    if (rope)
        tip = tip_rope[rope_pose][aim == D_UPDIAG ? 0 : aim == D_FWD ? 1 : 2];
    if (rope || ladder) {                 /* left / right still move along it (turn on a ladder), up / down only aim */
        held &= ~(B_UP | B_DOWN);
        newp &= ~(B_UP | B_DOWN);
    } else if (!wall) { /* stand still: left / right only turn, up / down only aim; down on the ground = crouch */
        if (held & B_RIGHT)
            FACING(p) |= FACE_RIGHT;
        else if (held & B_LEFT)
            FACING(p) &= ~FACE_RIGHT;
        held &= ~(B_LEFT | B_RIGHT | B_UP | (crouch ? 0 : B_DOWN));
        newp &= ~(B_LEFT | B_RIGHT | B_UP | (crouch ? 0 : B_DOWN));
    }
    if (!(held & B_SHOT)) {   /* the copy shot's pose: aiming only, no shot for X (copy_pose) */
        HELD(p) = held;
        NEWP(p) = newp & ~B_SHOT;
        return;
    }
    /* rapid fire: X fires on a new press; the hold itself is hidden so X never charges */
    if (newp & B_SHOT)
        last_shot = (u16)frame_count;
    else if ((u16)(frame_count - last_shot) >= RAPID) {
        newp |= B_SHOT;
        last_shot = (u16)frame_count;
    }
    HELD(p) = held & ~B_SHOT;
    NEWP(p) = newp;
}

/* Axl's aim poses (ground 193-196, air 197-201, straight down standing 203) */
static int aim_anim(int a)
{
    return (a >= AIM_GROUND && a <= AIM_AIR + 4) || a == AIM_GROUND_DOWN || a == AIM_WALL || a == AIM_WALL + 1;
}

#define LAND_MAX 10   /* frames of X's landing (state 8) after the shot let go: a plain landing takes 8 */
static void shoot_after(char *p, int was)
{
    static int land_t;
    int st = STATE(p), want;

    /* landed shooting: X's landing then holds its pose some 100 frames after the shot is let go (2026-10-29 e2e:
     * anim 124 16 frames, then 12 for 84 - a plain landing is 8; user report: the landing pose stayed for seconds):
     * past LAND_MAX not shooting, his standing state from its start (state 2, sub 0: X sets its idle) */
    land_t = st == 8 && !shooting ? land_t + 1 : 0;
    if (land_t > LAND_MAX) {
        O8(p, 0x0D) = 2;
        O8(p, 0x0E) = 0;
        set_anim(p, ANIM_IDLE);
        land_t = 0;
        if (test_on)
            test_log("landing held after a shot: standing");
        return;
    }

    if (shooting && wall && st >= 9 && st <= 11 && STATE(p) != 9)   /* no sliding while shooting on a wall; in the
        * cling (9, its timer refilled above) X does not slide, and a moving wall carries him there (user report
        * 2026-11-02: shooting on a moving press he stayed put in the air) - so only out of the cling */
        POS_Y(p) = wall_y;
    if (on_rope(st)) {
        want = ANIM(p) - 144;
        if (want >= 0 && want < 25 && rope_pose_of[want] != 0xff) {   /* X's own anim: its pose */
            rope_pose = rope_pose_of[want];
            rope_anim = ANIM(p);
        }
        want = !shooting ? -1 : aim == D_FWD ? rope_anim : ROPE_AIM_ANIM + 2 * rope_pose + (aim == D_DOWNDIAG);
        if (want < 0 && ANIM(p) >= ROPE_AIM_ANIM)
            want = rope_anim;
        if (want >= 0 && ANIM(p) != want)
            set_anim(p, want);
        return;
    }
    if (st == ST_LADDER) {   /* aims: their bank B anims through X's anim call; forward / let go: X's ladder shot */
        int in_aim = ANIM(p) >= ANIM_LADDER_AIM && ANIM(p) < ANIM_LADDER_AIM + 4;
        want = shooting && aim != D_FWD ? ANIM_LADDER_AIM + ladder_index(aim) : in_aim ? ANIM_LADDER_SHOT : -1;
        if (want >= 0 && ANIM(p) != want)
            X_ANIM_BANK(p, want);
        return;
    }
    if (shooting && wall && st >= 9 && st <= 11) {   /* forward: X's own wall shot */
        want = aim == D_FWD ? (ANIM(p) == AIM_WALL || ANIM(p) == AIM_WALL + 1 ? ANIM_CLING_SHOT : -1)
             : AIM_WALL + (aim == D_DOWNDIAG);
        if (want >= 0 && ANIM(p) != want)
            set_anim(p, want);
    } else if (shooting && !crouch) {
        want = on_ground(st) || st == ST_CROUCH ? (aim == D_DOWN ? AIM_GROUND_DOWN : AIM_GROUND + aim)
             : in_air(st) ? AIM_AIR + aim : -1;
        if (want >= 0 && ANIM(p) != want)
            set_anim(p, want);
    } else if (aim_anim(ANIM(p)) || (on_ground(st) && ANIM(p) == ANIM_FALL)) { /* not shooting, still an aim pose
        * (released, or landed after it) or the fall's on the ground (user report 2026-10-29: shooting in the air,
        * landing, letting go - the fall pose stayed): X's pose for the state (2026-10-27: the air aim stayed) */
        if (test_on)
            test_log("shot let go: state %d anim %d -> X's pose", st, ANIM(p));
        if (st == 8)
            set_anim(p, ANIM_LAND);   /* (its end takes X's landing on to standing; the idle loop never ends it) */
        else if (st == 2)
            set_anim(p, ANIM_IDLE);
        else if (st == 3)
            set_anim(p, ANIM_WALK);
        else if (st == 4)
            set_anim(p, ANIM_DASH);
        else if (in_air(st))
            set_anim(p, ANIM_FALL);
        else if (st >= 9 && st <= 11)
            set_anim(p, ANIM_CLING);
    }
}

static void hover_before(char *p)
{
    int jump_new = (NEWP(p) & B_JUMP) != 0;

    /* air dash: a new dash press in the air, once per jump: AIR_DASH frames at the dash-jump speed, no fall; also
     * out of a wall kick (ST_WALL_KICK, user report 2026-10-29 stream: holding the way into the wall, the dash after a
     * wall kick never came) - then the way he kicks off (facing), not the held way back into the wall */
    if (!in_air(STATE(p)) && STATE(p) != ST_WALL_KICK)
        adash_t = 0;
    else if (!adash_used && !shooting && (NEWP(p) & B_DASH)) {   /* (not while shooting: user request 2026-10-26) */
        adash_t = AIR_DASH;
        adash_used = 1;
        hovering = 0;
        axl_sound(SND_DASH);   /* (our own move: X's code plays nothing for it - user report 2026-10-29) */
        adash_kick = STATE(p) == ST_WALL_KICK;
        if (adash_kick)   /* (kicking, he faces the wall: the dash goes the other way) */
            adash_right = !(FACING(p) & FACE_RIGHT);
        else
            adash_right = (HELD(p) & B_RIGHT) ? 1 : (HELD(p) & B_LEFT) ? 0 : (FACING(p) & FACE_RIGHT) != 0;
        if (test_on)
            test_log("air dash start state %d facing %s", STATE(p), (FACING(p) & FACE_RIGHT) ? "right" : "left");
    }
    if (adash_t) {
        adash_t--;
        DASH_JUMP(p) = 1;
        if (adash_right)   /* (kept: X's kick turns him to the wall each frame) */
            FACING(p) |= FACE_RIGHT;
        else
            FACING(p) &= ~FACE_RIGHT;
        HELD(p) = (HELD(p) & ~(B_LEFT | B_RIGHT)) | (adash_right ? B_RIGHT : B_LEFT);
        VY(p) = 0;
        GRAVITY(p) = adash_t ? 0 : X_GRAVITY(p);
        AFTERIMAGE(p) = adash_t != 0;
        return;
    }
    /* hover: a new jump press in the air (not in a dash jump), or shooting once he no longer rises */
    /* (White Axl: again and again in one jump, user request 2026-10-27: shooting again after a pause keeps him up;
     * a jump press too, so the Ultimate's own hover never gets one) */
    if (!hovering && (!hover_used || armor() == 4) && in_air(STATE(p)) &&
        ((jump_new && !DASH_JUMP(p)) || (shooting && VY(p) <= 0))) {
        hovering = hover_used = 1;
        hover_t = 0;
        NEWP(p) &= ~B_JUMP;
    }
    if (!hovering)
        return;
    if (!((HELD(p) & B_JUMP) || shooting) || !in_air(STATE(p)) || (armor() != 4 && hover_t >= HOVER_MAX)) {
        hovering = 0;
        GRAVITY(p) = X_GRAVITY(p);
        if (in_air(STATE(p)) && ANIM(p) == ANIM_HOVER)
            set_anim(p, ANIM_FALL);
        return;
    }
    hover_t++;
    VY(p) = 0;
    GRAVITY(p) = 0;
}

/* the hover's burner loop (X8's, request 30) while Axl hovers (user report 2026-10-29: the 0.6 s start alone was not heard) */
static void hover_sound(void)
{
    static int on;
    if (hovering && !on)
        axl_sound(SND_HOVER);
    else if (!hovering && on)
        axl_sound_stop(SND_HOVER);
    on = hovering;
}

static void hover_after(char *p)
{
    int st = STATE(p);

    if (!in_air(st)) {
        if (hovering || gliding)
            GRAVITY(p) = X_GRAVITY(p);
        hover_used = hovering = gliding = 0;
        if (st != ST_WALL_KICK || !adash_kick)   /* (a dash out of the kick stays used until the next wall or floor) */
            adash_used = adash_kick = 0;
        return;
    }
    if (adash_t && !shooting) {
        static const unsigned char adash_frames[2] = {75, 73};   /* (anim 16's dash loop, a frame each) */
        if (ANIM(p) != ANIM_AIR_DASH) {
            X_ANIM_BANK(p, ANIM_FALL);            /* (bank A: an air shot may have left bank B) */
            set_anim(p, ANIM_AIR_DASH);
        }
        /* its picture by time: after an air shot X sets its own anim back every frame and the dash anim started
         * over on its first picture, a half pose (user report 2026-10-27) */
        O8(p, 0x66) = AIR_DASH - adash_t <= 1 ? 71 : adash_frames[adash_t & 1];
    } else if (!adash_t && ANIM(p) == ANIM_AIR_DASH)
        set_anim(p, ANIM_FALL);
    if (hovering) {
        VY(p) = 0;
        GRAVITY(p) = 0;
        if (!shooting && ANIM(p) != ANIM_HOVER)
            set_anim(p, ANIM_HOVER);
    } else {
        int g = hover_used && hover_t >= HOVER_MAX && (HELD(p) & B_JUMP);

        if (g != gliding) {
            gliding = g;
            GRAVITY(p) = g ? X_GRAVITY(p) / GLIDE_DIV : X_GRAVITY(p);
        }
    }
}

/* bullets: X's shot objects, work laid out as P: PS1 header byte k at +8+k (+8 flags 0x21, +9 id, +0xA sub,
 * +0xB state, +0xC init), x / y floats +0x14 / +0x1C (PS1 +8 / +0xC, pixels), vx / vy 16.16 +0x3C / +0x40,
 * anim +0x2B, frame +0x66, owner +0xC0. A shot is set up by its own first update (the frame after it is made),
 * then it is aimed here (PS1 aim_one did it at draw time; on PC the shot shows one frame at X's hand first). */
#define N_TRACK 32
#define DIAG 181              /* 256 * cos 45 */
#define GUARD_EVERY 8         /* X8: every 8th bullet breaks guards, drawn blue */
#define GUARD_EVERY_WHITE 4   /* White Axl (armour 4): every 4th (user request 2026-10-27: shorter) */
#define W_X(w) (*(volatile float *)((w) + 0x14))
#define W_Y(w) (*(volatile float *)((w) + 0x1C))
typedef struct { char *w; int age, dir, id, fixed, guard; s32 vx, vy; float x, y; const signed char *tip; } track_t;
static track_t track[N_TRACK];
int axl_guard_test;   /* test (guard_all): every bullet breaks guards */
static char *player_p, *player_t;
char *axl_self, *axl_p;
unsigned axl_frames;   /* X main calls (dna_watch: the player has stopped running) */
static int shot_count;
#define COPY_FULL_ID 0x12  /* X's full charge shot id (written to its charge fields while charging) */

/* X's shot spawner FUN_023d4a50(this = player class, kind 1, id, sub, P): thiscall, 4 stack args */
typedef void(__fastcall *spawn_t)(char *, void *, int, int, int, char *);
#define SPAWN_SHOT ((spawn_t)0x023D4A50)

/* a shot made by our own code: its position and velocity once it is set up (copy shot, Giga Crush) */
static struct { int on; s32 vx, vy; float x, y; } spawn_fixed;
static int log_fx;   /* test: log the other objects made while shooting */
static char *look_p;   /* axl_box -> axl_look_late: the player whose look is due, and his shooting */
static int look_fire;
static int special_new;   /* the special button was pressed this frame (read before it is hidden from X) */
static int shot_new, alt_new;   /* the shot / R button pressed this frame (changed: alt_new = the special button, the DNA
                                 * button, and special_new = R, the change's end) */

static void spawn_shot(char *self, char *p, int id, float x, float y, s32 vx, s32 vy)
{
    spawn_fixed.on = 1;
    spawn_fixed.x = x;
    spawn_fixed.y = y;
    spawn_fixed.vx = vx;
    spawn_fixed.vy = vy;
    {
        u8 req = O8(p, 0x14F);   /* B+0x93: the shot X's fire code asked for (PS1 +0x96), read by the shot's set-up */
        O8(p, 0x14F) = (u8)id;
        SPAWN_SHOT(self, 0, 1, id, 0, p);
        O8(p, 0x14F) = req;
    }
    spawn_fixed.on = 0;
}

static char *fx_seen[16];
static int fx_age[16];

void axl_allocated(char *o, int pool, int a, int b)
{
    int i;
    objs_alloc(o, pool, a, b);
    proj_allocated(o, pool, a, b);
    if (!log_fx || pool != 6)
        return;
    test_log("alloc pool %d a %d b %d -> %p", pool, a, b, o);
    for (i = 0; i < 16 && fx_seen[i]; i++)
        ;
    if (i < 16) {
        fx_seen[i] = o;
        fx_age[i] = 0;
    }
}

static void fx_log(void)
{
    int i;
    for (i = 0; i < 16; i++)
        if (fx_seen[i]) {
            char *o = fx_seen[i];

            if (fx_age[i] == 1 || fx_age[i] == 3) {
                char hex[0x80 * 2 + 1];
                int k;
                for (k = 0; k < 0x80; k++)
                    wsprintfA(hex + 2 * k, "%02x", (unsigned char)o[k]);
                test_log("fx %p age %d %s", o, fx_age[i], hex);
            }
            if (++fx_age[i] > 12)
                fx_seen[i] = 0;
        }
}

/* axl_dna.c: the change starts / its pose ends: no dash carried into the boss (user report 2026-10-27: changing
 * while dashing flew off at dash speed) - X's dash-jump flag, Axl's air dash, its afterimages, the run speed */
void axl_dash_cancel(char *p)
{
    DASH_JUMP(p) = 0;
    adash_t = 0;
    AFTERIMAGE(p) = 0;
    O32(p, 0x3C) = 0;   /* (vx) */
}

/* axl_proj.c setup_hook: X's shot w has just been set up (its first update) */
void axl_shot_setup(char *w)
{
    int i;
    for (i = 0; i < N_TRACK; i++)
        if (track[i].w == w && track[i].guard) {
            O8(w, 9) = GUARD_ID;
            O8(w, 0x5E) = GUARD_SLOT;
        }
}

void axl_spawned(char *w, int kind, int id, int sub, char *owner)
{
    int i;
    if (proj_spawned(w))
        return;
    if (test_on && player_p && kind != 1 && log_fx)
        test_log("made kind %d id %d sub %d owner %p -> %p", kind, id, sub, owner, w);
    if (!player_p || owner != player_p || kind != 1)
        return;
    for (i = 0; i < N_TRACK && track[i].w; i++)
        ;
    if (i == N_TRACK)
        return;
    track[i].w = w;
    track[i].age = 0;
    track[i].dir = aim;
    track[i].id = id;
    /* the guard-break bullet is chosen when it is made: its damage id goes in at its set-up (axl_shot_setup), before
     * anything it touches at the gun tip tests it (set at the aim, a frame later, it was a plain bullet point-blank) */
    track[i].guard = id == 0 &&
                     (++shot_count % (armor() == 4 ? GUARD_EVERY_WHITE : GUARD_EVERY) == 0 || axl_guard_test);
    track[i].tip = tip;
    track[i].fixed = spawn_fixed.on;
    if (spawn_fixed.on) {   /* before its first update: shots that do not take X's hand position start here */
        W_X(w) = spawn_fixed.x;
        W_Y(w) = spawn_fixed.y;
        FACING(w) = spawn_fixed.vx >= 0 ? FACE_RIGHT : 0;
    }
    track[i].vx = spawn_fixed.vx;
    track[i].vy = spawn_fixed.vy;
    track[i].x = spawn_fixed.x;
    track[i].y = spawn_fixed.y;
    if (test_on)
        test_log("spawn w %p id %d sub %d dir %d", w, id, sub, aim);
}

/* Axl's bullets are drawn turned to where they fly: X's shot flies as anim 0x15 (frame 4 launch, 5-9 flying) of the
 * shot table 0x03480FC0 with com00's tiles; the build adds the flying frames turned in SHOT_DIRS directions to the
 * bosses' projectile texture / parts section (lc2_dna.shot_frames). Each direction gets a copy of the table whose
 * anim 0x15 shows them; a bullet takes the table of its direction (and keeps its place in the script) every frame,
 * so a homing bullet turns smoothly. X's picture faces left and is mirrored for facing right (W+0x29 & 0x40). */
#define SHOT_TABLE ((void **)0x03480FC0)
#define SHOT_ANIM 0x15
#define SHOT_TABLE_N 64
#define SHOT_SCRIPT_MAX 16
typedef struct { const void *vt; u8 dur, flag; signed char step; u8 frame; } ent_t;
static void *dir_table[SHOT_DIRS][SHOT_TABLE_N];
static ent_t dir_script[SHOT_DIRS][SHOT_SCRIPT_MAX];
static int dir_n;   /* entries of the script (0 = not made yet) */

static void dir_init(void)
{
    const ent_t *src = (const ent_t *)SHOT_TABLE[SHOT_ANIM];
    int d, i, n;

    for (n = 0; n < SHOT_SCRIPT_MAX; n++)          /* contiguous entries up to the loop / hold */
        if (src[n].step <= 0) {
            n++;
            break;
        }
    for (d = 0; d < SHOT_DIRS; d++) {
        for (i = 0; i < SHOT_TABLE_N; i++)
            dir_table[d][i] = SHOT_TABLE[i];
        for (i = 0; i < n; i++) {
            dir_script[d][i] = src[i];
            if (src[i].frame >= 4 && src[i].frame <= 9)   /* (the launch frame: the first flying one) */
                dir_script[d][i].frame = SHOT_BASE + SHOT_NFLY * d + (src[i].frame <= 5 ? 0 : src[i].frame - 5);
        }
        dir_table[d][SHOT_ANIM] = dir_script[d];
    }
    dir_n = n;
    if (test_on)
        test_log("bullet direction scripts: %d entries, frames %d %d", n, src[0].frame, src[n > 1].frame);
}

/* the picture of a bullet flying at `deg` (counter-clockwise on screen, 0 = right) */
static void bullet_turn(char *w, float deg)
{
    const ent_t *cur = *(const ent_t *const *)(w + 0x50), *base;
    int d = (int)floorf(deg * SHOT_DIRS / 360.0f + 0.5f), k, idx = 0;

    if (!dir_n)
        dir_init();
    k = (FACING(w) & FACE_RIGHT) ? -d : d - SHOT_DIRS / 2;   /* the source faces left; facing right mirrors it */
    k &= SHOT_DIRS - 1;
    base = (const ent_t *)SHOT_TABLE[SHOT_ANIM];
    if (cur >= base && cur < base + dir_n)
        idx = (int)(cur - base);
    for (d = 0; d < SHOT_DIRS; d++)
        if (cur >= dir_script[d] && cur < dir_script[d] + dir_n)
            idx = (int)(cur - dir_script[d]);
    if (P_TABLE(w) == dir_table[k] && cur == &dir_script[k][idx])
        return;
    O32(w, 0x54) = FX_TEX_INDEX;                  /* the turned pictures' texture (common list) and parts section */
    O32(w, 0x58) = FX_PAT;
    P_TABLE(w) = dir_table[k];
    *(const ent_t **)(w + 0x50) = &dir_script[k][idx];
    O8(w, 0x66) = dir_script[k][idx].frame;
}

/* Homing (user request 2026-10-26): a bullet bends toward the nearest enemy ahead, slightly: only one within
 * HOME_CONE degrees of its course and HOME_RANGE px, turning at most HOME_TURN degrees a frame and HOME_TOTAL in
 * all (2026-10-26: 50 / 2.5 bent too much, 15 / 0.8 / 12 too little; now 25 / 1.5 / 30) */
#define HOME_RANGE 200
#define HOME_CONE 25.0f
#define HOME_TURN 1.0f    /* 2026-11-02 (user: bend less): 1.5 / 30, near 8 / 60 / 60 */
#define HOME_TOTAL 20.0f
/* a target close to the bullet (user report 2026-10-29: a near enemy a little above / below was missed - outside the
 * cone, or 1.5 degrees a frame too slow to reach it before passing): within HOME_NEAR px the cone, the turn and the
 * total grow toward HOME_NEAR_CONE / _TURN / _TOTAL as it gets closer */
#define HOME_NEAR 96.0f
#define HOME_NEAR_CONE 45.0f
#define HOME_NEAR_TURN 5.0f
#define HOME_NEAR_TOTAL 40.0f
#define N_HOME 32
static char *home[N_HOME];
static float home_turned[N_HOME];   /* degrees turned so far */

static int own_bullet(char *p, char *w)
{
    return O8(w, 8) && *(char **)(w + 0xC0) == p && (O8(w, 9) == 0 || O8(w, 9) == GUARD_ID || O8(w, 9) == GUARD_STUN_ID || O8(w, 9) == GUARD_WEAK_ID) && O8(w, 0xC) == 1 &&
           !proj_of(w);
}

/* the centre of an object's hurt box (box object {vtable, dx, dy, w, h}; facing right mirrors dx) */
static void box_centre(char *e, float *x, float *y)
{
    const signed char *b = *(const signed char *const *)(e + 0x7C);
    float bx = (float)b[4] + (unsigned char)b[6] * 0.5f;

    *x = W_X(e) + ((FACING(e) & FACE_RIGHT) ? -bx : bx);
    *y = W_Y(e) + (float)b[5] + (unsigned char)b[7] * 0.5f;
}

static void homing(char *p)
{
    int i;
    char *w, *e;
    float vx, vy, sp, a, ta, diff, ex, ey, cone, turn, total;

    for (i = 0; i < N_HOME; i++) {
        w = home[i];
        if (!w)
            continue;
        if (!own_bullet(p, w)) {
            home[i] = 0;
            continue;
        }
        vx = O32(w, 0x3C) / 65536.0f;
        vy = O32(w, 0x40) / 65536.0f;             /* up */
        sp = sqrtf(vx * vx + vy * vy);
        if (sp < 0.5f)
            continue;
        a = atan2f(vy, vx) * 57.29578f;
        if ((e = find_target(W_X(w), W_Y(w), HOME_RANGE)) != 0) {
            box_centre(e, &ex, &ey);
            ta = atan2f(W_Y(w) - ey, ex - W_X(w)) * 57.29578f;
            diff = ta - a;
            while (diff > 180)
                diff -= 360;
            while (diff < -180)
                diff += 360;
            {
                float dx = ex - W_X(w), dy = ey - W_Y(w), d = sqrtf(dx * dx + dy * dy);
                float closeness = d < HOME_NEAR ? (HOME_NEAR - d) / HOME_NEAR : 0;   /* 0 far .. 1 at the bullet */
                cone = HOME_CONE + (HOME_NEAR_CONE - HOME_CONE) * closeness;
                turn = HOME_TURN + (HOME_NEAR_TURN - HOME_TURN) * closeness;
                total = HOME_TOTAL + (HOME_NEAR_TOTAL - HOME_TOTAL) * closeness;
            }
            if (diff > -cone && diff < cone && home_turned[i] < total) {
                diff = diff > turn ? turn : diff < -turn ? -turn : diff;
                a += diff;
                home_turned[i] += diff < 0 ? -diff : diff;
                O32(w, 0x3C) = (s32)(sp * cosf(a / 57.29578f) * 65536);
                O32(w, 0x40) = (s32)(sp * sinf(a / 57.29578f) * 65536);
            }
        }
        bullet_turn(w, a < 0 ? a + 360 : a);
    }
}

static void home_add(char *w)
{
    int i;
    for (i = 0; i < N_HOME; i++)
        if (home[i] == w)
            return;
    for (i = 0; i < N_HOME; i++)
        if (!home[i]) {
            home[i] = w;
            home_turned[i] = 0;
            return;
        }
}

static void aim_velocity(char *w, int dir, int right)
{
    s32 sp = O32(w, 0x3C), vx, vy;

    if (sp < 0)
        sp = -sp;
    switch (dir) {
    case D_UP: vx = 0; vy = sp; break;
    case D_UPDIAG: vx = sp * DIAG >> 8; vy = sp * DIAG >> 8; break;
    case D_DOWNDIAG: vx = sp * DIAG >> 8; vy = -(sp * DIAG >> 8); break;
    case D_DOWN: vx = 0; vy = -sp; break;
    default: vx = sp; vy = 0; break;
    }
    O32(w, 0x3C) = right ? vx : -vx;
    O32(w, 0x40) = vy;
}

/* one new bullet: start at the gun tip with the aimed velocity; every GUARD_EVERY-th becomes a guard-break bullet */
static void aim_one(char *p, track_t *t)
{
    char *w = t->w;
    int right = FACING(p) & FACE_RIGHT;

    W_X(w) = *(float *)(p + 0x14) + (right ? t->tip[0] : -t->tip[0]);
    W_Y(w) = *(float *)(p + 0x1C) + t->tip[1];
    aim_velocity(w, t->dir, right);
    if (t->id == 0)
        home_add(w);
    if (t->guard) {
        O8(w, 9) = GUARD_ID;            /* sabers and shields do not stop it */
        O8(w, 0x5E) = GUARD_SLOT;       /* the same bullet in blue (user request 2026-10-26) */
    }
}

static void bullets(char *p)
{
    int i;
    for (i = 0; i < N_TRACK; i++) {
        track_t *t = &track[i];
        char *w = t->w;
        if (!w)
            continue;
        if (t->age == 1 && O8(w, 8) && O8(w, 0xC) == 1 && *(char **)(w + 0xC0) == p && t->fixed) {
            W_X(w) = t->x;
            W_Y(w) = t->y;
            O32(w, 0x3C) = t->vx;
            O32(w, 0x40) = t->vy;
            FACING(w) = t->vx >= 0 ? FACE_RIGHT : 0;
            if (test_on)
                test_log("fixed w %p id %d hdr %02x %02x %02x %02x %02x xy %.1f %.1f v %x %x anim %d frame %d", w,
                         t->id, O8(w, 8), O8(w, 9), O8(w, 0xA), O8(w, 0xB), O8(w, 0xC), W_X(w), W_Y(w), O32(w, 0x3C),
                         O32(w, 0x40), O8(w, 0x2B), O8(w, 0x66));
        } else if (t->age == 1 && O8(w, 8) && O8(w, 0xC) == 1 && *(char **)(w + 0xC0) == p) {
            aim_one(p, t);
            if (test_on)
                test_log("aimed w %p id %d dir %d xy %.1f %.1f v %x %x", w, t->id, t->dir, W_X(w), W_Y(w),
                         O32(w, 0x3C), O32(w, 0x40));
        }
        if (test_on && t->fixed && t->age >= 1 && t->age <= 6)
            test_log("  w %p age %d hdr %02x %02x %02x %02x %02x xy %.1f %.1f v %x %x anim %d frame %d", w, t->age,
                     O8(w, 8), O8(w, 9), O8(w, 0xA), O8(w, 0xB), O8(w, 0xC), W_X(w), W_Y(w), O32(w, 0x3C),
                     O32(w, 0x40), O8(w, 0x2B), O8(w, 0x66));
        if (test_on && t->age == 1 && (t->id == 9 || shot_count == 3)) {
            char hex[0xC0 * 2 + 1];
            int k;
            for (k = 0; k < 0xC0; k++)
                wsprintfA(hex + 2 * k, "%02x", (unsigned char)w[k]);
            test_log("wbytes id %d %s", t->id, hex);
        }
        if (++t->age > (test_on && t->fixed ? 6 : 1))
            t->w = 0;
    }
}

/* copy shot (X8; user request 2026-10-27: the sheet's ring bullet, fired with the gun's motion, growing and
 * shrinking, two small bullets turning round it in an X, its path drawn behind it; only from a charge): with the
 * plain gun (weapon 0) the special button charges (below) and, let go at the full stage, Axl takes his shot
 * pose and the ring flies from the gun tip in the aim, one at a time (copy_fire). Pictures: lc2_dna.copy_pictures, colours in the hover jets' palette slot (flame_pal,
 * hover_jets). (PS1: X8's copy shot sound; DNA from copy-shot kills: not yet) */
#define COPY_ID 0     /* Axl's bullet's id: 1 damage, gone at its first hit (user request 2026-10-27; 0x12 pierced, many hits) */
#define SPECIAL_SHOTS 1
#define COPY_SPEED 3.0f      /* px per frame (user request 2026-10-27: slow) */
#define COPY_LIFE 120
#define COPY_PULSE 2          /* frames per ring size */
#define COPY_ORB_R 15         /* the small bullets: ellipses along the diagonals, COPY_ORB_R x COPY_ORB_R2 */
#define COPY_ORB_R2 5
#define COPY_ORB_T 20         /* frames per turn */
#define COPY_TRAIL_EVERY 2
#define COPY_TRAIL_LIFE 9     /* COPY_FADE steps */
#define WEAPON(p) O8(p, 0x14C)                 /* PS1 +0x93 */
#define GIGA_GAUGE(p) (*(volatile short *)((p) + 0x1A0))   /* PS1 +0xA8: weapon energy[0] (B + 0xE4) */
#define ENERGY_MAX(p) (*(volatile short *)((p) + 0x2CC))   /* PS1 +0x14A (B + 0x210) */
static char *copy_w, *copy_orb[2];
static float copy_px, copy_py;
static int copy_fx_t;   /* frames its pictures (ring, small bullets, trail) still need the palette */
static int jet_slot = -1;
static const signed char copy_box[4] = {-10, -10, 20, 20};
static const unsigned char copy_sizes[8] = {0, 1, 2, 3, 4, 3, 2, 1};

static void fx_look(char *w, proj *q, int i)   /* copy shot picture i, in the jets' palette */
{
    (void)q;
    proj_fx_look(w, COPY_BASE + i);
    O16(w, 0x5E) = (unsigned short)(jet_slot >= 0 ? jet_slot : 0);
}

static void copy_fn(char *w, proj *q)
{
    fx_look(w, q, copy_sizes[(q->t / COPY_PULSE) & 7]);
}

static void muzzle_fn(char *w, proj *q)   /* the copy shot's muzzle flash: the ring opening up, small to big */
{
    fx_look(w, q, q->t < COPY_N_RING ? q->t : COPY_N_RING - 1);
    O32(w, 0x3C) = O32(w, 0x40) = 0;
}

static int copy_out(void)
{
    if (copy_w && !proj_of(copy_w))
        copy_w = 0;
    return copy_w != 0;
}

static void orb_fn(char *w, proj *q)   /* q->a = which one (its diagonal) */
{
    proj *c = copy_w ? proj_of(copy_w) : 0;
    float a, u, v;

    if (!c) {
        proj_kill(w);
        return;
    }
    a = 6.2831853f * (float)(q->t % COPY_ORB_T) / COPY_ORB_T + (q->a ? 3.1415927f : 0);
    u = COPY_ORB_R * cosf(a);
    v = COPY_ORB_R2 * sinf(a);
    *(volatile float *)(w + 0x14) = *(volatile float *)(copy_w + 0x14) + 0.7071f * (u + v);
    *(volatile float *)(w + 0x1C) = *(volatile float *)(copy_w + 0x1C) + 0.7071f * (q->a ? v - u : u - v);
    O32(w, 0x3C) = O32(w, 0x40) = 0;
    O8(w, 0x2A) = sinf(a) > 0 ? 3 : 0;   /* behind the ring on the far half of the turn */
    fx_look(w, q, COPY_ORB - COPY_BASE);
}

static void trail_fn(char *w, proj *q)   /* q->a = direction (0 level, 1 "\", 2 upright, 3 "/") */
{
    int k = q->t * COPY_FADE / COPY_TRAIL_LIFE;
    fx_look(w, q, COPY_TRAIL - COPY_BASE + COPY_FADE * q->a + (k < COPY_FADE ? k : COPY_FADE - 1));
}

static char *fx_spawn(float x, float y, int life, proj_fn fn, int a, int pic)
{
    char *w = proj_spawn(0, x, y, 0, 0, 0, life, fn, P_PIERCE | P_TOUGH | P_WALL | P_NOSCREEN);
    if (w) {
        proj_nobox(w);
        proj_of(w)->a = a;
        proj_fx_look(w, COPY_BASE + pic);   /* (before its set-up: else X's bullet shows for a frame) */
    }
    return w;
}

/* charging (user request 2026-10-27: the copy shot only from a charge, with X's own charge): the special button
 * held counts frames and writes X's charge fields as X's own charge does (its stage drives X's charge glow; X never
 * charges for Axl: the shot button is never held for X); let go at the full stage, Axl takes his shot pose (Axl's
 * shot pose and aim, nothing for X: no X bullet, no X muzzle flash; user idea 2026-10-27) and the ring goes out
 * from the gun tip (copy_fire). Before: nothing. */
#define COPY_STAGE1 30
#define COPY_STAGE2 90
#define COPY_POSE 16          /* frames of the shot pose */
#define COPY_SOUND_AT 25      /* X's charge sound starts 25 frames into the hold */
static int copy_hold;                  /* frames the special button has been held */

static int opt_auto, part_full, part_quick;   /* this frame's auto charge / full-charge / quick-charge (charge_hide) */
static void copy_charge(char *p, int held)   /* 1 held (a frame on), 0 let go, -1 broken off */
{
    if (held > 0) {
        /* shooting meanwhile keeps the charge (user request 2026-10-29: as the auto charge; until then it started
         * over): X's charge fields stay 0 through X's update - X makes its shot's id from them, a plain shot would
         * come out as its charge buster - and get the stage after it, for the glow (axl_post) */
        if (held == 1 && copy_hold < COPY_STAGE2) {
            int was = copy_hold;
            copy_hold += part_quick ? 2 : 1;
            if (copy_hold > COPY_STAGE2)
                copy_hold = COPY_STAGE2;
            if (was < COPY_SOUND_AT && copy_hold >= COPY_SOUND_AT)
                axl_sound(SND_CHARGE);   /* X's charge sound (at X's time; looped until let go) */
        }
        CHARGE_STAGE(p) = 0;
        CHARGE_ID(p) = 0;
        return;
    }
    if (test_on)
        test_log("copy charge let go %d hold %d state %d", held, copy_hold, STATE(p));
    if (copy_hold >= COPY_SOUND_AT)
        axl_sound_stop(SND_CHARGE);
    if (held == 0 && copy_hold >= COPY_STAGE2 && !copy_out()) {
        copy_pose = COPY_POSE;   /* Axl's shot pose and aim (shoot_before), the ring from its tip (copy_fire) */
        copy_fire_now = 1;
    }
    copy_hold = 0;
    CHARGE_STAGE(p) = 0;
    CHARGE_ID(p) = 0;
}

/* The collection's Auto Charge and the full-charge part (user request 2026-10-29: X's charged buster came out of
 * them): the option byte G + 0x175 (st + 0x105, PS1 0x800CCF99) bit 0 = auto charge; the equipped parts P + 0x258
 * (B + 0x19C) bit 0x80000 = every shot a full charge (Ultimate Buster, name a guess), 0x100000 = charge at half time
 * (Quick Charge); bit 1 = held rapid fire (Axl's own anyway). For Axl they are hidden from X's update (axl_frame ..
 * axl_post: X neither charges nor fires its charge) and turn into the copy shot's: auto - it charges by itself, the
 * special button fires it once full; the full-charge part - the special button fires it at once (the part as worn:
 * the parts screen's X word st + 0xB0). The limited Overdrive is left to X: while it runs, Axl's shots are X's charge
 * buster (user decision 2026-10-29). */
#define OPT_BYTE ((volatile unsigned char *)(*(char **)0x041D8C8C + 0x175))
#define PARTS_EQ(p) (*(volatile unsigned *)((p) + 0x258))
#define PART_FULL 0x80000u
#define PART_QUICK 0x100000u
/* the limited part in use B + 0x1FC (P + 0x2B8; PS1 P+0x144): 6 = Overdrive, every buster shot charged for 480
 * frames (FUN_023aed40; docs/research-x6-parts-bits-20261029.md) - its timer runs outside X's update (FUN_020afc40) */
#define LIMITED(p) (*(volatile int *)((p) + 0x2B8))
#define LIMITED_OVERDRIVE 6
static char *hidden_p;
static unsigned char hidden_opt;
static unsigned hidden_parts;
static int hidden_limited;

static void charge_hide(char *p)
{
    if (hidden_p || !*(char **)0x041D8C8C)
        return;
    hidden_p = p;
    hidden_opt = *OPT_BYTE;
    hidden_parts = PARTS_EQ(p);
    opt_auto = hidden_opt & 1;
    hidden_limited = LIMITED(p);
    {   /* (the X parts as equipped, st + 0xB0, too: in case P's copy is not where X reads it) */
        char *g = *(char **)0x041D8C8C;
        unsigned eq = g ? *(volatile unsigned *)(g + 0x70 + 0xB0) : 0, eqz = g ? *(volatile unsigned *)(g + 0x70 + 0xB4) : 0;
        static unsigned last_p = 1, last_eq = 1;
        if (test_on && (hidden_parts != last_p || eq != last_eq))
            test_log("parts: P+0x258 %08x, equipped X st+0xB0 %08x Zero st+0xB4 %08x, char st+0x74 %d, P+0x0A %d, "
                     "limited %d", hidden_parts, eq, eqz, g ? *(volatile unsigned char *)(g + 0x70 + 0x74) : -1,
                     *(volatile unsigned char *)(p + 0x0A), hidden_limited);
        last_p = hidden_parts;
        last_eq = eq;
        /* the parts screen's X word is what Axl wears (2026-10-29 play: st + 0xB0 0x000A0130 with the Ultimate Buster,
         * P + 0x258 0 - not copied there for normal X) */
        part_full = ((hidden_parts | eq) & PART_FULL) != 0;   /* (Overdrive left to X: its shots are X's charge
                                                               * buster for its 480 frames - user decision 2026-10-29) */
        part_quick = ((hidden_parts | eq) & PART_QUICK) != 0;
    }
    *OPT_BYTE = (unsigned char)(hidden_opt & ~3);   /* (bit 1, held = rapid fire, too: Axl fires so anyway - user
                                                     * request 2026-10-29, the option on or off no difference) */
    PARTS_EQ(p) = hidden_parts & ~PART_FULL;
}

static void charge_unhide(void)
{
    if (!hidden_p)
        return;
    if (*(char **)0x041D8C8C)
        *OPT_BYTE = (unsigned char)((*OPT_BYTE & ~3) | (hidden_opt & 3));
    /* (put back only if the game left it as hidden: the stage start copies the worn word during X's update -
     * FUN_025446c0 - and restoring over that took the Ultimate Buster out of it, pause menu and all; 2026-10-29) */
    if (PARTS_EQ(hidden_p) == (hidden_parts & ~PART_FULL))
        PARTS_EQ(hidden_p) = hidden_parts;
    hidden_p = 0;
}

/* the copy shot by itself (auto charge) or at once (the full-charge part): 1 = it took the special button */
#define COPY_GAP 40   /* frames between instant copy shots (user request 2026-10-29: the full-charge part must not
                       * make them rapid fire) */
static int copy_gap;

static int copy_auto(char *p, int sp_new)
{
    if (copy_gap)
        copy_gap--;
    if (part_full) {
        if (sp_new && !copy_out() && !copy_gap) {
            copy_gap = COPY_GAP;
            copy_hold = COPY_STAGE2;
            copy_charge(p, 0);
        }
        return 1;
    }
    if (!opt_auto)
        return 0;
    if (sp_new && copy_hold >= COPY_STAGE2) {
        copy_charge(p, 0);
        return 1;
    }
    if (copy_hold < COPY_STAGE2) {
        int was = copy_hold;
        copy_hold += part_quick ? 2 : 1;
        if (copy_hold > COPY_STAGE2)
            copy_hold = COPY_STAGE2;
        if (was < COPY_SOUND_AT && copy_hold >= COPY_SOUND_AT)
            axl_sound(SND_CHARGE);
    }
    CHARGE_STAGE(p) = 0;   /* (its glow after X's update only: axl_post - else X's next shot is the charge's) */
    CHARGE_ID(p) = 0;
    return 1;
}

static void copy_launch(float x, float y, float ux, float uy)   /* (ux, uy): the unit direction, y up */
{
    int i;

    copy_w = proj_spawn(COPY_ID, x, y, (s32)(ux * COPY_SPEED * 65536), (s32)(uy * COPY_SPEED * 65536), 0,
                        COPY_LIFE, copy_fn, 0);
    if (!copy_w)
        return;
    proj_box(copy_w, copy_box);
    proj_fx_look(copy_w, COPY_BASE);
    copy_px = x;
    copy_py = y;
    for (i = 0; i < 2; i++)
        copy_orb[i] = fx_spawn(x, y, 0, orb_fn, i, COPY_ORB - COPY_BASE);
    fx_spawn(x, y, COPY_N_RING + 1, muzzle_fn, 0, 0);
}

/* the ring from the gun tip in the aim (shoot_before's aim / tip this frame) */
static void copy_fire(char *p)
{
    static const float dirs[5][2] = {{0, 1}, {0.7071f, 0.7071f}, {1, 0}, {0.7071f, -0.7071f}, {0, -1}};
    int right = FACING(p) & FACE_RIGHT;

    axl_sound(SND_COPY);
    copy_launch(*(float *)(p + 0x14) + (right ? tip[0] : -tip[0]), *(float *)(p + 0x1C) + tip[1],
                right ? dirs[aim][0] : -dirs[aim][0], dirs[aim][1]);
    if (test_on)
        test_log("copy fire aim %d", aim);
}

/* DNA by the copy shot (user decision 2026-10-28; PS1 axl_main.c copy_watch): a stage boss (stages 1-8) whose hit
 * points go to 0 while the copy shot is out or within DNA_LATE frames of its end (it is gone at its first hit; no
 * distance test - a big boss's hit part is far from its origin) gives its DNA (save bit
 * DNA_BIT(stage) in the parts word st + 0xAC = PS1 0x800CCF40, the bits the parts leave free: 0, 1, 26-31). Missed:
 * no DNA (the red panel's rematch, later). The boss weapon is owned only with both the boss beaten (st + 0x9C, which
 * is also the stage select's cleared mark - one byte for both in X6) and its DNA: OWNED(p) set every frame. */
#define SAVE_PARTS_AT 0xAC
#define DNA_BIT(k) ((k) <= 2 ? 1u << ((k) - 1) : 1u << ((k) + 23))
#define DNA_LATE 4
char *objs_boss(void);   /* axl_objs.c */

static char *game_st(void)
{
    char *g = *(char **)0x041D8C8C;
    return g ? g + 0x70 : 0;
}

static void dna_watch_copy(char *p)
{
    static char *boss;
    static int boss_hp, copy_seen;
    static unsigned copy_gone;
    char *st = game_st(), *b = objs_boss();
    int hp, stage;

    if (copy_out()) {
        copy_seen = 1;
    } else if (copy_seen) {
        copy_seen = 0;
        copy_gone = axl_frames;
    }
    if (!st || !b) {
        boss = 0;
        return;
    }
    hp = *(volatile unsigned char *)(b + 0x84);   /* (the hit points; + 0x85 is the bar's fill, 2026-10-28 log) */
    stage = (unsigned char)st[0x48];
    if (b == boss && boss_hp > 0 && hp == 0 && stage >= 1 && stage <= 8) {
        int copy = copy_out() || axl_frames - copy_gone <= DNA_LATE;
        if (copy)
            *(volatile unsigned *)(st + SAVE_PARTS_AT) |= DNA_BIT(stage);
        if (test_on)
            test_log("boss %p beaten in stage %d: %s -> parts %08x", b, stage, copy ? "copy shot, DNA got" : "no copy shot",
                     *(volatile unsigned *)(st + SAVE_PARTS_AT));
    }
    boss = b;
    boss_hp = hp;
}

/* the owned boss weapons: beaten and with the DNA (all_weapons: every one, nothing saved - the save byte is the
 * stage select's cleared mark too, user report 2026-10-28) */
static void dna_owned(char *p)
{
    char *st = game_st();
    unsigned parts, m = 0, k;

    if (st && axl_all_parts)   /* (testing, axl_options.txt: every part owned - the parts' bits 2-25 only) */
        *(volatile unsigned *)(st + SAVE_PARTS_AT) |= 0x03FFFFFCu;
    if (axl_all_weapons) {
        OWNED(p) = 0xFF;
        return;
    }
    if (!st)
        return;
    parts = *(volatile unsigned *)(st + SAVE_PARTS_AT);
    for (k = 1; k <= 8; k++)
        if (parts & DNA_BIT(k))
            m |= 1u << (k - 1);
    OWNED(p) = (unsigned char)(st[SAVE_OWNED_AT] & m);
    if (WEAPON(p) >= 1 && WEAPON(p) <= 8 && !(OWNED(p) >> (WEAPON(p) - 1) & 1) && !dna_boss)
        WEAPON(p) = 0;   /* (a weapon without its DNA: the plain gun) */
}

/* every frame (axl_frame): the charge's sparks / glow, the trail behind the flying ring */
static void copy_update(char *p)
{
    proj *q;
    float x, y, dx, dy;
    int o;

    if (copy_out())
        copy_fx_t = COPY_TRAIL_LIFE + 2;
    else if (copy_fx_t)
        copy_fx_t--;
    if (!copy_out() || !(q = proj_of(copy_w)) || !q->live || q->t % COPY_TRAIL_EVERY)
        return;   /* (not set up yet: its place is still 0, 0) */
    x = *(volatile float *)(copy_w + 0x14);
    y = *(volatile float *)(copy_w + 0x1C);
    dx = x - copy_px;
    dy = y - copy_py;
    if (dx * dx + dy * dy < 4)
        return;
    o = fabsf(dy) < fabsf(dx) * 0.4f ? 0 : fabsf(dx) < fabsf(dy) * 0.4f ? 2 : (dx > 0) == (dy > 0) ? 1 : 3;
    fx_spawn((x + copy_px) / 2, (y + copy_py) / 2, COPY_TRAIL_LIFE, trail_fn, o, COPY_TRAIL - COPY_BASE + COPY_FADE * o);
    copy_px = x;
    copy_py = y;
}

typedef void(__fastcall *clut_up_t)(void *owner, void *edx, int slot, const void *src, int n);
#define CLUT_UP ((clut_up_t)0x023AAEF0)
#define CLUT_OWNER_P (*(void **)0x041D8C44)
#define VX(p) O32(p, 0x3C)

/* Hover jets (PS1 axl_main.c draw_flame; user request 2026-10-27: none on PC): while Axl hovers, one flame under each
 * boot, the sheet's 4 flames in turn (lc2_dna.flame_pictures, anims FLAME_ANIM..), each object's origin on the boot's
 * nozzle of the body frame drawn (flame_spot: data coordinates facing left; the PS1 build's spots). Their palette
 * goes to a free CLUT slot while they are out. Body frames without spots (not a hover / air aim picture): none. */
static char *jet[2];
static unsigned short jet_orig[16];

static int jet_spot(char *p)
{
    unsigned i;
    for (i = 0; i < N_FLAME_FRAMES; i++)
        if (flame_frames[i] == O8(p, 0x66))
            return (int)i;
    return -1;
}

static void jet_fn(char *w, proj *q)
{
    char *p = axl_p;
    int s, right, i;

    if (!p || !hovering || (s = jet_spot(p)) < 0 || dna_boss) {
        *(volatile float *)(w + 0x1C) = -10000.0f;   /* (parked off screen until hovering again) */
        q->life = q->t + 2;
        return;
    }
    right = FACING(p) & FACE_RIGHT;
    *(volatile float *)(w + 0x14) = *(float *)(p + 0x14) + (right ? -flame_spot[s][q->a][0] : flame_spot[s][q->a][0]);
    *(volatile float *)(w + 0x1C) = *(float *)(p + 0x1C) + flame_spot[s][q->a][1];
    VX(w) = VY(w) = 0;
    i = (q->t / 2 + q->a) % N_FLAMES;
    if (q->anim != FLAME_ANIM + i)
        proj_boss_look(w, FLAME_ANIM + i);
    O16(w, 0x5E) = (unsigned short)(jet_slot >= 0 ? jet_slot : 0);
    O8(w, 0x29) = O8(p, 0x29);
    O8(w, 0x2A) = 3;   /* behind Axl */
    q->life = q->t + 2;
}

static void hover_jets(char *p)
{
    unsigned short *mem = CLUT_OWNER_P ? (unsigned short *)((char *)CLUT_OWNER_P + 0x1002C) : 0;
    int on = hovering && !dna_boss && jet_spot(p) >= 0, i;

    if ((on || copy_fx_t) && mem) {   /* (the copy shot's colours are in flame_pal too) */
        if (jet_slot < 0) {
            jet_slot = clut_free_run(1, dna_pal_slot, dna_pal_slot + 4);
            if (jet_slot < 0)
                jet_slot = 405;
            memcpy(jet_orig, mem + 16 * jet_slot, sizeof jet_orig);
        }
        if (!dna_flashing())
            CLUT_UP(CLUT_OWNER_P, 0, jet_slot, flame_pal, 1);
    } else if (jet_slot >= 0 && mem) {
        CLUT_UP(CLUT_OWNER_P, 0, jet_slot, jet_orig, 1);
        jet_slot = -1;
    }
    if (on && mem) {
        for (i = 0; i < 2; i++)
            if (!jet[i] || !proj_of(jet[i])) {
                jet[i] = proj_spawn(0, *(float *)(p + 0x14), *(float *)(p + 0x1C), 0, 0, 1, 0, jet_fn,
                                    P_PIERCE | P_TOUGH | P_WALL | P_NOSCREEN);
                if (jet[i]) {
                    proj_nobox(jet[i]);
                    proj_of(jet[i])->a = i;
                }
            }
    } else {
        for (i = 0; i < 2; i++)
            if (jet[i] && proj_of(jet[i]))
                proj_kill(jet[i]);
        jet[0] = jet[1] = 0;
    }
}

/* Giga Crush (user request 2026-10-27): the giga gauge fills as X takes hits; when it is full the giga button (0x40;
 * X's own Nova Strike is masked) plays Axl's giga pose (anim 39: crouch, then the sheet's giga poses, held arms out),
 * a gun spins in each hand (the sheet's giga gun turned 8 ways, faster and faster), then the screen flashes and every
 * enemy within the screen around Axl gets the sheet's burst and a hit (damage id GIGA_ID). The pictures and their
 * palette are lc2_dna.giga_pictures (FX texture, frames GIGA_BASE..); the palette goes to a free CLUT slot meanwhile.
 * Not while changed (the button is the DNA button then). */
#define ANIM_GIGA 245         /* lc2_hook.PC_SCRIPTS: the giga poses (X's anim 39 is the victory again) */
#define GIGA_LEN 96           /* frames from the press to the end */
#define GIGA_SPIN_AT 24       /* the pose is out (frame 186): the guns appear */
#define GIGA_HIT_AT 70        /* the flash, the bursts and the hits */
#define GIGA_ID 0x16          /* X's Nova Strike id: 8 on Nightmare Zero's table (most enemy tables take it) */
#define GIGA_RX 180           /* the screen around Axl (LC2's X6 view is 256 x 224 game pixels; a bit more) */
#define GIGA_RY 130
#define GIGA_MAX 24
#define LAYER_FRONT 0
static const signed char giga_hand[2][2] = {{-30, -12}, {26, -10}};   /* facing left (data): front / back hand */
static const signed char box_giga[4] = {-28, -28, 56, 56};
static int giga_t, giga_slot = -1;
static int roll_t, roll_dir;   /* the dodge roll (roll_before / roll_after) */
static unsigned short giga_orig[16];
static char *giga_gun[2];

static void giga_look(char *w, int i)   /* Giga picture i (its own held anim, else the shot's script moves on) */
{
    proj *q = proj_of(w);

    if (q && q->anim != GIGA_ANIM + i)
        proj_boss_look(w, GIGA_ANIM + i);
    O16(w, 0x5E) = (unsigned short)(giga_slot >= 0 ? giga_slot : 0);
    O8(w, 0x2A) = LAYER_FRONT;
}

static void giga_gun_fn(char *w, proj *q)
{
    char *p = axl_p;
    int right, t, a;

    if (!p || !giga_t) {
        proj_kill(w);
        return;
    }
    right = FACING(p) & FACE_RIGHT;
    *(volatile float *)(w + 0x14) = *(float *)(p + 0x14) + (right ? -giga_hand[q->a][0] : giga_hand[q->a][0]);
    *(volatile float *)(w + 0x1C) = *(float *)(p + 0x1C) + giga_hand[q->a][1];
    VX(w) = VY(w) = 0;
    t = q->t;
    q->b += 2 + t / 8;                            /* faster and faster: 1/4 .. about 3/4 turn per 8 frames */
    a = (q->b >> 3) & (GIGA_SPIN - 1);
    if (q->a)
        a = (GIGA_SPIN - a) & (GIGA_SPIN - 1);   /* the other hand turns the other way */
    giga_look(w, a);
    O8(w, 0x29) = O8(p, 0x29);
    q->life = q->t + 2;
}

static void giga_burst_fn(char *w, proj *q)
{
    int i = q->t / 3;

    VX(w) = VY(w) = 0;
    if (i >= GIGA_BURST) {
        proj_kill(w);
        return;
    }
    giga_look(w, GIGA_SPIN + i);
}

static void giga_palette(int on)
{
    unsigned short *mem = CLUT_OWNER_P ? (unsigned short *)((char *)CLUT_OWNER_P + 0x1002C) : 0;

    if (!mem)
        return;
    if (on && giga_slot < 0) {
        giga_slot = clut_free_run(1, dna_pal_slot, dna_pal_slot + 4);
        if (giga_slot < 0)
            giga_slot = 404;
        memcpy(giga_orig, mem + 16 * giga_slot, sizeof giga_orig);
        if (test_on)
            test_log("giga palette at slot %d", giga_slot);
    }
    if (on)
        CLUT_UP(CLUT_OWNER_P, 0, giga_slot, giga_pal, 1);
    else if (giga_slot >= 0) {
        CLUT_UP(CLUT_OWNER_P, 0, giga_slot, giga_orig, 1);
        giga_slot = -1;
    }
}

static void giga_hit(char *p)
{
    char *tg[GIGA_MAX], *w;
    int n, i;

    dna_screen_flash();
    n = objs_targets(*(float *)(p + 0x14), *(float *)(p + 0x1C), GIGA_RX, GIGA_RY, tg, GIGA_MAX);
    for (i = 0; i < n; i++) {
        float x = *(volatile float *)(tg[i] + 0x14), y = *(volatile float *)(tg[i] + 0x1C);
        w = proj_spawn(GIGA_ID, x, y, 0, 0, 1, 4, 0, P_PIERCE | P_WALL | P_NOSCREEN | P_HIDDEN);
        if (w)
            proj_box(w, box_giga);
        w = proj_spawn(0, x, y, 0, 0, 1, 3 * GIGA_BURST + 1, giga_burst_fn, P_PIERCE | P_WALL | P_NOSCREEN);
        if (w)
            proj_nobox(w);
    }
    if (test_on)
        test_log("giga: %d targets", n);
}

/* the giga pose is bank A (anim ANIM_GIGA is past X's bank table: set_anim keeps the bank of the anim before; after
 * the victory, bank B, Axl was invisible): X's own anim call to the idle anim picks bank A first */

/* Dodge roll (user request 2026-10-28, X7's Axl; the PS1 one, dash + down, was taken out 2026-10-24): R on the
 * ground when not changed - ROLL_LEN frames at ROLL_SPEED the way held (else faced), no hurt box, no input meanwhile;
 * up slopes, stopped by walls (axl_moves.c moves_step), over when he leaves the ground */
#define ANIM_ROLL 251          /* lc2_hook.ROLL_ANIM */
#define ROLL_LEN AXL_ROLL_LEN   /* the whole anim (the viewer's edit: the get-up after the tumble, 2026-10-29) */
#define ROLL_MOVE 20           /* frames it moves, invincible: the tumble */
#define ROLL_SPEED 4.5f        /* (90 px: user request 2026-10-29, 60 was short) */

static void roll_before(char *p, int r_new)
{
    if (!roll_t && r_new && !dna_boss && !giga_t && !copy_hold && on_ground(STATE(p)) && !LOCK(p)) {
        roll_t = ROLL_LEN;
        roll_dir = (HELD(p) & B_RIGHT) ? 1 : (HELD(p) & B_LEFT) ? -1 : (FACING(p) & FACE_RIGHT) ? 1 : -1;
        if (test_on)
            test_log("roll start dir %d", roll_dir);
    }
    if (roll_t) {
        HELD(p) = 0;
        NEWP(p) = 0;
    }
}

static void roll_after(char *p)
{
    if (!roll_t)
        return;
    if (!on_ground(STATE(p)) || INIT(p) != 1) {   /* (off a ledge: X's fall goes on) */
        roll_t = 0;
        return;
    }
    if (roll_dir > 0)
        FACING(p) |= FACE_RIGHT;
    else
        FACING(p) &= ~FACE_RIGHT;
    if (ROLL_LEN - roll_t < ROLL_MOVE)
        moves_step(p, roll_dir * ROLL_SPEED);
    if (--roll_t == 0) {
        set_anim(p, ANIM_IDLE);
        if (test_on)
            test_log("roll end x %.0f", *(volatile float *)(p + 0x14));
    } else if (ANIM(p) != ANIM_ROLL) {
        X_ANIM_BANK(p, ANIM_IDLE);   /* (bank A: the roll's frames) */
        set_anim(p, ANIM_ROLL);
    }
}

static void giga_anim(char *p)
{
    X_ANIM_BANK(p, ANIM_IDLE);
    set_anim(p, ANIM_GIGA);
}

static void giga_before(char *p)
{
    if (!giga_t && !dna_boss && (NEWP(p) & B_GIGA) && GIGA_GAUGE(p) >= ENERGY_MAX(p) && on_ground(STATE(p)) &&
        !LOCK(p)) {
        GIGA_GAUGE(p) = 0;
        giga_t = GIGA_LEN;
        giga_anim(p);
        if (test_on)
            test_log("giga start");
    }
    HELD(p) &= ~B_GIGA;
    NEWP(p) &= ~B_GIGA;
    if (giga_t) {
        HELD(p) = 0;
        NEWP(p) = 0;
    }
}

static void giga_after(char *self, char *p)
{
    int i, t;

    if (!giga_t)
        return;
    t = GIGA_LEN - giga_t;
    if (ANIM(p) != ANIM_GIGA)
        giga_anim(p);
    if (t == GIGA_SPIN_AT) {
        giga_palette(1);
        for (i = 0; i < 2; i++) {
            giga_gun[i] = proj_spawn(0, *(float *)(p + 0x14), *(float *)(p + 0x1C), 0, 0, 1, 0, giga_gun_fn,
                                     P_PIERCE | P_TOUGH | P_WALL | P_NOSCREEN);
            if (giga_gun[i]) {
                proj_nobox(giga_gun[i]);
                proj_of(giga_gun[i])->a = i;
            }
        }
    }
    if (t > GIGA_SPIN_AT && giga_slot >= 0 && !dna_flashing())
        giga_palette(1);   /* (kept up: the screen flash puts its saved CLUTs back) */
    if (t == GIGA_HIT_AT)
        giga_hit(p);
    if (--giga_t == 0) {
        for (i = 0; i < 2; i++)
            if (giga_gun[i] && proj_of(giga_gun[i]))
                proj_kill(giga_gun[i]);
        giga_gun[0] = giga_gun[1] = 0;
        set_anim(p, ANIM_IDLE);
    }
    if (giga_t == 0 || t == GIGA_LEN - 1)
        giga_palette(0);
}

/* Free flight for the flying bosses' DNA (Yammark, Heatnix, Mijinion; user request 2026-10-26), as PS1 High Max
 * (mods/highmax/src/hm_flight.c) after X's pre-state code: no jump / dash states; on the ground up lifts Axl 4 px
 * into the fall state; in the air gravity 0 and the vertical speed from up / down (2 px/f, 4 with dash held), the
 * horizontal one stays X's air control; X's default gravity is 0 meanwhile so walking off a ledge keeps him up. */
#define START_FALL ((x_method_t)0x0253DB60)   /* PS1 FUN_8003aff4: fall anim, speeds 0, gravity = default, state 7 */
#define FLY_SPEED 0x20000
#define FLY_FAST 0x40000
#define C_FLOOR 0x8
static s32 x_gravity_saved;

int dna_flies(void)
{
    return dna_boss == 1 || dna_boss == 3 || dna_boss == 7 || dna_boss == 8;   /* (Sheldon floats too: user report
                                                   * 2026-10-28) */
}

/* before X's state code while changed (user request 2026-10-27, every boss): no wall cling (flying, it was ended
 * every frame: the "rattle" against a wall), no ladder / rope (vertical rope = state 27, hanging rope 28: up + the
 * head's terrain type, PS1 FUN_80039984). X gets no up (no boss move uses it; flight reads it from fly_held), no
 * jump / dash when flying, and no left / right into a wall: the wall bits 1 / 2 of the terrain flags (P+0xB0, PS1
 * +0x70) on and the held direction not moving him. Whatever still gets in ends at once (changed_states). */
static u16 fly_held, fly_blocked;
static int fly_fresh;
static float fly_last_x;

/* a wall within 2 px of Axl going dir: wall bits there that the spot 8 px further back does not have (the feet in
 * the floor set wall bits too) */
static int wall_near(char *p, int dir)
{
    float x = *(volatile float *)(p + 0x14);
    int now, back;

    float y = *(volatile float *)(p + 0x1C);
    *(volatile float *)(p + 0x1C) = y - 6;   /* (6 px up: the floor's bits out of both, as axl_moves.c wall_ahead) */
    *(volatile float *)(p + 0x14) = x + dir * 2;
    now = proj_terrain(p) & 3;
    *(volatile float *)(p + 0x14) = x - dir * 6;
    back = proj_terrain(p) & 3;
    *(volatile float *)(p + 0x14) = x;
    *(volatile float *)(p + 0x1C) = y;
    return (now & ~back) != 0;
}

static void changed_input(char *p)
{
    u16 held = HELD(p), lr = held & (B_LEFT | B_RIGHT);
    float x = *(volatile float *)(p + 0x14);
    int air = in_air(STATE(p));   /* (all of the wall handling only in the air: X clings only there, and on the floor
                                   * slopes set wall bits / stop him a frame - user report 2026-10-27, Wolfang stuck
                                   * walking up a slope) */

    fly_held = held;
    if (!air || !((O8(p, 0xB0) | O8(p, 0xB1) | O8(p, 0xF0)) & C_WALLS) || !(lr & fly_blocked))
        fly_blocked = 0;
    if (air && !fly_blocked && lr && (lr & (B_LEFT | B_RIGHT)) != (B_LEFT | B_RIGHT) && x == fly_last_x &&
        ((O8(p, 0xB0) | O8(p, 0xB1) | O8(p, 0xF0)) & C_WALLS))
        fly_blocked = lr;
    fly_last_x = x;
    if (air && (lr == B_RIGHT || lr == B_LEFT) && wall_near(p, lr == B_RIGHT ? 1 : -1)) {
        /* in the air, a wall just ahead: X never gets the push (it clung for a frame, the change ended it, the
         * cling's sound every frame - user report 2026-10-27, the flyers rattled against walls); he still turns that
         * way. (On the floor X does not cling: no test there - slopes / steps must not read as walls) */
        if (lr == B_RIGHT)
            FACING(p) |= FACE_RIGHT;
        else
            FACING(p) &= ~FACE_RIGHT;
        fly_blocked = lr;
    }
    fly_fresh = 1;
    /* every wall a no-cling wall while changed (user request 2026-10-27: it still rattled flying right into a wall):
     * X decides the cling from the last terrain result, its wall bits are taken away before X's state code (the
     * terrain test still stops him at the wall when he moves) */
    if (air) {
        O8(p, 0xB0) &= ~C_WALLS;
        O8(p, 0xB1) &= ~C_WALLS;
        O8(p, 0xF0) &= ~C_WALLS;
    }
    lr = B_UP | fly_blocked | (dna_flies() ? B_JUMP | B_DASH : 0);
    HELD(p) = held & ~lr;
    NEWP(p) &= ~lr;
}

static void changed_states(char *self, char *p)
{
    int st = STATE(p);

    if (dna_boss && !dna_pose && ((st >= 9 && st <= 11) || on_rope(st))) {
        START_FALL(self);
        if (test_on)
            test_log("changed: state %d ended (fall)", st);
    }
}

int axl_fly_fast;

static void flight(char *self, char *p)
{
    u16 held = fly_fresh ? fly_held : HELD(p);
    int st = STATE(p);
    s32 sp;

    if (X_GRAVITY(p))
        x_gravity_saved = X_GRAVITY(p);
    axl_fly_fast = 0;
    if (!dna_flies() || dna_pose) {
        if (!X_GRAVITY(p) && x_gravity_saved) {   /* the change ended: X falls again */
            X_GRAVITY(p) = x_gravity_saved;
            if (in_air(st) && !GRAVITY(p))        /* (ended in the air: the flight's 0 stayed, he floated; 2026-10-27) */
                GRAVITY(p) = x_gravity_saved;
        }
        return;
    }
    HELD(p) = held & ~B_JUMP;
    NEWP(p) &= ~(B_JUMP | B_DASH);
    HELD(p) &= ~B_DASH;
    sp = (held & B_DASH) && dna_boss == 1 ? FLY_FAST : FLY_SPEED;   /* (Heatnix / Mijinion: the dash is a move) */
    axl_fly_fast = sp == FLY_FAST;   /* (the HUD's dash button on, axl_hud.c) */
    if (LOCK(p)) {                                /* a cutscene: fall to the floor like X */
        X_GRAVITY(p) = x_gravity_saved;
        if (in_air(st) && !GRAVITY(p))
            GRAVITY(p) = x_gravity_saved;
        return;
    }
    if (on_ground(st) && !(held & B_UP)) {       /* on the ground he walks as X, gravity and all: down a slope
                                                   * he follows it (user decision 2026-10-28; with no gravity he
                                                   * walked on at the height he had, off the slope) */
        if (!X_GRAVITY(p) && x_gravity_saved)
            X_GRAVITY(p) = x_gravity_saved;
        return;
    }
    X_GRAVITY(p) = 0;
    if (st >= 9 && st <= 11) {                    /* wall slide / kick */
        START_FALL(self);
        st = 7;
    }
    if (on_ground(st)) {
        START_FALL(self);
        POS_Y(p) -= 4;
        O8(p, 0xB0) &= ~C_FLOOR;
        O8(p, 0xB1) &= ~C_FLOOR;
        O8(p, 0xF0) &= ~C_FLOOR;
        st = 7;
    }
    if (in_air(st)) {
        GRAVITY(p) = 0;
        VY(p) = (held & B_UP) ? sp : (held & B_DOWN) ? -sp : 0;
    }
}

/* PS1 axl_frame: normal X (armour not 1-3) plays with Axl's table, the armoured X with X's */
void axl_frame(char *self, x_fn orig)
{
    static int ready;
    char *p = P_OF(self);
    int on = is_normal_x(self);

    if (!ready) {
        axl_table_init();
        ready = 1;
    }
    dna_unlook();
    dna_flash_tick();
    warn_tick();
    frame_count++;
    axl_frames++;
    special_new = (NEWP(p) & B_SPECIAL) != 0;
    charge_unhide();   /* (a frame whose post did not run) */
    if (on && INIT(p) == 1)
        charge_hide(p);   /* (X's auto charge / full-charge part off for X's update: the copy shot's, copy_auto) */
    else
        opt_auto = part_full = part_quick = 0;
    shot_new = (NEWP(p) & B_SHOT) != 0;
    /* R: the changed boss's second attack (user request 2026-10-28: L and R both opening the ring was a waste; the
     * giga button stays the Giga Attack - Axl's Giga Crush, the bosses' own later), never X's weapon change */
    alt_new = (NEWP(p) & B_R) != 0;
    if (dna_boss) {   /* changed (user request 2026-10-29): R ends the change, the special button is the DNA button */
        int r = alt_new;
        alt_new = special_new;
        special_new = r;
    }
    HELD(p) &= ~B_R;
    NEWP(p) &= ~B_R;
    proj_frame();
    objs_hp_frame();
    log_fx = (shooting || copy_hold) && test_on;
    if (test_on)
        fx_log();
    player_p = on ? p : 0;
    player_t = on ? self : 0;
    axl_p = player_p;
    axl_self = player_t;
    bullets(p);
    if (on)
        homing(p);
    if (on && P_TABLE(p) == X_TABLE)
        P_TABLE(p) = axl_table;
    else if (!on && P_TABLE(p) == axl_table)
        P_TABLE(p) = X_TABLE;
    shot_limit(on);
    hand_table(on);
    was_shooting = shooting;
    if (on && INIT(p) == 1) {
        dna_watch_copy(p);
        dna_owned(p);
    }
    if (on && INIT(p) == 1 && (menu_change_to & MENU_PICK)) {   /* the menu picked a weapon only (jump): selected,
        * no change (another one than the change's own ends it, dna_before) */
        WEAPON(p) = (u8)(menu_change_to & 0xFF);
        menu_change_to = 0;
    } else if (on && INIT(p) == 1 && menu_change_to == MENU_AXL) {   /* the menu chose Axl: his own gun (ends a change) */
        WEAPON(p) = 0;
        menu_change_to = 0;
    } else if (on && INIT(p) == 1 && menu_change_to) {   /* the DNA menu chose a boss: its weapon, then the change */
        if (dna_boss && dna_boss != menu_change_to)
            WEAPON(p) = (u8)menu_change_to;      /* (dna_before ends the current change on the weapon change) */
        else {
            WEAPON(p) = (u8)menu_change_to;
            special_new = !dna_boss;
            menu_change_to = 0;
        }
    }
    if (on && INIT(p) == 1 && menu_frame(self, p)) {
        shooting = hovering = adash_t = 0;
    } else if (on && INIT(p) == 1 && dna_before(p, special_new, giga_t || roll_t)) {   /* the change pose holds Axl */
        HELD(p) = 0;
        NEWP(p) = 0;
        shooting = hovering = adash_t = 0;
    } else if (on && INIT(p) == 1) {
        if (SPECIAL_SHOTS && WEAPON(p) == 0 && !dna_boss && !LOCK(p)) {   /* the copy shot: charge, let go */
            if (copy_auto(p, special_new))
                ;                 /* (auto charge / the full-charge part) */
            else if (HELD(p) & B_SPECIAL)
                copy_charge(p, 1);
            else if (copy_hold)
                copy_charge(p, 0);
        } else if (copy_hold)
            copy_charge(p, -1);
        int giga_new = (NEWP(p) & B_GIGA) != 0;   /* (before giga_before masks it: the changed boss's giga) */
        giga_before(p);           /* (masks the button: the Ultimate's Nova Strike never comes out) */
        roll_before(p, alt_new);  /* (R when not changed) */
        if (dna_boss) {           /* changed: the special button is the DNA button (alt_new), the giga button the boss's giga attack */
            int dash_new = (NEWP(p) & B_DASH) != 0, dash_held = (HELD(p) & B_DASH) != 0;
            HELD(p) &= ~B_GIGA;
            NEWP(p) &= ~B_GIGA;
            if (dna_boss != 1) {  /* the dash is the boss's move (Yammark: fast flight) */
                HELD(p) &= ~B_DASH;
                NEWP(p) &= ~B_DASH;
            }
#ifdef AXL_NO_SKILLS   /* (the test5 release: the giga button stays empty while changed, as in test4) */
            giga_new = 0;
#endif
            if (dna_giga_start(p, giga_new && !LOCK(p))) {   /* a giga attack holds him (axl_dna_bosses.c) */
                HELD(p) = 0;
                NEWP(p) = 0;
                dash_new = dash_held = alt_new = 0;
            }
            if (moves_before(p, dash_new, dash_held, alt_new)) {   /* a move holds Axl */
                HELD(p) = 0;
                NEWP(p) = 0;
            }
        }
        if (!giga_t)
            shoot_before(self, p);
        else
            shooting = 0;
        if (copy_fire_now) {
            copy_fire_now = 0;
            if (shooting)
                copy_fire(p);
        }
        if (!dna_flies())
            hover_before(p);
        if (dna_boss && !dna_pose && !LOCK(p))
            changed_input(p);
    } else {
        shooting = hovering = adash_t = giga_t = roll_t = 0;
        fly_blocked = 0;
        dna_reset();
        if (on) {   /* (Axl not in his normal update - an item's refill, a cutscene: no X charge from a held shot,
                     * user report 2026-10-28) */
            static int last;
            HELD(p) &= ~B_SHOT;
            NEWP(p) &= ~B_SHOT;
            if (test_on && last != (INIT(p) << 8 | STATE(p))) {
                last = INIT(p) << 8 | STATE(p);
                test_log("axl off normal: init %d state %d charge %d", INIT(p), STATE(p), CHARGE_STAGE(p));
            }
        }
    }
    /* X fires only while it counts fewer shots than its limit (B+0x95), and ours (copy shot pictures, hover flames,
     * Wolfang's spikes...) count too: taken off for X's code, put back after it (axl_post; user report 2026-10-27, the
     * rapid fire stalled). (Ours that die meanwhile take themselves off as X's do: the same total comes back) */
    shot_lent = on ? proj_live_count() : 0;
    if (shot_lent > SHOT_COUNT(p))
        shot_lent = SHOT_COUNT(p);
    SHOT_COUNT(p) -= (u8)shot_lent;
    orig(self);
    if (on && INIT(p) == 1) {
        changed_states(self, p);
        flight(self, p);
    }
    fly_fresh = 0;
}

/* Dash afterimages (user decision 2026-10-27: X's fading ghosts, grey instead of blue): the game draws them (pool 1
 * objects, 3 at a time, nearest first) with CLUT slots 13-15, flat colours it writes itself with the translucent bit
 * set (0xd103 / 0xbca0 / 0xb040, added to the screen: each one fainter; cdump 2026-10-26). Ours: grey of about the
 * same brightness, translucent too. Written after X's update (before the draw) while Axl leaves afterimages, the
 * game's colours put back afterwards. */
#define AFTERIMAGE_SLOT 13

static void afterimage_colours(char *p)
{
    static unsigned short pal[3][16], orig[48];
    static int left;   /* frames the afterimages may still be on screen */
    unsigned short *mem = CLUT_OWNER_P ? (unsigned short *)((char *)CLUT_OWNER_P + 0x1002C) : 0;
    static const unsigned short tone[3] = {0xAD4A, 0xA0E7, 0x9084};   /* (10, 10, 11), (7, 7, 8), (4, 4, 4) + STP */
    int i, j;

    if (!mem)
        return;
    if (AFTERIMAGE(p) && !left)
        memcpy(orig, mem + 16 * AFTERIMAGE_SLOT, sizeof orig);   /* the game's colours, put back afterwards */
    left = AFTERIMAGE(p) ? 30 : left > 0 ? left - 1 : 0;
    if (!left) {
        if (orig[1])
            CLUT_UP(CLUT_OWNER_P, 0, AFTERIMAGE_SLOT, orig, 3);
        orig[1] = 0;
        return;
    }
    if (!pal[0][1])
        for (i = 0; i < 3; i++)
            for (j = 1; j < 16; j++)
                pal[i][j] = tone[i];
    CLUT_UP(CLUT_OWNER_P, 0, AFTERIMAGE_SLOT, pal, 3);
}

/* White Axl (armor 4, PS1 axl_main.c white_axl): the body palettes of the player's palette blob (what FUN_023ac680 -
 * PS1 FUN_8003c4ec - uploads from: *(*(CLUT owner + 0x281C4) + 0x6C), PS1 layout, weapon w at w * 0x80 bytes) become
 * white_pal, then X's palettes are uploaded again. The blob comes back with every stage load: checked each frame
 * (user report 2026-10-27: the cheat's White Axl played white but was drawn in Axl's colours) */
typedef void(__fastcall *x_pal_t)(void *game);
#define X_PAL_UPLOAD ((x_pal_t)0x023AC680)

static void white_axl(void)
{
    char *res = CLUT_OWNER_P ? *(char **)((char *)CLUT_OWNER_P + 0x281C4) : 0;
    unsigned short *blob = res ? *(unsigned short **)(res + 0x6C) : 0;
    unsigned i;

    if (armor() != 4 || !blob || !*(void **)0x041D8C8C || !memcmp(blob + 16 * body_pals[0], white_pal, sizeof white_pal))
        return;
    if (memcmp(blob + 16 * body_pals[0], axl_body_pal, sizeof axl_body_pal)) {   /* not Axl's blob: left alone */
        static void *warned;
        if (test_on && warned != blob)
            test_log("white axl: blob %p is not Axl's palettes", (void *)blob);
        warned = blob;
        return;
    }
    for (i = 0; i < N_BODY_PALS; i++)
        memcpy(blob + 16 * body_pals[i], white_pal, sizeof white_pal);
    X_PAL_UPLOAD(*(void **)0x041D8C8C);
    if (test_on)
        test_log("white axl: palettes %u", (unsigned)N_BODY_PALS);
}

void axl_post(char *self)
{
    char *p = P_OF(self);

    charge_unhide();   /* (the option and the part back for everyone else) */
    SHOT_COUNT(p) += (u8)shot_lent;
    shot_lent = 0;

    if (!is_normal_x(self) || INIT(p) != 1)
        return;
    {   /* changed: no rope (user report 2026-10-29: the flying bosses got caught on ropes) - X's state code took one
         * (states 27 / 28, snapped onto it): put back where he was and falling (flight takes over), drawn so */
        static float rx, ry;
        static int r_ok;
        if (dna_boss && !dna_pose && on_rope(STATE(p))) {
            if (r_ok) {   /* (and his own step this frame, from the keys: X grabs again every frame he overlaps
                           * it - put back only, he stood still there, unable to fly on: user report 2026-10-29) */
                u16 h = HELD(p);
                float hx = (h & B_RIGHT) ? 1.5f : (h & B_LEFT) ? -1.5f : 0;
                float vy = !dna_flies() ? 2.0f : (h & B_UP) ? -2.0f : (h & B_DOWN) ? 2.0f : 0;
                rx += hx;
                ry += vy;
                *(volatile float *)(p + 0x14) = rx;
                POS_Y(p) = ry;
                if (hx)
                    FACING(p) = (u8)((FACING(p) & ~FACE_RIGHT) | (hx > 0 ? FACE_RIGHT : 0));
            }
            START_FALL(self);
            if (test_on)
                test_log("changed: rope let go at %.0f %.0f", rx, ry);
        } else {
            rx = *(volatile float *)(p + 0x14);
            ry = POS_Y(p);
            r_ok = 1;
        }
    }
    giga_after(self, p);
    moves_after(p);
    roll_after(p);
    if (copy_hold && WEAPON(p) == 0 && !dna_boss) {   /* the charge's stage after X's update: the glow for the draw
                                                      * only (cleared again before the next update: copy_charge) */
        CHARGE_STAGE(p) = copy_hold >= COPY_STAGE2 ? 2 : copy_hold >= COPY_STAGE1 ? 1 : 0;
        CHARGE_ID(p) = copy_hold >= COPY_STAGE2 ? COPY_FULL_ID : copy_hold >= COPY_STAGE1 ? 9 : 0;
    }
    if (armor() == 4 && STATE(p) == ST_ULT_HOVER) {   /* the Ultimate's own hover got in (a jump press after Axl's
        * hover was used, user report 2026-10-27; PS1 notes: state 33): a fall instead */
        START_FALL(self);
        hovering = 0;
        if (test_on)
            test_log("ultimate hover state: fall");
    }
    if (!giga_t && !dna_pose) {
        shoot_after(p, was_shooting);
        if (!dna_flies())
            hover_after(p);
    }
    dna_after(p, shooting, shooting && shot_new, alt_new);
    hud_frame(p);   /* (the skill HUD's buttons, axl_hud.c) */
    afterimage_colours(p);
    copy_update(p);
    hover_jets(p);
    hover_sound();
    white_axl();
}

/* PS1 axl_box: the hurt box (P+0x7C = box object 0x03480AA0 + 8 * n; PS1 +0x54 = 0x801F3F4C + 4 * n) of frames the
 * game has none for: Axl's added frames 228+ borrow an X frame's box (box_from, axl_gen.h); rope aims use the hang
 * frame's box (bank B index 7) */
#define BOX_PTR(p) (*(void *volatile *)((p) + 0x7C))
#define X_BOX_INDEX ((const u8 *)0x031DA6B8)     /* bank A frame -> box number (PS1 0x801F3B4C) */
#define ROPE_BOX_INDEX ((const u8 *)0x031DA7B8)  /* bank B (PS1 0x801F3C4C) */
#define X_BOXES ((char *)0x03480AA0)
#define FRAME(p) O8(p, 0x66)

void axl_box(char *self)
{
    char *p = P_OF(self);
    int f = FRAME(p), b;

    if (!is_normal_x(self) || INIT(p) != 1)
        return;
    if (ANIM(p) == 204 && !BOX_PTR(p) && (b = X_BOX_INDEX[0]) != 0)   /* change pose frames: none -> frame 0's */
        BOX_PTR(p) = X_BOXES + 8 * b;
    else if (ANIM(p) >= ROPE_AIM_ANIM && ANIM(p) < ROPE_AIM_ANIM + 8) {   /* rope aims: the hang frame's */
        if ((b = ROPE_BOX_INDEX[7]) != 0)
            BOX_PTR(p) = X_BOXES + 8 * b;
    } else if ((f == 244 || f == 245) && !BOX_PTR(p) && (b = X_BOX_INDEX[62]) != 0)   /* wall aims: the wall shot's */
        BOX_PTR(p) = X_BOXES + 8 * b;
    else if (f >= 228 && !BOX_PTR(p) && (b = X_BOX_INDEX[box_from[f - 228]]) != 0)
        BOX_PTR(p) = X_BOXES + 8 * b;
    if (moves_invincible() || (roll_t && ROLL_LEN - roll_t < ROLL_MOVE))
        BOX_PTR(p) = 0;      /* an invincible move (Shark's fin, Turtloid's shell): no hurt box */
    else if (dna_hurt_box() && BOX_PTR(p))
        BOX_PTR(p) = dna_hurt_box();   /* changed: the boss's size */
    look_p = p;              /* last: the boss's picture for the draw - put on after every unit has moved (axl_menu.c
                              * move_hook: axl_look_late), taken back before they move again: between X main and the
                              * end of the move pass the game's terrain work read the boss's frame / parts and a
                              * changed walker dropped through a slope onto the floor under it (2026-10-28 e2e) */
    look_fire = shooting;
}

/* axl_menu.c move_hook, after the unit move pass: the look of this frame (axl_box asked for it) */
void axl_look_late(void)
{
    if (look_p) {
        dna_look(look_p, look_fire);
        look_p = 0;
    } else
        dna_relook(axl_p);   /* (X main paused: the menu, the pause - the look kept on) */
}
