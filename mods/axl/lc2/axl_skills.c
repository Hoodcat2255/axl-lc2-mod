/* The changed boss's four skills and their cooldowns (docs/skills_design.md, user decisions 2026-11-02 / 2026-11-03):
 * the shot (the shot button), the special (the special button: the second attack, axl_moves.c), the dash (the dash
 * button: the boss's move, axl_moves.c) and the giga (the giga button: the boss's giga attack, axl_dna_bosses.c
 * dna_giga_start - all 8 bosses). Cooldowns in frames (1/60 s), all 0 when a change starts or
 * ends (moves_reset): the giga is ready right after the change. Toggles (Yammark's option train, Sheldon's plates,
 * axl_dna.c dna_alt) have none; Yammark's dash is his flight, held, none either. The shot keeps its interval as the
 * attack's motion (axl_dna.c): its "cooldown" is that motion's time left (dna_shot_left), for the HUD.
 */
#include "axl_lc2.h"
#include "axl_test.h"

#define GIGA_CD 1200   /* 20 s for every boss (user decision 2026-11-03) */

const skill_def skill_defs[9][SK_N] = {
    /*               shot     special    dash      giga */
    /* 0 none     */ {{0, 0}, {0, 0},   {0, 0},   {0, 0}},
    /* 1 Yammark  */ {{0, 0}, {0, 1},   {0, 0},   {GIGA_CD, 0}},   /* option train on / off; the flight is held */
    /* 2 Wolfang  */ {{0, 0}, {180, 0}, {120, 0}, {GIGA_CD, 0}},
    /* 3 Heatnix  */ {{0, 0}, {180, 0}, {120, 0}, {GIGA_CD, 0}},
    /* 4 Shark    */ {{0, 0}, {180, 0}, {180, 0}, {GIGA_CD, 0}},
    /* 5 Scaravich*/ {{0, 0}, {180, 0}, {120, 0}, {GIGA_CD, 0}},
    /* 6 Turtloid */ {{0, 0}, {180, 0}, {150, 0}, {GIGA_CD, 0}},
    /* 7 Sheldon  */ {{0, 0}, {0, 1},   {90, 0},  {GIGA_CD, 0}},   /* plates on / off */
    /* 8 Mijinion */ {{0, 0}, {180, 0}, {150, 0}, {GIGA_CD, 0}},
};

static unsigned short left[SK_N];   /* frames until the slot is ready again */

int skill_ready(int s)
{
    return left[s] == 0;
}

void skill_used(int s)
{
    left[s] = skill_defs[dna_boss < 9 ? dna_boss : 0][s].cd;
    if (test_on)
        test_log("skill %d used (boss %d, cooldown %d)", s, dna_boss, left[s]);
}

/* ready: used now (1); cooling down: refused (0) */
int skill_try(int s)
{
#ifdef AXL_NO_SKILLS   /* (the test5 release leaves the cooldowns out: user decision 2026-11-04) */
    (void)s;
    return 1;
#endif
    if (!skill_ready(s)) {
        if (test_on)
            test_log("skill %d refused (boss %d, cooldown %d left)", s, dna_boss, left[s]);
        return 0;
    }
    skill_used(s);
    return 1;
}

/* every changed frame after the pose (axl_dna.c dna_after) */
void skill_tick(void)
{
    int s;
    for (s = 0; s < SK_N; s++)
        if (left[s])
            left[s]--;
}

void skill_reset(void)
{
    int s;
    for (s = 0; s < SK_N; s++)
        left[s] = 0;
}

/* frames until slot s is ready (the HUD, the test harness's cd_* values) */
int skill_left(int s)
{
    return s == SK_SHOT ? dna_shot_left() : left[s];
}
