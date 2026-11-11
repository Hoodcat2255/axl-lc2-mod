/* The skill HUD (docs/skills_design.md, user decisions 2026-11-03): while changed, the boss's 4 skills (shot, special,
 * dash, giga) as buttons at the bottom left of the screen - an icon in a square border that fills clockwise as the
 * cooldown runs (gauge = 1 - left / its length), the icon dimmed meanwhile; ready: the border full, the icon bright,
 * a short white flash when it gets ready. Toggles (Yammark's option train and fast flight, Sheldon's plates) show
 * on (full, bright) / off (empty, dim). Hidden while not changed, in the change's pose and leaving blink, with the
 * DNA menu open, while X main does not run (the game's pause, an item's refill freeze), in a cutscene and dying.
 *
 * Drawing: 4 of our projectiles (axl_proj.c, P_HUD: no move, no anim step, no visibility of the game's) with the HUD
 * pictures (lc2_dna.hud_looks: texture HUD_TEX_INDEX, section HUD_PAT + boss - 1, frame HUD_FRAMES * slot + step).
 * Their coordinate system byte W+0x28 = -1: the game takes x / y as screen pixels then (FUN_023bc5d0 / FUN_023bbad0,
 * the off-screen and visibility tests, read it so: a system < 0 skips the screen layer's scroll), no camera to follow.
 * Made in X main (proj_spawn needs it), placed and shown after the unit pass (axl_menu.c move_hook: every frame,
 * also paused); made again when the game freed them (a stage load). Their palettes: HUD_LEVELS pairs (border, icon)
 * in a free run of CLUT slots, uploaded every frame shown, the slots' colours put back when the HUD goes.
 *
 * Off (AXL_NO_HUD, user decision 2026-11-11: "스킬 아이콘 아예 없애버려"): no buttons are made or shown. The HUD
 * texture stays, the giga pictures and Yammark's turned drones are drawn from it.
 */
#include <windows.h>

#include "axl_lc2.h"
#include "axl_proj.h"
#include "axl_data.h"      /* the game-derived tables (axl_data.bin) */
#include "axl_test.h"

#define AXL_NO_HUD   /* (the skill icons gone: see above) */

#define O8(p, o) (*(volatile unsigned char *)((p) + (o)))
#define O16(p, o) (*(volatile unsigned short *)((p) + (o)))
#define O32(p, o) (*(volatile int *)((p) + (o)))

#define HUD_X 22.0f        /* the first button's centre (screen 320 x 240) */
#define HUD_Y 220.0f
#define HUD_GAP 3          /* px between two buttons */
#define HUD_FLASH_LEN 6    /* frames of the ready flash */
#define HUD_SLOTS (2 * HUD_LEVELS)

typedef void(__fastcall *clut_upload_t)(void *ecx, void *edx, int slot, const void *src, int n);
#define CLUT_UPLOAD ((clut_upload_t)0x023AAEF0)
#define CLUT_OWNER (*(void **)0x041D8C44)

static char *cells[SK_N];
static int len_of[SK_N], left_was[SK_N], flash_t[SK_N];
static int pal_lo = -1;
static unsigned short pal_orig[HUD_SLOTS * 16];
int hud_shown;   /* test (axl_test.c hud): the buttons drawn this frame */

static void pal_give(void)
{
    if (pal_lo < 0)
        return;
    if (CLUT_OWNER)
        CLUT_UPLOAD(CLUT_OWNER, 0, pal_lo, pal_orig, HUD_SLOTS);
    pal_lo = -1;
}

static int form_was = -1;   /* Yammark's formation the HUD showed (-1: a new change) */

static void hud_kill(void)
{
    int s;
    form_was = -1;
    for (s = 0; s < SK_N; s++) {
        if (cells[s] && proj_of(cells[s]))
            proj_kill(cells[s]);
        cells[s] = 0;
        len_of[s] = left_was[s] = flash_t[s] = 0;
    }
    pal_give();
    hud_shown = 0;
}

/* X main, every frame (axl_x.c, after dna_after): the buttons made while changed, gone when not */
void hud_frame(char *p)
{
    int s;
#if defined(AXL_NO_SKILLS) || defined(AXL_NO_HUD)   /* (no skill HUD: test5 2026-11-04, then for good 2026-11-11) */
    (void)p;
    (void)s;
    return;
#endif

    if (!dna_boss || dna_boss > 8) {
        if (cells[0] || cells[1] || cells[2] || cells[3] || pal_lo >= 0)
            hud_kill();
        return;
    }
    for (s = 0; s < SK_N; s++) {
        if (cells[s] && proj_of(cells[s]))
            continue;
        cells[s] = proj_spawn(0, HUD_X, HUD_Y, 0, 0, 0, 0, 0, P_NOSCREEN | P_WALL | P_TOUGH | P_PIERCE | P_HUD);
        if (cells[s])
            proj_pose_look(cells[s], HUD_TEX_INDEX, HUD_PAT + dna_boss - 1, HUD_FRAMES * s + HUD_STEPS);
        if (test_on)
            test_log("hud: slot %d made %p (boss %d)", s, cells[s], dna_boss);
    }
}

int hud_dash_on;   /* test (hud_dash): Yammark's dash button drawn on */

/* the slot's state now: its frame step and palette level (on: a toggle's state, else -1) */
static void slot_state(int s, int *step, int *level)
{
    int left = skill_left(s), on = -1;

    if (dna_boss != 1)
        form_was = -1;
    if (s == SK_SPECIAL && dna_boss == 1) {   /* Yammark's formation: a quarter of the border each */
        if (yam_form != form_was)
            flash_t[s] = form_was >= 0 ? HUD_FLASH_LEN : 0;
        form_was = yam_form;
        *step = HUD_STEPS * (yam_form + 1) / 4;
        *level = flash_t[s] && flash_t[s]-- ? HUD_FLASH : HUD_READY;
        left_was[s] = 0;
        return;
    }
    if (s == SK_SPECIAL && skill_defs[dna_boss][s].toggle)
        on = dna_alt != 0;
    else if (s == SK_DASH && dna_boss == 1)
        on = hud_dash_on = axl_fly_fast;
    if (on >= 0) {
        *step = on ? HUD_STEPS : 0;
        *level = on ? HUD_READY : HUD_COOL;
        left_was[s] = 0;
        return;
    }
    if (left > left_was[s])   /* just used: its length is what is left now */
        len_of[s] = left;
    if (!left && left_was[s])
        flash_t[s] = HUD_FLASH_LEN;
    left_was[s] = left;
    if (left && len_of[s] > 0) {
        *step = HUD_STEPS * (len_of[s] - left) / len_of[s];
        *level = HUD_COOL;
        return;
    }
    *step = HUD_STEPS;
    *level = flash_t[s] && flash_t[s]-- ? HUD_FLASH : HUD_READY;
}

/* after every unit moved (axl_menu.c move_hook; menu_open: the DNA menu is up): placed, shown or hidden */
void hud_late(int menu_open)
{
    static unsigned last;
    char *p = axl_p;
    int s, ran = axl_frames != last, show;
#if defined(AXL_NO_SKILLS) || defined(AXL_NO_HUD)
    (void)menu_open;
    (void)p;
    (void)s;
    (void)ran;
    (void)show;
    return;
#endif

    last = axl_frames;
    if (!dna_boss && (cells[0] || cells[1] || cells[2] || cells[3] || pal_lo >= 0)) {
        hud_kill();   /* (the change ended while X main did not run: his death) */
        return;
    }
    show = p && dna_boss && dna_boss <= 8 && ran && !dna_pose && !dna_leaving() && !menu_open && !dna_flashing() &&
           O8(p, 0x0C) == 1 && !O8(p, 0x1EC);
    if (show && pal_lo < 0 && CLUT_OWNER) {
        int lo = clut_free_run(HUD_SLOTS, dna_pal_slot, dna_pal_slot + 4);
        if (lo >= 0) {
            pal_lo = lo;
            memcpy(pal_orig, (unsigned short *)((char *)CLUT_OWNER + 0x1002C) + 16 * lo, sizeof pal_orig);
            if (test_on)
                test_log("hud palettes at slot %d", lo);
        }
    }
    if (pal_lo < 0)
        show = 0;   /* (no free CLUT slots: no HUD rather than wrong colours) */
    else if (show)
        CLUT_UPLOAD(CLUT_OWNER, 0, pal_lo, hud_pals[dna_boss], HUD_SLOTS);
    hud_shown = 0;
    for (s = 0; s < SK_N; s++) {
        char *w = cells[s];
        proj *q = w ? proj_of(w) : 0;
        int step, level;

        if (!q || !q->live)
            continue;
        if (!show) {
            O8(w, 0x0B) = 0;
            continue;
        }
        slot_state(s, &step, &level);
        O8(w, 0x28) = 0xFF;                                   /* screen coordinates */
        *(volatile float *)(w + 0x14) = HUD_X + s * (HUD_CELL + HUD_GAP);
        *(volatile float *)(w + 0x1C) = HUD_Y;
        O32(w, 0x3C) = O32(w, 0x40) = 0;
        *(void *volatile *)(w + 0x78) = 0;                    /* no attack box, no terrain box */
        *(void *volatile *)(w + 0xA8) = 0;
        O32(w, 0x54) = HUD_TEX_INDEX;
        O32(w, 0x58) = HUD_PAT + dna_boss - 1;
        O8(w, 0x66) = (unsigned char)(HUD_FRAMES * s + step);
        O16(w, 0x5E) = (unsigned short)(pal_lo + 2 * level);
        O8(w, 0x29) = 0;                                      /* the picture as made */
        O8(w, 0x2A) = 0;                                      /* the front layer */
        O8(w, 0x0B) = 1;
        hud_shown |= 1 << s;
    }
    if (test_on) {
        static int was = -1;
        if (hud_shown != was)
            test_log("hud shown %x (boss %d)", hud_shown, dna_boss);
        was = hud_shown;
    }
}
