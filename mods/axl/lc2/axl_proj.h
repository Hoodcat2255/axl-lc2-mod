/* Axl's own projectiles on PC (axl_proj.c) */
#define N_PROJ 64
#define P_PIERCE 1    /* keep going after a hit */
#define P_WALL 4      /* no terrain test: through walls */
#define P_NOSCREEN 2  /* not freed off screen */
#define P_KEEP 8      /* terrain contact does not free it: the behaviour reads W+0xB0 (1/2 wall, 4 ceiling, 8 floor) */
#define P_TOUGH 16    /* hits never free it, not even blocked ones (options, shell plates) */
#define P_HIDDEN 32   /* not drawn (a body attack box that follows Axl) */
#define P_HUD 64      /* the skill HUD's (axl_hud.c): not moved, no anim step, shown / hidden by its owner */
#define P_GIGA 128    /* a giga attack's (axl_dna_bosses.c, charged damage ids): gone with the change as ids 1-8 */
#define P_HIT1 256    /* with proj_nobox: X's shot's own box for its first frame (a buster hit where it is made: Axl's
                       * Giga Crush barrage - its damage came from these, user 2026-11-08) */
#define P_DNA 512     /* a changed boss's shot with Axl's own damage id (Yammark's gatling, id 0): gone with the change */

typedef struct proj proj;
typedef void (*proj_fn)(char *w, proj *q);
struct proj {
    char *w, *task;
    proj_fn fn;           /* per-frame behaviour before the move (may change the speed); 0 = straight */
    int a, b, c, d;       /* behaviour state */
    int t, life;          /* frames lived, frames to live (0 = until it hits / leaves the screen) */
    int flags;
    unsigned seen;
    const signed char *box;
    float x, y;
    float tx, ty;         /* behaviour state: a spot (Yammark's giga charge) */
    int vx, vy;
    unsigned char id, right, anim, live, nobox, hitno, fx;
    signed char got;      /* this frame's hit (W+0xD4 before it is cleared: > 0 a hit, < 0 blocked), for the behaviour */
    int tex, pat;         /* (proj_pose_look: another texture / parts section than the FX one) */
};

proj *proj_of(char *w);
char *proj_spawn(int id, float x, float y, int vx, int vy, int right, int life, proj_fn fn, int flags);
void proj_kill(char *w);
void proj_kill_dna(void);                       /* every boss-attack projectile (the change ended) */
void proj_spark(char *w);                        /* the buster's wall hit effect at w */
void proj_box(char *w, const signed char *box);  /* attack and terrain box {dx, dy, w, h} */
void proj_nobox(char *w);                        /* no attack box (terrain box kept) */
void proj_boss_look(char *w, int anim);          /* the bosses' projectile pictures, Axl's FX_* anim */
void proj_set_anim(char *w, int anim);
void proj_fx_look(char *w, int frame);
#define PROJ_TEX0 0x100   /* proj_pose_look tex: texture index 0 (the shared pictures, com00) - 0 means the FX one */
void proj_pose_look(char *w, int tex, int pat, int frame);   /* frame of any texture / parts section, held */           /* FX picture (FX_PAT frame) held, from our own table fx_table */
int proj_spawned(char *w);                       /* axl_x.c: X's shot spawner made w; 1 = it is ours */
void proj_frame(void);
void proj_allocated(char *o, int pool, int a, int b);   /* axl_x.c axl_allocated */
int proj_install(void);
int proj_terrain(char *w);
int proj_free(void);                             /* free entries of the table */
int proj_count(int flag);                        /* live ones with that flag */
extern int proj_fails;                           /* spawns refused so far */
int proj_live_count(void);                       /* ours in X's shot count (axl_x.c lends them back) */                       /* terrain bits (1 / 2 wall, 4 ceiling, 8 floor) of w where it is now */
