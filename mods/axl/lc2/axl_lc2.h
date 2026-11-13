/* Shared declarations of the Axl DLL for Legacy Collection 2 (addresses: re/lc2/codemap.md) */
#define N_X_ANIMS 193                       /* X's anims (PS1 0x801F4AD4) */
#define X_TABLE ((void **)0x03480798)        /* the player X's table on PC (8-byte entries, axl_data.h axl_ent) */
#define ZERO_TABLE ((void **)0x03480AF0)
#define P_OF(self) (*(char **)((self) + 0xD4))   /* the player work */
#define ANIM_COMP 0x24                       /* P + 0x24: anim component (PS1 obj + 0x30 = P + 0x4C) */
#define P_TABLE(p) (*(void ***)((p) + ANIM_COMP + 0x28))

#include "axl_armor.h"              /* is_normal_x */

typedef void(__fastcall *x_fn)(char *self);
extern int axl_all_weapons, axl_all_parts;                  /* axl_options.txt (axl_hook.c) or the test command all_weapons */
#define OWNED(p) (*(volatile unsigned char *)((p) + 0x1C1))   /* PS1 +0xC9: owned boss weapons, bit k-1 */
#define SAVE_OWNED_AT 0x9C                   /* st + 0x9C = PS1 0x800CCF30, copied to OWNED at the stage start */
extern void *axl_table[];
void axl_table_init(void);

/* axl_x.c: X main hooks (PS1 axl_frame / axl_post) */
void axl_frame(char *self, x_fn orig);
void axl_post(char *self);
void axl_box(char *self);                 /* after X's hurt box: Axl's frames 228+ and rope aims */
void axl_allocated(char *o, int pool, int a, int b);   /* test: pool allocator result */
void axl_spawned(char *w, int kind, int id, int sub, char *owner);   /* X's shot spawner made w */

extern char *axl_self, *axl_p;
extern unsigned axl_frames;
void dna_watch(void);
void axl_shot_setup(char *w);
void axl_dash_cancel(char *p);              /* the player's task and work this frame (0 when not Axl) */

/* axl_dna.c: DNA Change */
extern unsigned char dna_boss, dna_pose, dna_alt;
extern int dna_toss_t, dna_shell_away;   /* axl_dna.c: a toss motion's frame (-1 none); Sheldon's shell out of his hands */
int dna_before(char *p, int special_new, int busy);
int dna_flies(void);
int dna_ghost_palette(int on);         /* axl_dna.c: Yammark's green dash afterimages' slots (-1 none) */
int dna_ghost(char *w);                /* axl_dna.c: w drawn as the changed boss's current picture */                  /* axl_x.c: the changed boss flies (Yammark, Heatnix, Mijinion) */
void dna_after(char *p, int fire, int fire_new, int alt_new);
void dna_cool_reset(void);
int dna_cool_left(void);           /* axl_dna_bosses.c: the boss attack's cooldown left */
void dna_yammark(char *p, int t, int fire, int fire_new);   /* axl_dna_bosses.c */
int dna_hit_loss(char *e, int loss);   /* axl_dna_bosses.c: an enemy's loss to Yammark's wedge charge, made less */
void dna_wolfang(char *p, int t, int fire, int fire_new);
void dna_heatnix(char *p, int t, int fire, int fire_new);
int heat_charge(int fire, int fire_new, int busy, int idle);   /* axl_dna_bosses.c: Heatnix's charged shot */
int heat_charge_stage(void);
void heat_charge_reset(void);
void dna_shark(char *p, int t, int fire, int fire_new);
void dna_scaravich(char *p, int t, int fire, int fire_new);
void dna_turtloid(char *p, int t, int fire, int fire_new);
void dna_sheldon(char *p, int t, int fire, int fire_new);
void dna_mijinion(char *p, int t, int fire, int fire_new);
int dna_giga_invincible(void);         /* axl_dna_bosses.c: Yammark's giga spiral */
int dna_giga_start(char *p, int giga_new);   /* axl_dna_bosses.c: the giga button (changed); 1 = the giga holds him */
void dna_giga_tick(char *p);           /* every changed frame (axl_dna.c dna_after) */
int dna_giga_look(void);               /* the attack motion's frame to show during a giga, -1 none (dna_look) */
void dna_giga_reset(void);             /* the change starts / ends (moves_reset) */
int moves_giga_swim(char *p, int id, int len);   /* axl_moves.c: Shark's giga swim, 0 = not now */

/* axl_objs.c: the game's objects by pool (targets, enemy bullets) */
void objs_alloc(char *o, int pool, int a, int b);
void objs_dump(void);
char *objs_table_near(float x, float y, int r, unsigned table);
int objs_iframe_open(float x, float y, int r);
extern int axl_pierce;                /* axl_x.c: Speed Shot worn, Axl's bullets pierce (axl_proj.c) */
void proj_pierce_new(char *w);         /* axl_proj.c: a new plain bullet, Speed Shot worn */
char *objs_hurt_obj_near(float x, float y, int r);
int objs_guard_open(float x, float y, int r, int id);
void objs_log_near(float x, float y, int r, int id);   /* test */   /* the guard-break bullet opens guards (axl_proj.c) */
extern int objs_mute;
int objs_muted(char *o);
void objs_hp_frame(void);
int objs_hurt_near(float x, float y, int r);
void objs_hp_watch(void);
char *find_target(float x, float y, int range);
char *objs_find_id(int id);
float objs_overhead_bottom(float x, float half_w, float top);   /* a press over a boss */
int objs_solid_beside(float x0, float x1, float y0, float y1);   /* a solid at the player's side */
void dna_press_hold(void);   /* axl_dna.c: before the game lists the solids (axl_hook.c) */
void axl_flash_frame(void);   /* axl_x.c: the muzzle flashes turned to the aim (axl_hook.c flash_state_hook) */
void __cdecl dna_press_task(char *task);
char __fastcall dna_press_contact(void *ecx, void *edx, int i);   /* axl_dna.c: the press's contact flag (axl_hook.c) */   /* axl_dna.c: the press's offset, before layer 1 is set (axl_hook.c) */
void axl_look_late(void);              /* axl_x.c: the changed look put on after the unit move pass */

/* axl_moves.c: the bosses' moves (dash) and second attacks (DNA button) */
int moves_before(char *p, int dash_new, int dash_held, int alt_new);
void moves_reset(char *p);             /* axl_moves.c: nothing of a move carried over */
void moves_after(char *p);
int moves_invincible(void);
int moves_step(char *p, float dx);   /* axl_moves.c: a step along x (slopes up, walls stop) */

/* axl_skills.c: the changed boss's 4 skills and their cooldowns (docs/skills_design.md) */
enum { SK_SHOT, SK_SPECIAL, SK_DASH, SK_GIGA, SK_N };
typedef struct { unsigned short cd; unsigned char toggle; } skill_def;   /* cd frames; toggle: on / off, no cooldown */
extern const skill_def skill_defs[9][SK_N];
int skill_ready(int s);
void skill_used(int s);
int skill_try(int s);                  /* ready: used (1), else refused (0) */
void skill_tick(void);                 /* every changed frame (axl_dna.c dna_after) */
void skill_reset(void);                /* the change starts / ends (moves_reset) */
int skill_left(int s);                 /* frames until ready (SK_SHOT: the attack motion's time left) */
/* axl_skills.c: the changed boss's specialty (user decision 2026-11-09, the flyers' kits: Yammark speed, Heatnix
 * attack, Sheldon defence, Mijinion evasion): flight speed (fixed 16.16 px/f, vertical), the main shot's damage id,
 * damage taken in percent */
typedef struct { int fly; unsigned char shot_id, taken_pct; } boss_mod;
extern const boss_mod boss_mods[9];
int dna_shot_left(void);               /* axl_dna.c */
int dna_leaving(void);                 /* axl_dna.c: the change's leaving blink runs */
int dna_yam_hold(void);                /* axl_dna_bosses.c: Yammark's change of formation holds him */
void yam_next_form(void);              /* axl_dna_bosses.c: Yammark's next formation (the special button) */
extern int yam_form;                   /* 0 circle, 1 wedge, 2 line abreast, 3 column */
extern int axl_fly_fast;               /* axl_x.c: Yammark's dash on (the HUD) */
int moves_yam_dash_dir(int *dy);         /* axl_moves.c: its direction (dx; *dy) */
int moves_dash_charge(void);           /* axl_moves.c: frames Heatnix's dash button has been held (its charge) */
int moves_yam_dash(void);              /* axl_moves.c: Yammark's dash under way */
int moves_yam_ghosts(void);
/* axl_hud.c: the skill HUD (docs/skills_design.md) */
void hud_frame(char *p);               /* X main: the buttons made while changed */
void __fastcall axl_boss_hp_set(char *comp, void *edx, int v);   /* axl_x.c: the boss hold (axl_hook.c) */
int dna_stage_boss_type(int stage);     /* axl_dna.c: stage 1-8's Maverick type, else 0 */
void items_late(void);                /* axl_items.c: after the unit pass, the 1UP's palette slot */
int dna_fire_immune(char *att);      /* axl_dna_bosses.c: Heatnix not hurt by this (fire) */
int dna_lava_immune(void);           /* axl_dna_bosses.c: Heatnix not killed by this death tile (lava) */
void dna_death_tile(void *o, int code);   /* axl_dna_bosses.c: a death tile code met (0x3E / 0x3F) */
void dna_magma_late(void);           /* axl_dna_bosses.c: the Magma Stream flames, after the unit pass */
void hud_late(int menu_open);          /* after the unit pass: placed, shown / hidden */

/* axl_menu.c: the DNA menu (L / R) */
extern int menu_change_to;
#define MENU_AXL 0x100   /* menu_change_to: Axl's own gun (the change ends) */
#define MENU_PICK 0x200  /* menu_change_to | MENU_PICK: that weapon picked only, no change (the jump button) */
int menu_frame(char *self, char *p);
int menu_install(void);
int block_enemy_shots(float x, float y, int r);
int count_enemy_shots(float x, float y, int r);
void dna_look(char *p, int fire);
void dna_unlook(void);
void dna_relook(char *p);
void *code_alloc(unsigned long n);   /* axl_hook.c: stub memory, read / write until code_seal makes it read / execute */
void code_seal(void *t, unsigned long n);
int dna_player_select_install(void);   /* axl_dna.c: PLAYER SELECT starts on Axl */
int dna_rematch_install(void);   /* axl_dna.c: a beaten Maverick fights again when its panel was red */
int dna_giga_row_install(void);   /* axl_dna.c: the pause menu's giga row for normal X too */
int dna_hud_install(void);   /* axl_dna.c: the weapon HUD's number = the change's seconds left */
void dna_reset(void);
void dna_flash_tick(void);
void *dna_hurt_box(void);                   /* the changed boss's hurt box object, 0 = X's */
/* sounds of Axl's bank (FUN_023be620 kind 1; request ids, lc2_build.BOSS_SOUNDS / CHANGE_SOUND) */
void axl_sound(int id);
extern int axl_se_own;   /* axl_dna.c: axl_voice / axl_sound's request under way (se_hook) */
void dna_charge_palette(int on, int k);   /* axl_dna.c: the charge glow's colours, boss k (0 Axl) */
int axl_part_full(void);              /* axl_x.c: the Ultimate Buster worn */
void axl_voice(int id);   /* axl_dna.c: a request on no object */
void axl_sound_stop(int id);           /* axl_dna.c: stops a pl00 request's sound */
extern int dna_pal_slot, menu_pal_lo;   /* our CLUT slots now (axl_dna.c clut_free_run) */
int clut_free_run(int n, int lo, int hi);
int moves_look(void);
#define GIGA_ANIM 231                   /* lc2_hook.GIGA_ANIM: anim GIGA_ANIM + i holds Giga picture i */
#define FLAME_ANIM 246                  /* lc2_hook.FLAME_ANIM: anim FLAME_ANIM + i holds hover flame i */
void dna_screen_flash(void);            /* axl_dna.c */
int dna_flashing(void);
void objs_aim_point(char *e, float *x, float *y);   /* axl_objs.c: where to aim at e (learned, else its hurt box) */
int objs_aim_at(char *e, float *x, float *y);   /* axl_objs.c: where e was last hurt (lately), 1; else 0 */
int objs_targets(float x, float y, int rx, int ry, char **out, int max);   /* axl_objs.c */                   /* axl_moves.c: 1 dash move, 2 second attack, 0 none */
#define MENU_SLOTS 45                   /* 9 badges x MENU_PALS (5), checked in axl_menu.c */
/* damage id of the guard-break bullet: 0x18 = Zero's first saber hit (PS1 ids 0x18-0x1F, 0x23-0x2A are Zero's).
 * The bullet-stopping tables (PS1 0x80075B9C, 0x80075C3C, 0x80075D7C: enemy sabers and shields, reaction 7 for
 * ids 0 and 9) let it through (reaction 2) and 29 enemy tables take damage from it (id 9: 24, all of them
 * included except 0x800745BC). Shield tables 0x80074B5C / 0x800759BC are immune to ids 0 and 9 but take 2 / 1. */
#define GUARD_ID 0x18
#define GUARD_STUN_ID 0x12        /* X's full charge: what stuns High Max */
#define GUARD_SLOT 11             /* the guard-break bullet's palette slot (W+0x5E): X's charge blue */
#define HIGHMAX_TABLE 0x031DF4D8u /* High Max's damage table (PC copy of PS1 0x8007523C, unstunned) */
#define NZERO_TYPE 0x23           /* Nightmare Zero (object id, Yammark stage's blue pillar, hpwatch 2026-10-27; 0x35 on
                                   * the Mijinion stage, 2026-11-08: found by NZERO_TABLE, not by type) */
#define NZERO_TABLE 0x031DF398u   /* his damage table (2 bytes per id: id 0x18 2, buster 1, ids 0x11 / 0x16 8) */
#define GUARD_WEAK_ID 0x32        /* X's saber id (0x32): 5 on Nightmare Zero's table and his flinch (state 12, anim 8 - X
                                   * with the Falcon armour, user 2026-11-08; 0x16 took 8, no flinch) */

#define SND_YAM_SHOT 20
#define SND_YAM_GUARD 34  /* pl00 requests 34 / 35 / 17 / 24: Yammark's "Formation... Guard / Delta (or Wing) / Line / Final!" */
#define SND_YAM_WING 35   /* (lc2_build YAM_VOICES) */
#define SND_YAM_LINE 17
#define SND_YAM_FINAL 24
#define SND_YAM_SPIRAL 16 /* the giga's "Formation... Spiral!" (without the user's file: "Final!") */
#define SND_HEAT_GODBIRD 14   /* pl00 requests 14 / 15 / 9 / 12: Heatnix's "God Bird!" / "Magma Stream!" / "Ha!" /
                              * "Teya!" (lc2_build HEAT_VOICES; 9 / 12 X's own, the game's turned into the shot by se_hook) */
#define SND_HEAT_MAGMA 15
#define SND_HEAT_HA 9
#define SND_HEAT_TEYA 12
#define SND_WOLF_THROW 21
#define SND_WOLF_BREAK 22
#define SND_HEAT_SHOT 23
#define SND_SCAR_KICK 25
#define SND_SCAR_ROLL 26
#define SND_CHANGE 27
#define SND_CHARGE 29  /* pl00 request 29: X's charge loop copied there (lc2_build CHARGE_SOUND) */
#define SND_COPY 10    /* pl00 request 10 = se_pl00_PG00_ID10: X8's copy shot (lc2_build AXL_SOUNDS) */
#define SND_IKUZO 32   /* pl00 request 32: Axl's "ikuzo" (X8), with the beam-in (lc2_build IKUZO_SOUND) */
#define SND_DASH 5     /* pl00 request 5 = se_pl00_PG00_ID05: X8's dash (lc2_build AXL_SOUNDS) */
#define SND_READY 28   /* pl00 request 28: Axl's "Ready" voice, played for the common READY (lc2_build READY_SOUND) */
#define SND_SIREN 33   /* pl00 request 33: X8's warning siren, for the common WARNING (lc2_build SIREN_SOUND) */
#define SIREN_MS 1190  /* its length, 1.2 s (its first wail): back to back */
void warn_tick(void);   /* axl_hook.c: the warning voice's repeats, every X frame */
#define SND_HOVER 30  /* pl00 request 30: X8's burner loop, on while hovering (lc2_build HOVER_SOUND; request 11 faded in late) */
