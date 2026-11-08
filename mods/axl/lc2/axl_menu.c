/* DNA menu (user request 2026-10-26, Mega Man ZX style): L (X's previous-weapon button; R is the DNA button since
 * 2026-10-28, axl_x.c) opens a ring of the owned
 * boss badges (X's weapon menu icons on round badges, lc2_dna.menu_icons) around Axl and pause the game: a ring
 * lying flat like a hula hoop (an ellipse; the far half drawn behind him, the near half in front), the chosen badge
 * in front, left / right turn it like a revolver cylinder (eased over a few frames), L / R or the shot button change
 * Axl into that boss, jump closes the menu. The button that closed it is held back from X until released (no shot).
 *
 * Pause: the unit manager's move pass (FUN_01e8db00, every frame) skips a move line whose flags (sUnit + 0x28 +
 * 24 * line) have bit 1; the play lines (PAUSE_LINES: player, objects, map sets, scroll, charge ...; logged in the
 * intro 2026-10-26) get it while the menu is open, the drawing, dialogue and engine lines go on. (The global pause
 * byte *(0x0378D54C)+0x64 stops every line and the engine's streamed BGM too.) The badges are made two frames before the pause (their set-up runs in
 * the move pass), placed in the world around the player (the camera stands still meanwhile). While paused no player
 * code runs, so the
 * menu reads the game's own buttons from the pad system in a hook on that move pass (game_new: the key config and pads
 * apply).
 */
#include <windows.h>

#include "axl_lc2.h"
#include "axl_proj.h"
#include "axl_data.h"      /* the game-derived tables (axl_data.bin) */
#include "axl_test.h"

#define O8(p, o) (*(volatile unsigned char *)((p) + (o)))
#define O16(p, o) (*(volatile unsigned short *)((p) + (o)))
#define N_LINES (*(int *)(*(char **)0x0378DA94 + 0x620))
#define LINE_FLAGS(su, i) (*(volatile unsigned *)((su) + 0x28 + 24 * (i)))
#define LINE_PAUSE 2
static char *sunit;           /* the unit manager (this of the move pass) */
static unsigned line_paused;  /* lines we set the pause bit of */

static int play_line(int i)
{
    return i >= 11 && i <= 32 && i != 26 && i != 29 && i != 30;   /* not dialogue, BG drawing, palette effects */
}

/* the engine works the line flags out again every frame before the move pass: set every frame while open */
static void pause_lines(int on)
{
    int i, n;
    if (!sunit)
        return;
    n = N_LINES < 32 ? N_LINES : 32;
    for (i = 0; i < n; i++) {
        if (on && play_line(i) && !(LINE_FLAGS(sunit, i) & LINE_PAUSE)) {
            LINE_FLAGS(sunit, i) |= LINE_PAUSE;
            line_paused |= 1u << i;
        } else if (!on && (line_paused >> i & 1))
            LINE_FLAGS(sunit, i) &= ~LINE_PAUSE;
    }
    if (!on)
        line_paused = 0;
}
#define UNIT_MOVE 0x01E8DB00u
#define B_LR 0x600            /* X's weapon change buttons (P+0xC0 / C4: 0x400 L, 0x200 R - on the pad, user report 2026-10-29) */
#define B_L 0x400             /* the ring's button (R: the DNA button, axl_x.c) */
#include <math.h>
#define RING_R 60.0f           /* half width of the ring: Axl stands inside it */
#define RING_RY 18.0f          /* half height: the ring lies nearly flat */
#define RING_UP 6.0f           /* its centre above his feet (below his face) */
#define LAYER_FRONT 0          /* draw layer W+0x2A (PS1 +0x16): smaller is drawn over (the player 2, X's shot 1) */
#define LAYER_BACK 3
#define RING_UNWIND 300.0f     /* degrees the badges wind in when the ring opens */
#define RING_EASE 0.7f         /* the turn left each frame */
#define OPEN_FRAMES 2

typedef void(__fastcall *clut_upload_t)(void *ecx, void *edx, int slot, const void *src, int n);
#define CLUT_UPLOAD ((clut_upload_t)0x023AAEF0)
#define CLUT_OWNER (*(void **)0x041D8C44)

static int menu_state;        /* 0 closed, 1 opening (badges being set up), 2 open (paused) */
static int opening, sel, chosen;
static float ring_rot;        /* degrees still to turn (eases to 0) */
static float ring_grow;       /* 0 .. 1: the ring spreads out from Axl when it opens */
static char *icons[9];   /* badge 0 Axl (his own gun), k boss k */
static unsigned char keys_prev[256];
static unsigned short swallow;   /* X's buttons held back after the menu closed, until released */
int menu_change_to;           /* a boss chosen: axl_x.c starts the change with it (MENU_AXL: back to Axl) */

static int key_down(int vk)
{
    return test_on && test_key(vk);   /* (the harness's scripted keys only: the ring reads the game's buttons, and
                                       * no keyboard read in the DLL - 2026-10-29, keylogger-like to scanners) */
}

static int key_new(int vk)
{
    int down = key_down(vk), was = keys_prev[vk];
    keys_prev[vk] = (unsigned char)down;
    return down && !was;
}

/* The game's own buttons while the ring is open (user report 2026-10-28: the menu read fixed keyboard keys - no pad,
 * no key config): P+0xC0 / C4 come from nMegaX6::uPlJoyConvert on move line 12, paused with the play lines, but its
 * source, the pad system sMegaX6Pad (*0x041D8C68, a cSystem: not paused) keeps the raw bits with the collection's own
 * remapping applied (+0x26 held, +0x2A new press); X6's button config table turns them into P's bits (FUN_023ab940
 * does the same with FUN_023aba00: thiscall, 1 stack arg, reads P+0x0A only) */
#define X6PAD (*(char *volatile *)0x041D8C68)
typedef unsigned short(__fastcall *padmap_t)(void *self, void *edx, unsigned short raw);
#define PAD_MAP ((padmap_t)0x023ABA00)
#define G_RIGHT 0x1
#define G_LEFT 0x2
#define G_SHOT 0x20
#define G_JUMP 0x80
#define G_L 0x400

static unsigned short game_new(void)
{
    char *pad = X6PAD;
    return pad ? PAD_MAP(pad, 0, *(volatile unsigned short *)(pad + 0x2A)) : 0;
}

static int owned(char *p, int k)
{
    return !k || ((O8(p, 0x1C1) >> (k - 1)) & 1);   /* (0: Axl himself) */
}

/* the ring position of badge k (0 Axl, boss k) among the owned ones, and their count */
static int ring_pos(char *p, int k, int *n)
{
    int j, pos = 0;
    for (*n = 0, j = 0; j <= 8; j++)
        if (owned(p, j)) {
            if (j == k)
                pos = *n;
            ++*n;
        }
    return pos;
}

/* badge i (0 Axl, boss i) at its place on the ring, its palette by depth or selected */
/* the badges' palette slots: a free run taken at the opening, its colours put back at the close (axl_dna.c
 * clut_free_run; the fixed 410-454 were the gate stage's) */
typedef char menu_slots_check[MENU_SLOTS == 9 * MENU_PALS ? 1 : -1];
int menu_pal_lo = MENU_PAL_SLOT;
static unsigned short menu_pal_orig[MENU_SLOTS * 16];
static int menu_pal_held;

static void menu_pal_take(void)
{
    int s;

    if (!CLUT_OWNER)
        return;
    if (!menu_pal_held) {
        s = clut_free_run(MENU_SLOTS, dna_pal_slot, dna_pal_slot + 4);
        menu_pal_lo = s >= 0 ? s : MENU_PAL_SLOT;
        memcpy(menu_pal_orig, (unsigned short *)((char *)CLUT_OWNER + 0x1002C) + 16 * menu_pal_lo, sizeof menu_pal_orig);
        menu_pal_held = 1;
        if (test_on)
            test_log("menu palettes at slot %d%s", menu_pal_lo, s < 0 ? " (no free run: the default)" : "");
    }
    CLUT_UPLOAD(CLUT_OWNER, 0, menu_pal_lo, menu_pals, MENU_SLOTS);
}

static void menu_pal_give(void)
{
    if (!menu_pal_held)
        return;
    if (CLUT_OWNER)
        CLUT_UPLOAD(CLUT_OWNER, 0, menu_pal_lo, menu_pal_orig, MENU_SLOTS);
    menu_pal_held = 0;
}

static void place(char *p, int i)
{
    char *w = icons[i];
    proj *q = w ? proj_of(w) : 0;
    int n, pos, s;
    float a;

    if (!q || !q->live)
        return;
    pos = ring_pos(p, i, &n);
    s = ring_pos(p, sel, &n);
    a = (-90.0f + (pos - s) * 360.0f / n + ring_rot
         + RING_UNWIND * (1.0f - ring_grow)) * 0.0174533f;   /* opening: unwinds as it grows, a spiral */   /* -90: the near side (down on screen) */
    O8(w, 0x28) = O8(p, 0x28);                            /* the player's coordinate system */
    *(volatile float *)(w + 0x14) = *(volatile float *)(p + 0x14) + RING_R * ring_grow * cosf(a);
    *(volatile float *)(w + 0x1C) = *(volatile float *)(p + 0x1C) - RING_UP - RING_RY * ring_grow * sinf(a);
    *(volatile int *)(w + 0x3C) = *(volatile int *)(w + 0x40) = 0;
    *(void *volatile *)(w + 0x78) = 0;                    /* no attack box, no terrain box */
    *(void *volatile *)(w + 0xA8) = 0;
    *(volatile int *)(w + 0x54) = FX_TEX_INDEX;
    *(volatile int *)(w + 0x58) = FX_PAT;
    O16(w, 0x5E) = (unsigned short)(menu_pal_lo + MENU_PALS * i + (i == sel ? MENU_LEVELS
                                    : (int)((1.0f - sinf(a)) * 0.5f * (MENU_LEVELS - 1) + 0.5f)));   /* far dark, near bright */
    O8(w, 0x66) = (unsigned char)(ICON_BASE + i);
    O8(w, 0x2A) = sinf(a) < 0 ? LAYER_FRONT : LAYER_BACK;   /* near half over Axl, far half behind him */
    O8(w, 0x0B) = 1;                                      /* drawn */
    O8(w, 0x29) = 0;                                      /* facing left = the picture as made (right mirrored the A) */
}

static void close_menu(void)
{
    int i;

    swallow = 0x20 | B_LR | 0x80;                         /* shot, L / R, jump */
    pause_lines(0);
    for (i = 0; i < 9; i++) {
        if (icons[i])
            proj_kill(icons[i]);
        icons[i] = 0;
    }
    menu_state = 0;
    menu_pal_give();
    if (test_on)
        test_log("menu closed, chosen %d", chosen);
}

/* X main (not paused): L / R opens the menu; while it opens Axl gets no input. Returns 1 when it took the input. */
int menu_frame(char *self, char *p)
{
    int i, k;

    if (swallow) {                                        /* the close button: not a shot / weapon change */
        swallow &= O16(p, 0xC0);
        O16(p, 0xC0) &= ~swallow;
        O16(p, 0xC4) &= ~swallow;
    }
    if (menu_state == 0) {
        if (!(O16(p, 0xC4) & B_L) || O8(p, 0x1EC) || dna_pose || !O8(p, 0x1C1))
            return 0;
        sel = dna_boss ? dna_boss : (O8(p, 0x14C) >= 1 && O8(p, 0x14C) <= 8) ? O8(p, 0x14C) : 0;
        if (!owned(p, sel))
            sel = 0;
        for (i = 0; i < 9; i++) {
            icons[i] = owned(p, i) ? proj_spawn(0, *(float *)(p + 0x14), *(float *)(p + 0x1C) - 40, 0, 0, 1, 0, 0,
                                                    P_NOSCREEN | P_WALL | P_TOUGH | P_PIERCE) : 0;
            if (icons[i])
                proj_boss_look(icons[i], FX_OPT);         /* (the badge frame is set by place) */
        }
        menu_pal_take();
        for (k = 0; k < 256; k++)
            keys_prev[k] = (unsigned char)key_down(k);   /* the L / R press that opened it is old */
        chosen = 0;
        ring_rot = 0;
        ring_grow = 0.05f;
        opening = OPEN_FRAMES;
        menu_state = 1;
        if (test_on)
            test_log("menu opening, selected %d", sel);
    }
    O16(p, 0xC0) = 0;
    O16(p, 0xC4) = 0;
    return 1;
}

/* every frame, before the units move (also while paused) */
static void menu_tick(void)
{
    int i, k, step = 0, pad_choose = 0, pad_close = 0;
    char *p = axl_p;

    if (!menu_state || !p)
        return;
    ring_grow += (1.0f - ring_grow) * 0.14f;              /* spreads out over ~30 frames */
    if (ring_grow > 0.99f)
        ring_grow = 1.0f;
    for (i = 0; i < 9; i++)
        place(p, i);
    ring_rot *= RING_EASE;
    if (ring_rot > -0.5f && ring_rot < 0.5f)
        ring_rot = 0;
    if (menu_state == 1) {
        if (--opening <= 0) {
            pause_lines(1);
            menu_state = 2;
            if (test_on)
                test_log("menu open (paused)");
        }
        return;
    }
    if (CLUT_OWNER)
        CLUT_UPLOAD(CLUT_OWNER, 0, menu_pal_lo, menu_pals, 9 * MENU_PALS);
    line_paused = 0;
    pause_lines(1);
    {   /* (the game's buttons: left / right turn, shot or L again changes into the chosen one, jump picks it only) */
        unsigned short g = game_new();
        if (g & G_RIGHT)
            step = 1;
        else if (g & G_LEFT)
            step = -1;
        pad_choose = (g & (G_SHOT | G_L)) != 0;
        pad_close = (g & G_JUMP) != 0;   /* (jump: the weapon picked, no change - user request 2026-10-29) */
    }
    if (step) {
        int n;
        for (k = 0; k < 9; k++) {
            sel = (sel + step + 9) % 9;
            if (owned(p, sel))
                break;
        }
        ring_pos(p, sel, &n);
        ring_rot += step * 360.0f / n;           /* the ring stays where it was and turns to the new choice */
        if (test_on)
            test_log("menu select %d", sel);
    }
    if (pad_choose) {
        chosen = sel;
        menu_change_to = sel ? sel : MENU_AXL;
        close_menu();
    } else if (pad_close) {
        chosen = sel;
        menu_change_to = MENU_PICK | sel;
        close_menu();
    } else if (key_new(VK_ESCAPE))
        close_menu();
}

typedef void(__fastcall *move_t)(void *self);
static move_t move_orig;

/* test: the move lines (sUnit + 0x28 + 24 * line: flags, + 0x2c first unit; unit + 0x14 next) and their units */
static void log_lines(char *su)
{
    int n = *(int *)(*(char **)0x0378DA94 + 0x620), i, c;
    char *u;
    for (i = 0; i < n; i++) {
        char vts[200] = "";
        int len = 0;
        u = *(char **)(su + 0x2c + 24 * i);
        for (c = 0; u && c < 400; c++, u = *(char **)(u + 0x14))
            if (c < 12)
                len += wsprintfA(vts + len, " %08x", *(unsigned *)u);
        test_log("line %d flags %08x units %d:%s", i, *(unsigned *)(su + 0x28 + 24 * i), c, vts);
    }
}

static void __fastcall move_hook(void *self)
{
    sunit = (char *)self;
    if (test_on && menu_state == 1 && opening == OPEN_FRAMES)
        log_lines((char *)self);
    dna_unlook();      /* (the last frame's look off before any unit moves: only the draw sees it) */
    menu_tick();
    if (!menu_state)
        dna_watch();
    {
        unsigned before = axl_frames;
        move_orig(self);
        if (test_on && axl_p && axl_frames == before && !menu_state) {   /* (test: X main did not run this pass -
            * an item's refill freeze? what the shot / charge do meanwhile, user report 2026-10-28) */
            static unsigned last;
            unsigned now = (unsigned)O16(axl_p, 0xC0) << 8 | O8(axl_p, 0x155);
            if (now != last)
                test_log("x main idle: held %04x charge %d init %d state %d", O16(axl_p, 0xC0), O8(axl_p, 0x155),
                         O8(axl_p, 0x0C), O8(axl_p, 0x0D));
            last = now;
        }
    }
    items_late();      /* (axl_items.c: the 1UP's own palette slot) */
    axl_look_late();   /* (axl_x.c: this frame's look, every unit moved) */
    hud_late(menu_state != 0);   /* (axl_hud.c: the skill HUD placed and shown, or hidden) */
}

int menu_install(void)
{
    static const unsigned char entry[8] = {0x51, 0x57, 0x8B, 0xF9, 0x8D, 0x4C, 0x24, 0x04};
    unsigned char *t, b[8];
    DWORD old;

    if (memcmp((void *)UNIT_MOVE, entry, 8))
        return 0;
    t = code_alloc(16);
    if (!t)
        return 0;
    memcpy(t, entry, 8);
    t[8] = 0xE9;
    *(LONG *)(t + 9) = (LONG)((UNIT_MOVE + 8) - ((DWORD)t + 13));
    code_seal(t, 16);
    move_orig = (move_t)t;
    b[0] = 0xE9;
    *(LONG *)(b + 1) = (LONG)((DWORD)move_hook - (UNIT_MOVE + 5));
    b[5] = b[6] = b[7] = 0x90;
    if (!VirtualProtect((void *)UNIT_MOVE, 8, PAGE_EXECUTE_READWRITE, &old))
        return 0;
    memcpy((void *)UNIT_MOVE, b, 8);
    VirtualProtect((void *)UNIT_MOVE, 8, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void *)UNIT_MOVE, 8);
    return 1;
}
