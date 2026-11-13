/* The game-derived tables the DLL needs, loaded at start-up from axl_data.bin next to dinput8.dll (axl_data.c).
 *
 * The build makes the file from the user's own game data (lc2_build.py / lc2_hook.py write the generated headers to
 * mods/axl/work/lc2/gen/, tools/lc2_data.py packs them with lc2/data/axl_data_pack.c); the DLL itself is built from
 * this repository alone. Here only the layout and the mod's own constants (CLUT slots, HUD layout, counts that size
 * arrays): the generated headers define the same constants, and the packer, built with -Werror, stops when one
 * differs. No pointers, longs or floats in the layout: it is the same for the packer (the build machine) and the DLL
 * (x86 Windows), AXL_DATA_SIZE checks it in both.
 *
 * File: axl_data_t as it is in memory (little-endian). hdr.hash = FNV-1a 32 of the bytes after the header. A missing
 * file or one whose magic, version, size or hash differs: the DLL installs no hook (axl_hook.c install).
 */
#ifndef AXL_DATA_H
#define AXL_DATA_H

#define AXL_DATA_FILE "axl_data.bin"
#define AXL_DATA_MAGIC 0x444C5841u   /* "AXLD" */
#define AXL_DATA_VERSION 10          /* + 1 at every layout change */
#define AXL_DATA_SIZE 45848          /* sizeof(axl_data_t): the packer and the DLL agree on the layout */

/* ---- the mod's own constants (the generated headers repeat them: a mismatch stops the packer) ---- */
#define DNA_SEQ_MAX 96
#define DNA_HOLD 255   /* dna_seq.at of move / walk / jump / fall / land: the last entry held, else the loop start */
#define DNA_PAL_SLOT 400
#define MENU_PAL_SLOT 410
#define SHOT_DIRS 32
#define FLASH_DIRS 8
#define FLASH_NF 4
#define GIGA_SPIN 8
#define MENU_LEVELS 4
#define MENU_PALS 5
#define HUD_STEPS 16
#define HUD_FRAMES 17
#define HUD_CELL 20
#define HUD_LEVELS 3
#define HUD_COOL 0
#define HUD_READY 1
#define HUD_FLASH 2

/* PC script entry = C++ object: vtable 0x031DE36C (RXC2.exe), then the PS1 bytes (dur, flag, step, frame) */
#define AXL_ENT_VT 0x031DE36Cu
typedef struct { unsigned vt; unsigned char dur, flag; signed char step; unsigned char frame; } axl_ent;
typedef struct { unsigned char n, at; unsigned char dur[DNA_SEQ_MAX], pose[DNA_SEQ_MAX]; } dna_seq;
typedef struct { int tex, pat, pal; dna_seq idle, fire, atk, move, alt, walk, jump, fall, land; dna_seq cling, ceil, cjump,
                 cshot, cdash, leap; dna_seq idle2, jump2, fall2; unsigned char lift[64]; signed char push[64];
                 dna_seq toss, giga; } dna_look_t;

/* array limits (the generated tables must fit: the packer checks) */
#define AXL_TABLE_MAX 256   /* anims are a byte */
#define AXL_ENTS_MAX 512
#define BOX_FROM_N 28       /* frames 228-255 */
#define BODY_PALS_MAX 32
#define SPIKE_SEQ_MAX 8
#define FLAME_FRAMES_MAX 16
#define N_BOSSES 9          /* 0 = Axl, 1-8 the bosses */

typedef struct {
    unsigned magic, version, size, hash;
    /* axl_anims.h (lc2_hook.py): Axl's animation scripts */
    int n_table, n_ents, roll_len;
    short start[AXL_TABLE_MAX];
    axl_ent ents[AXL_ENTS_MAX];
    /* axl_pal.h (lc2_hook.py) */
    unsigned short axl_body_pal[16];
    /* axl_gen.h (build_axl.py, the PS1 build): what the DLL uses of it */
    unsigned char box_from[BOX_FROM_N];
    unsigned short white_pal[16];
    int n_body_pals;
    unsigned char body_pals[BODY_PALS_MAX];
    int dna_pose_len, rope_aim_anim;
    signed char tip_crouch[2], tip_wall[2], tip_rope[4][3][2];
    int fx_opt, fx_shot, fx_bomb, fx_boom, fx_ice, fx_shard, fx_fire, fx_fireball, fx_anchor, fx_ball, fx_rain,
        fx_plate_v, fx_plate_h, fx_plate_hit, fx_counter, fx_shell, fx_ray_h, fx_ray_v, fx_clone, fx_beam;
    /* axl_dna_looks.h (lc2_dna.py header) */
    dna_look_t dna_looks[N_BOSSES];
    signed char dna_box[N_BOSSES][4];
    unsigned char dna_room[N_BOSSES], dna_ghost_tex[N_BOSSES];
    int fx_tex_index, fx_pat, shot_base, shot_nfly, icon_base, giga_burst, n_flames, copy_base, copy_n_ring, copy_orb,
        copy_trail, copy_fade, spike_base, spike_seq_n, icicle_base, wall_spike_base;
    unsigned char spike_seq[SPIKE_SEQ_MAX][2];
    unsigned short giga_pal[16], flame_pal[16];
    int n_flame_frames;
    unsigned char flame_frames[FLAME_FRAMES_MAX];
    signed char flame_spot[FLAME_FRAMES_MAX][2][2];
    unsigned short menu_pals[N_BOSSES * MENU_PALS][16];
    unsigned short dna_pals[N_BOSSES][64];
    /* axl_hud_looks.h (lc2_dna.py hud_header) */
    int hud_tex_index, hud_pat, giga_ball, giga_ball_n, giga_gather, giga_gather_n, giga_orb, giga_orb_n, giga_jet, giga_jet_r, giga_jet_n, giga_form, giga_form_n, giga_opt_rot,
        giga_opt_rot_n, giga_jet_rot, giga_jet_rot_n, giga_bigfire, giga_bigfire_n, giga_spark, giga_spark_n,
        giga_slash, giga_slash_r, giga_slash_n, giga_flamef, giga_flamef_n, giga_flamec, giga_flamec_n;
    unsigned short heat_flame_pal[16];
    unsigned short hud_pals[N_BOSSES][2 * HUD_LEVELS][16];
    /* axl_dna_looks.h: the turned muzzle flash (lc2_dna.flash_looks) */
    int flash_tex_index, flash_pat;
    /* axl_pal.h: the 1UP's own palette (lc2_build.oneup_own_pal, axl_items.c) */
    unsigned short oneup_pal[16];
} axl_data_t;

_Static_assert(sizeof(axl_data_t) == AXL_DATA_SIZE, "axl_data_t layout changed: AXL_DATA_SIZE, AXL_DATA_VERSION");

#ifndef AXL_DATA_PACK
extern axl_data_t axl_d;
int axl_data_load(void);   /* 1 = axl_data.bin read and checked */

/* the names the code used when the tables were compiled in */
#define AXL_N_TABLE (axl_d.n_table)
#define AXL_ROLL_LEN (axl_d.roll_len)
#define axl_start (axl_d.start)
#define axl_ents (axl_d.ents)
#define axl_body_pal (axl_d.axl_body_pal)
#define box_from (axl_d.box_from)
#define white_pal (axl_d.white_pal)
#define N_BODY_PALS (axl_d.n_body_pals)
#define body_pals (axl_d.body_pals)
#define DNA_POSE_LEN (axl_d.dna_pose_len)
#define ROPE_AIM_ANIM (axl_d.rope_aim_anim)
#define tip_crouch (axl_d.tip_crouch)
#define tip_wall (axl_d.tip_wall)
#define tip_rope (axl_d.tip_rope)
#define FX_OPT (axl_d.fx_opt)
#define FX_SHOT (axl_d.fx_shot)
#define FX_BOMB (axl_d.fx_bomb)
#define FX_BOOM (axl_d.fx_boom)
#define FX_ICE (axl_d.fx_ice)
#define FX_SHARD (axl_d.fx_shard)
#define FX_FIRE (axl_d.fx_fire)
#define FX_FIREBALL (axl_d.fx_fireball)
#define FX_ANCHOR (axl_d.fx_anchor)
#define FX_BALL (axl_d.fx_ball)
#define FX_RAIN (axl_d.fx_rain)
#define FX_PLATE_V (axl_d.fx_plate_v)
#define FX_PLATE_H (axl_d.fx_plate_h)
#define FX_PLATE_HIT (axl_d.fx_plate_hit)
#define FX_COUNTER (axl_d.fx_counter)
#define FX_SHELL (axl_d.fx_shell)
#define FX_RAY_H (axl_d.fx_ray_h)
#define FX_RAY_V (axl_d.fx_ray_v)
#define FX_CLONE (axl_d.fx_clone)
#define FX_BEAM (axl_d.fx_beam)
#define dna_looks (axl_d.dna_looks)
#define dna_box (axl_d.dna_box)
#define dna_room (axl_d.dna_room)
#define dna_ghost_tex (axl_d.dna_ghost_tex)
#define FX_TEX_INDEX (axl_d.fx_tex_index)
#define FX_PAT (axl_d.fx_pat)
#define SHOT_BASE (axl_d.shot_base)
#define SHOT_NFLY (axl_d.shot_nfly)
#define ICON_BASE (axl_d.icon_base)
#define GIGA_BURST (axl_d.giga_burst)
#define N_FLAMES (axl_d.n_flames)
#define COPY_BASE (axl_d.copy_base)
#define COPY_N_RING (axl_d.copy_n_ring)
#define COPY_ORB (axl_d.copy_orb)
#define COPY_TRAIL (axl_d.copy_trail)
#define COPY_FADE (axl_d.copy_fade)
#define SPIKE_BASE (axl_d.spike_base)
#define SPIKE_SEQ_N (axl_d.spike_seq_n)
#define ICICLE_BASE (axl_d.icicle_base)
#define WALL_SPIKE_BASE (axl_d.wall_spike_base)
#define spike_seq (axl_d.spike_seq)
#define giga_pal (axl_d.giga_pal)
#define flame_pal (axl_d.flame_pal)
#define N_FLAME_FRAMES (axl_d.n_flame_frames)
#define flame_frames (axl_d.flame_frames)
#define flame_spot (axl_d.flame_spot)
#define menu_pals (axl_d.menu_pals)
#define dna_pals (axl_d.dna_pals)
#define HUD_TEX_INDEX (axl_d.hud_tex_index)
#define HUD_PAT (axl_d.hud_pat)
#define GIGA_BALL (axl_d.giga_ball)
#define GIGA_BALL_N (axl_d.giga_ball_n)
#define GIGA_GATHER (axl_d.giga_gather)
#define GIGA_GATHER_N (axl_d.giga_gather_n)
#define GIGA_ORB (axl_d.giga_orb)
#define GIGA_ORB_N (axl_d.giga_orb_n)
#define GIGA_JET (axl_d.giga_jet)
#define GIGA_JET_R (axl_d.giga_jet_r)
#define GIGA_JET_N (axl_d.giga_jet_n)
#define GIGA_FORM (axl_d.giga_form)
#define GIGA_FORM_N (axl_d.giga_form_n)
#define GIGA_OPT_ROT (axl_d.giga_opt_rot)
#define GIGA_OPT_ROT_N (axl_d.giga_opt_rot_n)
#define GIGA_JET_ROT (axl_d.giga_jet_rot)
#define GIGA_JET_ROT_N (axl_d.giga_jet_rot_n)
#define GIGA_BIGFIRE (axl_d.giga_bigfire)
#define GIGA_BIGFIRE_N (axl_d.giga_bigfire_n)
#define GIGA_SPARK (axl_d.giga_spark)
#define GIGA_SPARK_N (axl_d.giga_spark_n)
#define GIGA_SLASH (axl_d.giga_slash)
#define GIGA_SLASH_R (axl_d.giga_slash_r)
#define GIGA_SLASH_N (axl_d.giga_slash_n)
#define GIGA_FLAMEF (axl_d.giga_flamef)
#define GIGA_FLAMEF_N (axl_d.giga_flamef_n)
#define GIGA_FLAMEC (axl_d.giga_flamec)
#define GIGA_FLAMEC_N (axl_d.giga_flamec_n)
#define HEAT_FLAME_PAL (axl_d.heat_flame_pal)
#define hud_pals (axl_d.hud_pals)
#define FLASH_TEX_INDEX (axl_d.flash_tex_index)
#define FLASH_PAT (axl_d.flash_pat)
#define oneup_pal (axl_d.oneup_pal)
#endif

#endif
