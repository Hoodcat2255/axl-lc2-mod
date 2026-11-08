/* Packs the generated headers (made from the user's game data: mods/axl/work/lc2/gen/) into axl_data.bin.
 * Built and run on the build machine by tools/lc2_data.py (not part of the DLL):
 *   zig cc -Werror=macro-redefined -I<gen> axl_data_pack.c -o pack && pack axl_data.bin
 * A constant of the mod defined differently here and in axl_data.h stops the build (macro redefined); a table larger
 * than its place in axl_data_t stops it too (the asserts below). */
#include <stdio.h>
#include <string.h>

#define AXL_DATA_PACK
#include "../axl_data.h"
#include "axl_anims.h"
#include "axl_pal.h"
#include "axl_gen.h"
#include "axl_dna_looks.h"
#include "axl_hud_looks.h"

static axl_data_t d;

#define PUT(field, table)                                                                  \
    do {                                                                                   \
        _Static_assert(sizeof(table) <= sizeof(d.field), #table " does not fit " #field); \
        memcpy(&d.field, table, sizeof(table));                                            \
    } while (0)
#define SAME(field, table)                                                                 \
    do {                                                                                   \
        _Static_assert(sizeof(table) == sizeof(d.field), #table " is not " #field "'s size"); \
        memcpy(&d.field, table, sizeof(table));                                            \
    } while (0)

int main(int argc, char **argv)
{
    unsigned h = 2166136261u, i;
    const unsigned char *p = (const unsigned char *)&d;
    FILE *f;

    if (argc != 2) {
        fprintf(stderr, "usage: axl_data_pack OUT\n");
        return 2;
    }
    d.magic = AXL_DATA_MAGIC;
    d.version = AXL_DATA_VERSION;
    d.size = sizeof d;
    /* axl_anims.h */
    d.n_table = AXL_N_TABLE;
    d.n_ents = AXL_N_ENTS;
    d.roll_len = AXL_ROLL_LEN;
    PUT(start, axl_start);
    PUT(ents, axl_ents);
    /* axl_pal.h */
    SAME(axl_body_pal, axl_body_pal);
    SAME(oneup_pal, oneup_pal);
    /* axl_gen.h */
    SAME(box_from, box_from);
    SAME(white_pal, white_pal);
    d.n_body_pals = sizeof body_pals;
    PUT(body_pals, body_pals);
    d.dna_pose_len = DNA_POSE_LEN;
    d.rope_aim_anim = ROPE_AIM_ANIM;
    SAME(tip_crouch, tip_crouch);
    SAME(tip_wall, tip_wall);
    SAME(tip_rope, tip_rope);
    d.fx_opt = FX_OPT;
    d.fx_shot = FX_SHOT;
    d.fx_bomb = FX_BOMB;
    d.fx_boom = FX_BOOM;
    d.fx_ice = FX_ICE;
    d.fx_shard = FX_SHARD;
    d.fx_fire = FX_FIRE;
    d.fx_fireball = FX_FIREBALL;
    d.fx_anchor = FX_ANCHOR;
    d.fx_ball = FX_BALL;
    d.fx_rain = FX_RAIN;
    d.fx_plate_v = FX_PLATE_V;
    d.fx_plate_h = FX_PLATE_H;
    d.fx_plate_hit = FX_PLATE_HIT;
    d.fx_counter = FX_COUNTER;
    d.fx_shell = FX_SHELL;
    d.fx_ray_h = FX_RAY_H;
    d.fx_ray_v = FX_RAY_V;
    d.fx_clone = FX_CLONE;
    d.fx_beam = FX_BEAM;
    /* axl_dna_looks.h */
    SAME(dna_looks, dna_looks);
    SAME(dna_box, dna_box);
    SAME(dna_room, dna_room);
    SAME(dna_ghost_tex, dna_ghost_tex);
    d.fx_tex_index = FX_TEX_INDEX;
    d.fx_pat = FX_PAT;
    d.shot_base = SHOT_BASE;
    d.shot_nfly = SHOT_NFLY;
    d.icon_base = ICON_BASE;
    d.giga_burst = GIGA_BURST;
    d.n_flames = N_FLAMES;
    d.copy_base = COPY_BASE;
    d.copy_n_ring = COPY_N_RING;
    d.copy_orb = COPY_ORB;
    d.copy_trail = COPY_TRAIL;
    d.copy_fade = COPY_FADE;
    d.spike_base = SPIKE_BASE;
    d.spike_seq_n = SPIKE_SEQ_N;
    d.icicle_base = ICICLE_BASE;
    d.wall_spike_base = WALL_SPIKE_BASE;
    PUT(spike_seq, spike_seq);
    SAME(giga_pal, giga_pal);
    SAME(flame_pal, flame_pal);
    d.n_flame_frames = sizeof flame_frames;
    PUT(flame_frames, flame_frames);
    _Static_assert(sizeof flame_spot / sizeof flame_spot[0] == sizeof flame_frames, "flame_spot / flame_frames");
    PUT(flame_spot, flame_spot);
    SAME(menu_pals, menu_pals);
    SAME(dna_pals, dna_pals);
    /* axl_hud_looks.h */
    d.hud_tex_index = HUD_TEX_INDEX;
    d.hud_pat = HUD_PAT;
    d.giga_ball = GIGA_BALL;
    d.giga_ball_n = GIGA_BALL_N;
    d.giga_gather = GIGA_GATHER;
    d.giga_gather_n = GIGA_GATHER_N;
    d.giga_orb = GIGA_ORB;
    d.giga_orb_n = GIGA_ORB_N;
    SAME(hud_pals, hud_pals);
    d.flash_tex_index = FLASH_TEX_INDEX;
    d.flash_pat = FLASH_PAT;

    for (i = 16; i < sizeof d; i++)
        h = (h ^ p[i]) * 16777619u;
    d.hash = h;
    f = fopen(argv[1], "wb");
    if (!f || fwrite(&d, sizeof d, 1, f) != 1 || fclose(f) != 0) {
        perror(argv[1]);
        return 1;
    }
    return 0;
}
