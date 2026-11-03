/* Test harness of the Axl DLL: active only when axl_test.txt exists next to RXC2.exe.
 *
 * axl_test.txt, one command per line ('#' comments). Frames are the game's input updates (its keyboard read
 * 0x01E74FA0, hooked: loading or a pause stops the count, and the keys a frame sees are fixed at its start); the
 * game reads the keys with GetAsyncKeyState, whose import is wrapped:
 *   clock wall           frames = 1/60 s of real time since the hooks were installed instead (a timer thread: the
 *                        harness before 2026-10-28; a long load shifts every later frame)
 *   key F1 F2 NAME...    hold the keys (virtual-key names below, A-Z, 0-9 or 0xNN) from frame F1 to F2 inclusive
 *   pdump F1 F2          append every X main call (player frame) in that range to axl_pdump.bin
 *   quit F               end the process at frame F
 *   stdump F             log *0x041D8C8C + 0 .. 0x300 (game object; st = + 0x70) as hex at frame F (ticker thread)
 *   cdump F              log which emulated CLUT slots are in use at frame F
 *   objdump F            log the live objects of every pool (axl_objs.c) at frame F
 *   talk F1 F2           from F1 to F2 press Z (4 frames in 30) while the player is held (dialogue, READY): the
 *                        stage's dialogue runs on and no jump follows it; never in the Hunter base (stage 15: Z saves)
 *   skipto F             once the player has been free for 60 frames (after the opening): the frame count becomes F
 *                        (scripts keep their play-time frames however long the opening took)
 *   strscan F            at frame F log every distinct "X6\\..." resource name in memory (not sounds)
 *   animlog F1 F2        from F1 to F2 log the player's state, anim, anim cursor, frame and parts section every X main call
 *   guard_all            every bullet is a guard-break bullet (axl_x.c)
 *   etable F VA          from frame F every live enemy uses the damage table at VA (hex; PS1 table copies 0x031DE8F8 +)
 *   hpwatch F            from frame F log every change of an enemy's damage table / hit points (axl_objs.c)
 *   wait_go              frame 0 is the first player frame after axl_go.txt appears next to RXC2.exe (the user
 *                        drives the game to the place, then gives it the focus back: LC2 opens its menu without it,
 *                        Tab is pressed every 2 s until the game runs again; the script starts when the
 *                        player is free (no input lock). Removing axl_go.txt arms it again: the next go file
 *                        runs the script once more, frames from 0)
 *   pfloat F1 F2 OFF V   from frame F1 to F2, write the float V to the player work P + OFF (hex) every X main call
 *   weakid N             objects of id N count as the weakness boss of any DNA form (axl_dna.c weak_hit)
 *   all_weapons          every boss weapon owned (as axl_options.txt's all_weapons)
 *   xanim F N            at frame F (after X's state code) X's anim N through his own anim call FUN_023aea40 (it picks
 *                        the anim's bank: the victory anims 95 / 97 without beating a boss)
 *   ppoke F1 F2 OFF V    (ppoke8: one byte) from frame F1 to F2, write the 16-bit V to the player work P + OFF (hex) every X main call
 *   memscan F HEX        at frame F log every address where the bytes HEX (up to 64) are in committed memory
 *   snap F               log "snap F" at frame F (lc2_test.py grabs the window when it reads it)
 *   scan F               at the first X main call from frame F: log every dword in [LO, HI) found in this / P or
 *                        one pointer away (0x400 bytes each), as path = value
 *   scanrange LO HI      the value range for scan (hex; default 0x03280000 0x03490000, the anim tables)
 *   tdump ADDR N         at the first X main call: N table pointers at ADDR (hex) and 512 bytes from each pointer
 *                        -> axl_tdump.bin (u32 ADDR, u32 N, N u32 pointers, N * 512 bytes; unreadable = 0xff)
 *   poke F OFF V         at frame F (and every frame after, until another poke of OFF) write byte V to the game
 *                        state st + OFF (st = *(0x041D8C8C) + 0x70 = PS1 0x800CCE94); hex OFF and V
 *
 * After a line 'seq' the script is a scenario: one step at a time, each step waiting on the game frames (it runs
 * next to the lines above, e.g. base_fast's menu keys). Steps:
 *   wait COND [T]        until COND holds (checked every frame); not within T frames (default 3600): 'e2e FAIL'
 *                        and the scenario ends
 *   expect COND [LABEL]  'e2e PASS LABEL' or 'e2e FAIL LABEL (NAME = value)'; goes on either way
 *   press N KEY...       hold the keys N frames;  idle N: N frames
 *   talk N [COND]        Space and Z by turns, 4 frames in 30, while the player is held (Space skips cutscenes, Z
 *                        pages dialogues) for N frames, or until COND (not within N: 'e2e FAIL', the end); never in
 *                        the Hunter base (stage 15: Z there saves)
 *   page N [COND]        as talk, Z only, whether the player is held or not (a boss's dialogue in which he counts
 *                        as free: Sigma's)  hold KEY... / release: keys held from now on
 *   set OFF V            the game state byte st + OFF = V (hex) now;  pset OFF V: the player work byte P + OFF
 *   bset OFF V           the live boss's byte + OFF = V now (bset 84 1: hit points 1)
 *   trace N              the next N frames: a log line each (place, state, terrain bytes, speed) - goes on with the steps
 *   nolift on|look|off   the changed look drawn without its lift (axl_dna.c: the player moved up for the draw) / no
 *                        look at all (drawn as Axl)
 *   mark                 dx dy rise shots count from here;  show NAME...: 'e2e show NAME=value ...' (measured values)
 *   unchange             the DNA change ended now (axl_dna.c dna_test_end; the gauge kept: with energy full the
 *                        next change needs no gauge writes - writing a gauge the game counts as not owned crashed
 *                        its HUD after a respawn, 2026-10-28)
 *   ghost on|off         no hurt box for the player (axl_hook.c x_box_hook): enemies and their shots pass through,
 *                        no hit stun or knockback; the terrain box and walls are untouched
 *   energy full|off      every weapon gauge above 0 kept full (an emptied gauge stays 0: that ends a change)
 *   god on|off           the player's hit points (P+0x84) and lives (st+0x75) kept up every frame
 *   bot boss|right|left|off  shooting all the time; boss: toward a live boss, aimed at it (no boss: walking right);
 *                        right / left: walking that way, jumping at walls and when stuck (bot_keys); off: no bot keys
 *   snap / log TEXT      'snap F' (lc2_test.py grabs the window) / a log line 'e2e log TEXT'
 *   goto X Y [S]         (once the player is free: not while dying / respawning) the player moved S px a frame (default 4) along a path to (X, Y) through the stage's free
 *                        space: a breadth-first search on 16 px cells, a cell free when the player's terrain box
 *                        there meets no terrain and the cell above is free too (proj_terrain: it reports terrain edges only, so solid rock reads
 *                        free - the start's corridor walls keep the path in); no path: 'e2e FAIL', the end
 *   survey X0 Y0 X1 Y1   the terrain bits (proj_terrain & 0xF, hex, '.' none) of every 16 px cell of the rectangle,
 *                        a log line 'e2e terrain Y row' per row (to pick test places: walls, slopes, ceilings)
 *   objs                 one log line of the live objects' types (header byte W+9) of 0x30 and up (bosses, events)
 *   xwatch ADDR [OFF]    execution break (DR1) at ADDR (hex): ecx of up to 8 different calls logged; with OFF a
 *                        2-byte write watch on the first ecx + OFF; with POOL I only the task whose work (+0xC0)
 *                        is that remembered object; 'xwatch ADDR OFF eax' uses eax instead of ecx
 *   owatch POOL I OFF    write watch (DR0) on the I-th remembered object of POOL + OFF (hex)
 *   gwatch PTR OFF       write watch (DR0) on *PTR + OFF (hex): each writer EIP logged once
 *   gdump PTR LEN [OFF]  one log line: LEN bytes (decimal, up to 2048) at *PTR + OFF (PTR, OFF hex)
 *   end                  'e2e end pass P fail F', the process ends 60 frames later (the last snap grabbed)
 * The scenario runs on the game clock only (not with clock wall); a step that does not parse fails it at once.
 * COND = NAME OP VALUE, OP one of == != < > <= >=, VALUE decimal or 0x hex. NAME: stage area state checkpoint
 * lives (st bytes +0x48 +0x49 +0x3C +0x59 +0x75; state is always 0 on PC, codemap), hp anim (P+0x84, P+0x2B), free (1 once the player has not been
 * held by a cutscene / READY for 30 frames), px py (P's position, pixels), boss_alive, boss_hp (a live boss object, axl_objs.c objs_boss: the intro's 0x30, the Mavericks, Sigma),
 * st_XX / p_XX (any byte, hex offset), steps (frames spent in this step), embedded (1 when the player's terrain box
 * meets walls on both sides or the floor and the ceiling at once: stuck in the terrain), dx dy rise shots (since the
 * last 'mark': moved right / down in px, the highest point above the mark, our projectiles made), dna_shots (live
 * boss-attack projectiles: none once a change ended), cd_shot cd_special cd_dash cd_giga (frames until that skill
 * of the changed boss is ready again, axl_skills.c; cd_shot = the attack motion's frames left), hud (the skill HUD's
 * buttons drawn this frame, bit = slot: 15 all four, 0 hidden; axl_hud.c).
 * Log lines go to axl_lc2.log (always when the harness is active).
 * axl_pdump.bin records: 'AXLP', u32 frame, u32 this, u32 P (= *(this + 0xD4)), u32 phase (0 before X's state code,
 * 1 after), 0x100 bytes of this, 0x400 bytes of P.
 */
#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "axl_test.h"

extern char *axl_p;   /* axl_x.c: the player work P */
int proj_terrain(char *w);   /* axl_proj.c: the game's terrain flags for w's terrain box where w is */
char *find_target(float x, float y, int range);   /* axl_objs.c */
#define O8B(p, o) (*(volatile unsigned char *)((p) + (o)))

#ifdef AXL_RELEASE
/* the release build (lc2_hook.py --release): no harness at all - nothing read, nothing logged */
int test_on;
int test_frame;
int test_clock_wall;
int test_ghost;
void test_init(void) {}
void test_input(void) {}
void test_log(const char *fmt, ...) { (void)fmt; }
int test_key(int vk) { (void)vk; return 0; }
void test_player(char *self, int phase) { (void)self; (void)phase; }
void test_watch(void *addr) { (void)addr; }
void test_projlook(int *tex, int *pat, int *anim) { (void)tex; (void)pat; (void)anim; }
#else

#define MAX_RUNS 256
#define DUMP_THIS 0x100
#define DUMP_P 0x400

typedef struct { int f1, f2; unsigned char vk[8]; int n; } key_run;

static key_run runs[MAX_RUNS];
static int n_runs, pdump_f1 = -1, pdump_f2 = -1, quit_f = -1, scan_f = -1;
static unsigned scan_lo = 0x03280000, scan_hi = 0x03490000;
typedef struct { int f; unsigned off; unsigned char v; } poke_t;
static poke_t pokes[32];
static int n_pokes;
static int snaps[64], n_snaps;
static int objdumps[16], n_objdumps;
static int hpwatch_f = -1, etable_f = -1;
static int animlog_f1 = -1, animlog_f2 = -1, strscan_f = -1;
static int talk_f1 = -1, talk_f2 = -1, held_now = 1, skipto_f = -1, free_n;
static unsigned etable_va;
static int cdumps[8], n_cdumps;
static int go_wait, go_ticks, go_used;
static struct { int f1, f2; unsigned off; float v; } pfloats[8];
static int n_pfloats;
static int stdumps[16], n_stdumps;
typedef struct { char name[16]; int op, v; } cond_t;
typedef struct {
    int op, n, v, off;
    cond_t c;
    unsigned char vk[8];
    char text[48];
} step_t;
enum { S_TRACE, S_NOLIFT, S_SHOW, S_MARK, S_UNCHANGE, S_GHOST, S_ENERGY, S_SURVEY, S_BSET, S_GOTO, S_WAIT, S_EXPECT, S_PRESS, S_IDLE, S_TALK, S_HOLD, S_RELEASE, S_SET, S_PSET, S_GOD, S_BOT, S_SNAP, S_LOG, S_OBJS, S_GDUMP, S_GWATCH, S_OWATCH, S_XWATCH, S_END };
#define MAX_STEPS 8192   /* (moves_intro: about 8000 steps) */
static step_t steps[MAX_STEPS];
static float mark_x, mark_y, mark_top;   /* mark: the place then, the highest point (least y) since */
static int mark_shots;
static int trace_n;   /* trace N: frames still to log */
static int step_new = 1;
static char cur_label[48];   /* the last 'log' text (a death names it) */   /* the current step runs its first frame (goto: its path) */
static int seq_bad;   /* steps that did not parse or did not fit: the scenario fails at its start */
static int n_steps, step_at, step_t0, seq_on, seq_pass, seq_fail, seq_quit = -1, god, bot, energy_full;
static unsigned char seq_down[256], hold_down[256];
static int last_pframe = -100;   /* the last frame X main ran (free: he is updated) */
int test_clock_wall;               /* clock wall: frames from the timer thread, not from the game's input updates
                                     * (axl_hook.c sets it when the keyboard read cannot be hooked) */
#define clock_wall test_clock_wall
static volatile LONG wall_frame;   /* 1/60 s since the harness started (the 'clock' log lines compare the two) */

/* seq gdump (2026-11-02, the Metal Shark stage's ceiling press): n bytes of the object a global pointer at ptr points
 * to, as hex (readable memory only) */
static void g_dump(unsigned ptr, int n, int at)
{
    static char hex[0x800 * 2 + 1];
    MEMORY_BASIC_INFORMATION mi;
    char *o = 0;
    int k;
    if (VirtualQuery((void *)ptr, &mi, sizeof mi) && mi.State == MEM_COMMIT && *(char **)ptr)
        o = *(char **)ptr + at;
    if (!o || !VirtualQuery(o, &mi, sizeof mi) || mi.State != MEM_COMMIT ||
        (char *)mi.BaseAddress + mi.RegionSize < o + n) {
        test_log("e2e gdump %08x: unreadable", ptr);
        return;
    }
    for (k = 0; k < n; k++)
        wsprintfA(hex + 2 * k, "%02x", (unsigned char)o[k]);
    test_log("e2e gdump %08x %p %s", ptr, o, hex);
}

/* the game state st (*0x041D8C8C + 0x70, PS1 0x800CCE94) and the 0x70 bytes before it, as hex */
static void st_dump(void)
{
    char *g = *(char **)0x041D8C8C, hex[0x300 * 2 + 1];
    int k;
    if (!g)
        return;
    for (k = 0; k < 0x300; k++)
        wsprintfA(hex + 2 * k, "%02x", (unsigned char)g[k]);
    test_log("stdump %s", hex);
}   /* wait_go: frames count from when axl_go.txt appears (the user drove the game there) */

/* the user drives the game (wait_go): his keys and the real focus count; else the run is the script's alone */
int test_user_drives(void)
{
    return go_used;
}

static BOOL CALLBACK raise_game(HWND h, LPARAM unused)
{
    DWORD pid;
    (void)unused;
    GetWindowThreadProcessId(h, &pid);
    if (pid != GetCurrentProcessId() || !IsWindowVisible(h) || GetWindow(h, GW_OWNER))
        return TRUE;
    ShowWindow(h, SW_RESTORE);
    SetForegroundWindow(h);
    test_log("raised window %p", (void *)h);
    return FALSE;
}
static int projlooks[8][4], n_projlooks;

/* projlook F TEX PAT ANIM: from frame F our projectiles use that picture (-1 = their own) */
void test_projlook(int *tex, int *pat, int *anim)
{
    int k;
    for (k = 0; k < n_projlooks; k++)
        if (test_frame >= projlooks[k][0]) {
            *tex = projlooks[k][1];
            *pat = projlooks[k][2];
            *anim = projlooks[k][3];
        }
}

/* the emulated VRAM CLUTs (*0x041D8C44 + 0x1002C + 32 * slot): one char per slot, '.' all zero, 'x' used */
static void clut_dump(void)
{
    char *base = *(char **)0x041D8C44, line[257];
    int s, i, used;

    if (!base)
        return;
    for (s = 0; s < 1024; s++) {
        for (used = 0, i = 0; i < 32; i++)
            used |= base[0x1002C + 32 * s + i];
        line[s & 255] = used ? 'x' : '.';
        if ((s & 255) == 255) {
            line[256] = 0;
            test_log("clut %4d %s", s - 255, line);
        }
    }
    for (s = 0; s < 128; s++) {   /* the first slots' colours (X's, the HUD's) */
        unsigned short *c = (unsigned short *)(base + 0x1002C + 32 * s);
        test_log("clut slot %2d: %04x %04x %04x %04x %04x %04x %04x %04x %04x %04x %04x %04x %04x %04x %04x %04x", s,
                 c[0], c[1], c[2], c[3], c[4], c[5], c[6], c[7], c[8], c[9], c[10], c[11], c[12], c[13], c[14], c[15]);
    }
}
void objs_dump(void);
void objs_hp_watch(void);
void objs_set_table(unsigned va);
static int memscan_f = -1, memscan_n;
static unsigned char memscan_pat[64];
typedef struct { int f1, f2; unsigned off; unsigned short v; int bytes; } ppoke_t;
static ppoke_t ppokes[16];
static int xanims[8][2], n_xanims;
typedef void(__stdcall *xanim_t)(char *p, int anim);
#define X_ANIM_BANK ((xanim_t)0x023AEA40)
static int n_ppokes;
static unsigned tdump_addr, tdump_n;
static FILE *logf, *dumpf;
static unsigned char down[256];        /* keys held by the script this frame */
static unsigned char polled[256 / 8];  /* keys the game asked for (logged once) */
int test_on;
int test_frame;
int test_ghost;

static const struct { const char *name; unsigned char vk; } VK[] = {
    {"ESCAPE", 0x1B}, {"RETURN", 0x0D}, {"SPACE", 0x20}, {"SHIFT", 0x10}, {"LSHIFT", 0xA0}, {"RSHIFT", 0xA1},
    {"CONTROL", 0x11}, {"LCONTROL", 0xA2}, {"TAB", 0x09}, {"BACK", 0x08}, {"UP", 0x26}, {"DOWN", 0x28},
    {"LEFT", 0x25}, {"RIGHT", 0x27}, {"F1", 0x70}, {"F2", 0x71},
};

static void game_dir(char *out, const char *file)
{
    char *p;
    GetModuleFileNameA(NULL, out, MAX_PATH);
    p = strrchr(out, '\\');
    if (p)
        p[1] = 0;
    lstrcatA(out, file);
}

void test_log(const char *fmt, ...)
{
    va_list ap;
    if (!logf)
        return;
    fprintf(logf, "[%6d] ", test_frame);
    va_start(ap, fmt);
    vfprintf(logf, fmt, ap);
    va_end(ap);
    fputc('\n', logf);
    fflush(logf);
}

static int vk_of(const char *s)
{
    unsigned i;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
        return (int)strtoul(s, NULL, 16);
    if (!s[1] && ((s[0] >= 'A' && s[0] <= 'Z') || (s[0] >= '0' && s[0] <= '9')))
        return s[0];
    for (i = 0; i < sizeof VK / sizeof VK[0]; i++)
        if (!lstrcmpiA(s, VK[i].name))
            return VK[i].vk;
    return -1;
}

static void test_tick(int from_game);
static int value_of(const char *name, int *ok);

/* the timer thread: real time (wall_frame), the go file wait and, with clock wall, the frames */
static DWORD WINAPI ticker(LPVOID arg)
{
    LARGE_INTEGER f, t0, t;
    (void)arg;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&t0);
    for (;;) {
        Sleep(4);
        QueryPerformanceCounter(&t);
        while (wall_frame < (int)((t.QuadPart - t0.QuadPart) * 60 / f.QuadPart)) {
            InterlockedIncrement(&wall_frame);
            test_tick(0);
        }
    }
    return 0;
}

/* the game's keyboard read (axl_hook.c): one game frame */
void test_input(void)
{
    if (test_on)
        test_tick(1);
}

static const char *const OPS[] = {"==", "!=", "<", ">", "<=", ">="};

static const char *const NAMES[] = {"stage", "area", "state", "checkpoint", "lives", "hp", "anim", "free", "px", "py",
                                    "boss_alive", "boss_hp", "steps", "embedded", "dx", "dy", "rise", "shots", "dna_shots",
                                    "cd_shot", "cd_special", "cd_dash", "cd_giga", "hud"};

static int parse_cond(char **tok, int n, cond_t *c)
{
    int k, known;
    if (n < 3)
        return 0;
    known = !strncmp(tok[0], "st_", 3) || !strncmp(tok[0], "p_", 2);
    for (k = 0; k < (int)(sizeof NAMES / sizeof NAMES[0]); k++)
        known |= !strcmp(tok[0], NAMES[k]);
    if (!known) {
        test_log("seq: unknown value '%s'", tok[0]);
        return 0;
    }
    lstrcpynA(c->name, tok[0], sizeof c->name);
    for (c->op = -1, k = 0; k < 6; k++)
        if (!strcmp(tok[1], OPS[k]))
            c->op = k;
    c->v = (int)strtol(tok[2], NULL, 0);
    return c->op >= 0;
}

static void parse_step(char **tok, int n)
{
    step_t *t = &steps[n_steps];
    int k, ok = 1;
    if (n_steps >= MAX_STEPS) {
        seq_bad++;
        test_log("seq: bad step '%s': more than %d steps", tok[0], MAX_STEPS);
        return;
    }
    memset(t, 0, sizeof *t);
    if (!strcmp(tok[0], "wait")) {
        t->op = S_WAIT;
        ok = parse_cond(tok + 1, n - 1, &t->c);
        t->n = n > 4 ? atoi(tok[4]) : 3600;
    } else if (!strcmp(tok[0], "expect")) {
        t->op = S_EXPECT;
        ok = parse_cond(tok + 1, n - 1, &t->c);
        lstrcpynA(t->text, n > 4 ? tok[4] : tok[1], sizeof t->text);
    } else if (!strcmp(tok[0], "press") || !strcmp(tok[0], "hold")) {
        t->op = tok[0][0] == 'p' ? S_PRESS : S_HOLD;
        k = t->op == S_PRESS ? 2 : 1;
        t->n = t->op == S_PRESS && n > 1 ? atoi(tok[1]) : 0;
        for (; k < n && t->v < 8; k++) {
            int d = vk_of(tok[k]);
            if (d >= 0)
                t->vk[t->v++] = (unsigned char)d;
            else
                ok = 0;
        }
    } else if (!strcmp(tok[0], "idle") && n == 2) {
        t->op = S_IDLE;
        t->n = atoi(tok[1]);
    } else if ((!strcmp(tok[0], "talk") || !strcmp(tok[0], "page")) && (n == 2 || n == 5)) {
        t->op = S_TALK;
        t->off = tok[0][0] == 'p';   /* page: Z only, the player held or not */
        t->n = atoi(tok[1]);
        t->v = n == 5;   /* until COND */
        if (t->v)
            ok = parse_cond(tok + 2, 3, &t->c);
    } else if (!strcmp(tok[0], "show") && n >= 2) {   /* (the names into text, space separated) */
        t->op = S_SHOW;
        for (k = 1; k < n; k++) {
            int len = lstrlenA(t->text);
            if (len && len < (int)sizeof t->text - 1)
                t->text[len++] = ' ', t->text[len] = 0;
            lstrcpynA(t->text + len, tok[k], (int)sizeof t->text - len);
        }
    } else if (!strcmp(tok[0], "trace") && n == 2) {
        t->op = S_TRACE;
        t->n = atoi(tok[1]);
    } else if (!strcmp(tok[0], "nolift") && n == 2) {
        t->op = S_NOLIFT;
        t->v = !strcmp(tok[1], "on") ? 1 : !strcmp(tok[1], "look") ? 2 : 0;
    } else if (!strcmp(tok[0], "mark"))
        t->op = S_MARK;
    else if (!strcmp(tok[0], "unchange"))
        t->op = S_UNCHANGE;
    else if (!strcmp(tok[0], "release"))
        t->op = S_RELEASE;
    else if (!strcmp(tok[0], "bset") && n == 3) {
        t->op = S_BSET;
        t->off = (int)strtoul(tok[1], NULL, 16);
        t->v = (int)strtoul(tok[2], NULL, 16);
    } else if ((!strcmp(tok[0], "set") || !strcmp(tok[0], "pset")) && n == 3) {
        t->op = tok[0][0] == 's' ? S_SET : S_PSET;
        t->off = (int)strtoul(tok[1], NULL, 16);
        t->v = (int)strtoul(tok[2], NULL, 16);
    } else if (!strcmp(tok[0], "ghost") && n == 2) {
        t->op = S_GHOST;
        t->v = !strcmp(tok[1], "on");
    } else if (!strcmp(tok[0], "energy") && n == 2) {
        t->op = S_ENERGY;
        t->v = !strcmp(tok[1], "full");
    } else if (!strcmp(tok[0], "god") && n == 2) {
        t->op = S_GOD;
        t->v = !strcmp(tok[1], "on");
    } else if (!strcmp(tok[0], "bot") && n == 2) {
        t->op = S_BOT;
        t->v = !strcmp(tok[1], "boss") ? 1 : !strcmp(tok[1], "right") ? 2 : !strcmp(tok[1], "left") ? 3 : 0;
    } else if (!strcmp(tok[0], "survey") && n == 5) {
        t->op = S_SURVEY;
        t->off = atoi(tok[1]);
        t->v = atoi(tok[2]);
        t->n = atoi(tok[3]);
        t->c.v = atoi(tok[4]);   /* (x0 y0 x1 y1: off v n c.v) */
    } else if (!strcmp(tok[0], "goto") && (n == 3 || n == 4)) {
        t->op = S_GOTO;
        t->off = atoi(tok[1]);
        t->v = atoi(tok[2]);
        t->n = n == 4 ? atoi(tok[3]) : 4;
    } else if (!strcmp(tok[0], "snap"))
        t->op = S_SNAP;
    else if (!strcmp(tok[0], "objs"))
        t->op = S_OBJS;
    else if (!strcmp(tok[0], "gdump") && (n == 3 || n == 4)) {   /* gdump PTR LEN [OFF]: LEN bytes of *PTR + OFF */
        t->op = S_GDUMP;
        t->off = (int)strtoul(tok[1], NULL, 16);
        t->n = atoi(tok[2]) > 0x800 ? 0x800 : atoi(tok[2]);
        t->v = n == 4 ? (int)strtoul(tok[3], NULL, 16) : 0;
    } else if (!strcmp(tok[0], "owatch") && n == 4) {   /* owatch POOL I OFF: write watch on object I of POOL + OFF */
        t->op = S_OWATCH;
        t->v = atoi(tok[1]);
        t->off = atoi(tok[2]);
        t->n = (int)strtoul(tok[3], NULL, 16);
    } else if (!strcmp(tok[0], "xwatch") && (n >= 2 && n <= 5)) {   /* xwatch ADDR [OFF [POOL I | eax]] */
        t->op = S_XWATCH;
        t->off = (int)strtoul(tok[1], NULL, 16);
        t->n = n >= 3 ? (int)strtoul(tok[2], NULL, 16) : -1;
        t->v = n == 5 ? atoi(tok[3]) * 1000 + atoi(tok[4]) : n == 4 && !strcmp(tok[3], "eax") ? -2 : -1;
    } else if (!strcmp(tok[0], "gwatch") && n == 3) {   /* gwatch PTR OFF: write watch on *PTR + OFF */
        t->op = S_GWATCH;
        t->off = (int)strtoul(tok[1], NULL, 16);
        t->n = (int)strtoul(tok[2], NULL, 16);
    }
    else if (!strcmp(tok[0], "log")) {
        t->op = S_LOG;
        for (k = 1; k < n; k++) {   /* (48 bytes: a short note, the rest cut) */
            int len = lstrlenA(t->text);
            if (len && len < (int)sizeof t->text - 1)
                t->text[len++] = ' ', t->text[len] = 0;
            lstrcpynA(t->text + len, tok[k], (int)sizeof t->text - len);
        }
    } else if (!strcmp(tok[0], "end"))
        t->op = S_END;
    else
        ok = 0;
    if (ok)
        n_steps++;
    else {
        seq_bad++;
        test_log("seq: bad step '%s' (%d words)", tok[0], n);
    }
}

/* a crash: the exception, where, the registers and the code addresses on the stack (game .text 0x401000-0x3111000,
 * our DLL's) - the log is flushed per line, so this is the last thing in it */
static LONG CALLBACK crash_handler(EXCEPTION_POINTERS *e)
{
    static int done;
    DWORD code = e->ExceptionRecord->ExceptionCode, *sp = (DWORD *)e->ContextRecord->Esp;
    int k, n = 0;
    if (code != EXCEPTION_ACCESS_VIOLATION && code != EXCEPTION_ILLEGAL_INSTRUCTION && code != EXCEPTION_INT_DIVIDE_BY_ZERO &&
        code != EXCEPTION_STACK_OVERFLOW && code != EXCEPTION_PRIV_INSTRUCTION)
        return EXCEPTION_CONTINUE_SEARCH;
    if (done++ < 3) {
        test_log("CRASH code %08lx at eip %08lx addr %08lx (%s) eax %08lx ebx %08lx ecx %08lx edx %08lx esi %08lx edi %08lx "
                 "ebp %08lx esp %08lx", code, e->ContextRecord->Eip,
                 (DWORD)e->ExceptionRecord->ExceptionInformation[1],
                 e->ExceptionRecord->ExceptionInformation[0] ? "write" : "read", e->ContextRecord->Eax,
                 e->ContextRecord->Ebx, e->ContextRecord->Ecx, e->ContextRecord->Edx, e->ContextRecord->Esi,
                 e->ContextRecord->Edi, e->ContextRecord->Ebp, e->ContextRecord->Esp);
        for (k = 0; k < 512 && n < 16; k++) {
            MEMORY_BASIC_INFORMATION mi;
            DWORD v;
            if (!VirtualQuery(sp + k, &mi, sizeof mi) || mi.State != MEM_COMMIT)
                break;
            v = sp[k];
            if ((v >= 0x00401000 && v < 0x03111000) ||
                (VirtualQuery((void *)v, &mi, sizeof mi) && mi.AllocationBase == (void *)GetModuleHandleA("dinput8.dll"))) {
                test_log("CRASH stack +%03x: %08lx", k * 4, v);
                n++;
            }
        }
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

void test_init(void)
{
    char path[MAX_PATH], line[256];
    FILE *f;

    game_dir(path, "axl_test.txt");
    f = fopen(path, "r");
    if (!f)
        return;
    test_on = 1;
    game_dir(path, "axl_lc2.log");
    logf = fopen(path, "w");
    AddVectoredExceptionHandler(0, crash_handler);   /* (last in line: the game's own handlers first) */
    while (fgets(line, sizeof line, f)) {
        char *tok[12];
        int n = 0;
        char *s = strtok(line, " \t\r\n");
        while (s && n < 12 && s[0] != '#') {
            tok[n++] = s;
            s = strtok(NULL, " \t\r\n");
        }
        if (n == 1 && !strcmp(tok[0], "seq")) {
            seq_on = 1;
            continue;
        }
        if (seq_on && n) {
            parse_step(tok, n);
            continue;
        }
        if (n >= 3 && !strcmp(tok[0], "key") && n_runs < MAX_RUNS) {
            key_run *r = &runs[n_runs++];
            int k;
            r->f1 = atoi(tok[1]);
            r->f2 = atoi(tok[2]);
            for (k = 3; k < n && r->n < 8; k++) {
                int d = vk_of(tok[k]);
                if (d >= 0)
                    r->vk[r->n++] = (unsigned char)d;
                else
                    test_log("unknown key %s", tok[k]);
            }
        } else if (n == 3 && !strcmp(tok[0], "pdump")) {
            pdump_f1 = atoi(tok[1]);
            pdump_f2 = atoi(tok[2]);
        } else if (n == 5 && !strcmp(tok[0], "projlook") && n_projlooks < 8) {
            int *e = projlooks[n_projlooks++];
            e[0] = atoi(tok[1]);
            e[1] = atoi(tok[2]);
            e[2] = atoi(tok[3]);
            e[3] = atoi(tok[4]);
        } else if (n == 5 && !strcmp(tok[0], "pfloat") && n_pfloats < 8) {
            pfloats[n_pfloats].f1 = atoi(tok[1]);
            pfloats[n_pfloats].f2 = atoi(tok[2]);
            pfloats[n_pfloats].off = strtoul(tok[3], NULL, 16);
            pfloats[n_pfloats++].v = (float)atof(tok[4]);
        } else if (n == 2 && !strcmp(tok[0], "weakid")) {
            extern int dna_weak_test;
            dna_weak_test = atoi(tok[1]);
        } else if (n == 1 && !strcmp(tok[0], "all_weapons")) {
            extern int axl_all_weapons;
            axl_all_weapons = 1;
        } else if (n == 2 && !strcmp(tok[0], "quit"))
            quit_f = atoi(tok[1]);
        else if (n == 4 && !strcmp(tok[0], "poke") && n_pokes < 32) {
            pokes[n_pokes].f = atoi(tok[1]);
            pokes[n_pokes].off = strtoul(tok[2], NULL, 16);
            pokes[n_pokes++].v = (unsigned char)strtoul(tok[3], NULL, 16);
        } else if (n == 3 && !strcmp(tok[0], "tdump")) {
            tdump_addr = strtoul(tok[1], NULL, 16);
            tdump_n = strtoul(tok[2], NULL, 0);
        } else if (n == 3 && !strcmp(tok[0], "xanim") && n_xanims < 8) {
            xanims[n_xanims][0] = atoi(tok[1]);
            xanims[n_xanims++][1] = atoi(tok[2]);
        } else if (n == 5 && (!strcmp(tok[0], "ppoke") || !strcmp(tok[0], "ppoke8")) && n_ppokes < 16) {
            ppokes[n_ppokes].bytes = tok[0][5] == '8' ? 1 : 2;
            ppokes[n_ppokes].f1 = atoi(tok[1]);
            ppokes[n_ppokes].f2 = atoi(tok[2]);
            ppokes[n_ppokes].off = strtoul(tok[3], NULL, 16);
            ppokes[n_ppokes++].v = (unsigned short)strtoul(tok[4], NULL, 16);
        } else if (n == 3 && !strcmp(tok[0], "memscan")) {
            int k;
            memscan_f = atoi(tok[1]);
            for (k = 0; tok[2][2 * k] && tok[2][2 * k + 1] && k < 64; k++) {
                char h[3] = {tok[2][2 * k], tok[2][2 * k + 1], 0};
                memscan_pat[k] = (unsigned char)strtoul(h, NULL, 16);
            }
            memscan_n = k;
        } else if (n == 2 && !strcmp(tok[0], "snap") && n_snaps < 64)
            snaps[n_snaps++] = atoi(tok[1]);
        else if (n == 2 && !strcmp(tok[0], "scan"))
            scan_f = atoi(tok[1]);
        else if (n == 2 && !strcmp(tok[0], "stdump") && n_stdumps < 16)
            stdumps[n_stdumps++] = atoi(tok[1]);
        else if (n == 2 && !strcmp(tok[0], "cdump") && n_cdumps < 8)
            cdumps[n_cdumps++] = atoi(tok[1]);
        else if (n == 2 && !strcmp(tok[0], "clock"))
            clock_wall = !strcmp(tok[1], "wall");
        else if (n == 1 && !strcmp(tok[0], "wait_go"))
            go_wait = go_used = 1;
        else if (n == 3 && !strcmp(tok[0], "talk")) {
            talk_f1 = atoi(tok[1]);
            talk_f2 = atoi(tok[2]);
        } else if (n == 2 && !strcmp(tok[0], "strscan")) {
            strscan_f = atoi(tok[1]);
        } else if (n == 3 && !strcmp(tok[0], "animlog")) {
            animlog_f1 = atoi(tok[1]);
            animlog_f2 = atoi(tok[2]);
        } else if (n == 2 && !strcmp(tok[0], "skipto"))
            skipto_f = atoi(tok[1]);
        else if (n == 1 && !strcmp(tok[0], "guard_all")) {
            extern int axl_guard_test;
            axl_guard_test = 1;
        } else if (n == 3 && !strcmp(tok[0], "etable")) {
            etable_f = atoi(tok[1]);
            etable_va = strtoul(tok[2], NULL, 16);
        } else if (n == 2 && !strcmp(tok[0], "hpwatch"))
            hpwatch_f = atoi(tok[1]);
        else if (n == 2 && !strcmp(tok[0], "objdump") && n_objdumps < 16)
            objdumps[n_objdumps++] = atoi(tok[1]);
        else if (n == 3 && !strcmp(tok[0], "scanrange")) {
            scan_lo = strtoul(tok[1], NULL, 16);
            scan_hi = strtoul(tok[2], NULL, 16);
        }
    }
    fclose(f);
    if (pdump_f1 >= 0) {
        game_dir(path, "axl_pdump.bin");
        dumpf = fopen(path, "wb");
    }
    test_log("harness on: %d key runs, pdump %d-%d, quit %d, clock %s", n_runs, pdump_f1, pdump_f2, quit_f,
             clock_wall ? "wall" : "game");
    {   /* (the run's start in Unix ms: lc2_frames.py lines the log up with the video) */
        FILETIME ft;
        unsigned long long t;
        GetSystemTimeAsFileTime(&ft);
        t = ((unsigned long long)ft.dwHighDateTime << 32 | ft.dwLowDateTime) / 10000 - 11644473600000ULL;
        test_log("harness start unix_ms %lu%03lu", (unsigned long)(t / 1000), (unsigned long)(t % 1000));
    }
    CreateThread(NULL, 0, ticker, NULL, 0, NULL);
}

/* a live boss (axl_objs.c) */
char *objs_boss(void);
void objs_types(void);

static int value_of(const char *name, int *ok)
{
    char *g = *(char **)0x041D8C8C, *st = g ? g + 0x70 : 0, *p = axl_p, *b;
    *ok = 1;
    if (!strncmp(name, "st_", 3) && st)
        return (unsigned char)st[strtoul(name + 3, NULL, 16)];
    if (!strncmp(name, "p_", 2) && p)
        return (unsigned char)p[strtoul(name + 2, NULL, 16)];
    if (!strcmp(name, "steps"))
        return test_frame - step_t0;
    if (!strcmp(name, "boss_alive"))
        return objs_boss() != 0;
    if (!strcmp(name, "boss_hp"))
        return (b = objs_boss()) ? O8B(b, 0x84) : -1;   /* (a byte, PS1 +0x5C; +0x85 is something else) */
    if (!strcmp(name, "hud")) {   /* the skill HUD's buttons drawn now, bit = slot (axl_hud.c) */
        extern int hud_shown;
        return hud_shown;
    }
    if (!strncmp(name, "cd_", 3)) {   /* a skill slot's frames until ready (axl_skills.c; 0 = SK_SHOT .. 3 = SK_GIGA) */
        int skill_left(int s);
        static const char *const slot[] = {"shot", "special", "dash", "giga"};
        int s;
        for (s = 0; s < 4; s++)
            if (!strcmp(name + 3, slot[s]))
                return skill_left(s);
    }
    if (st) {
        if (!strcmp(name, "stage"))
            return (unsigned char)st[0x48];
        if (!strcmp(name, "area"))
            return (unsigned char)st[0x49];
        if (!strcmp(name, "state"))
            return (unsigned char)st[0x3C];
        if (!strcmp(name, "checkpoint"))
            return (unsigned char)st[0x59];
        if (!strcmp(name, "lives"))
            return (unsigned char)st[0x75];
    }
    if (p) {
        if (!strcmp(name, "hp"))
            return (unsigned char)p[0x84];
        if (!strcmp(name, "anim"))
            return (unsigned char)p[0x2B];
        if (!strcmp(name, "free"))
            return free_n >= 30 && test_frame - last_pframe <= 2;   /* (X main runs: not dying, no load) */
        if (!strcmp(name, "px"))
            return (int)*(volatile float *)(p + 0x14);
        if (!strcmp(name, "py"))
            return (int)*(volatile float *)(p + 0x1C);
        if (!strcmp(name, "dna_shots")) {   /* live boss-attack projectiles (axl_proj.c) */
            int proj_live_dna(void);
            return proj_live_dna();
        }
        if (!strcmp(name, "dx"))   /* (from the last mark) */
            return (int)(*(volatile float *)(p + 0x14) - mark_x);
        if (!strcmp(name, "dy"))
            return (int)(*(volatile float *)(p + 0x1C) - mark_y);
        if (!strcmp(name, "rise"))
            return (int)(mark_y - mark_top);
        if (!strcmp(name, "shots")) {
            extern int proj_made;
            return proj_made - mark_shots;
        }
        if (!strcmp(name, "embedded")) {   /* the terrain box meets walls on both sides or floor and ceiling */
            int b = proj_terrain(p) & 0xF;
            return (b & 3) == 3 || (b & 0xC) == 0xC;
        }
    }
    *ok = 0;
    return 0;
}

static int cond_holds(const cond_t *c, int *v)
{
    int ok;
    *v = value_of(c->name, &ok);
    if (!ok)
        return 0;
    switch (c->op) {
    case 0: return *v == c->v;
    case 1: return *v != c->v;
    case 2: return *v < c->v;
    case 3: return *v > c->v;
    case 4: return *v <= c->v;
    default: return *v >= c->v;
    }
}

static void seq_end(void)
{
    test_log("e2e end pass %d fail %d", seq_pass, seq_fail);
    step_at = n_steps;
    seq_quit = test_frame + 60;
}

/* the bot (bot boss / right / left): with a live boss, toward it (within 48 px: face it), aimed at it; else walking
 * that way (boss: right). Shooting at the boss, or in bursts at an enemy within 120 px (Axl stands while the shot
 * is held); a jump (held 20 frames: a high jump, the hover after it) when Axl has not moved for 8 frames */
static void bot_keys(void)
{
    static int last_x, stuck, jump_t;
    char *b = bot == 1 ? objs_boss() : 0, *p = axl_p;
    float dx = bot == 3 ? -1000.0f : 1000.0f, dy = 0;
    if (!p || held_now)
        return;
    if (b) {
        dx = *(volatile float *)(b + 0x14) - *(volatile float *)(p + 0x14);
        dy = *(volatile float *)(b + 0x1C) - *(volatile float *)(p + 0x1C);
    }
    if (dx > 48 || (dx > 0 && test_frame % 40 < 2))
        down[0x27] = 1;
    else if (dx < -48 || (dx < 0 && test_frame % 40 < 2))
        down[0x25] = 1;
    if (dy < -40)
        down[0x26] = 1;
    else if (dy > 40)
        down[0x28] = 1;
    {   /* Axl stands still while the shot is held (8-way aim): shoot at the boss, or for a while at an enemy near */
        static int shoot_t;
        float x = *(volatile float *)(p + 0x14), y = *(volatile float *)(p + 0x1C);
        if (b || (!shoot_t && find_target(x, y, 120)))
            shoot_t = 30;
        if (shoot_t && shoot_t-- > 10)
            down['X'] = 1;
    }
    stuck = (int)*(volatile float *)(p + 0x14) == last_x && (dx > 48 || dx < -48) ? stuck + 1 : 0;
    last_x = (int)*(volatile float *)(p + 0x14);
    if (jump_t)
        jump_t--;
    if (!jump_t && (stuck > 8 || (b && dy < -60))) {
        jump_t = 40;
        stuck = 0;
    }
    if (jump_t > 20)
        down['Z'] = 1;
}

/* goto: a path through the stage's free space. Cells of 16 px around the start and the target (1024 px more on every
 * side); a cell is free when the player's own terrain box placed at its middle meets no terrain (the game's test,
 * proj_terrain: wall / floor / ceiling bits 0xF), tested when the breadth-first search first reaches it. */
#define CELL 16
#define GW 640
#define GH 256
static unsigned char g_seen[GW * GH];   /* 0 unknown, 1 free, 2 blocked */
static int g_prev[GW * GH], g_queue[GW * GH], path[GW * GH], n_path, path_at, gx0, gy0;

static int cell_free(int c)
{
    char *p = axl_p;
    float x = *(volatile float *)(p + 0x14), y = *(volatile float *)(p + 0x1C);
    int bits;
    if (g_seen[c])
        return g_seen[c] == 1;
    *(volatile float *)(p + 0x14) = (float)(gx0 + (c % GW) * CELL + CELL / 2);
    *(volatile float *)(p + 0x1C) = (float)(gy0 + (c / GW) * CELL + CELL / 2);
    bits = proj_terrain(p) & 0xF;
    *(volatile float *)(p + 0x14) = x;
    *(volatile float *)(p + 0x1C) = y;
    g_seen[c] = bits ? 2 : 1;
    return !bits;
}

static int clearance = 1;   /* find_path: a cell needs the one above free too (off: the retry) */

static int find_path(int tx, int ty)
{
    int sx = (int)*(volatile float *)(axl_p + 0x14), sy = (int)*(volatile float *)(axl_p + 0x1C);
    int x0 = (sx < tx ? sx : tx) - 1024, y0 = (sy < ty ? sy : ty) - 1024, s, goal, head = 0, tail = 0, k, tested = 0;
    static const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};
    gx0 = x0 < 0 ? 0 : x0 / CELL * CELL;
    gy0 = y0 < 0 ? 0 : y0 / CELL * CELL;
    if (tx < gx0 || ty < gy0 || sx < gx0 || sy < gy0 || (tx - gx0) / CELL >= GW || (ty - gy0) / CELL >= GH ||
        (sx - gx0) / CELL >= GW || (sy - gy0) / CELL >= GH)
        return -1;
    memset(g_seen, 0, sizeof g_seen);
    s = (sy - gy0) / CELL * GW + (sx - gx0) / CELL;
    goal = (ty - gy0) / CELL * GW + (tx - gx0) / CELL;
    g_seen[s] = g_seen[goal] = 1;   /* (standing on the floor: the start and the target count as free) */
    g_prev[s] = -1;
    g_queue[tail++] = s;
    g_seen[s] = 3;   /* 3: reached */
    while (head < tail) {
        int c = g_queue[head++];
        if (c == goal)
            break;
        for (k = 0; k < 4; k++) {
            int cx = c % GW + dx[k], cy = c / GW + dy[k], d = cy * GW + cx;
            if (cx < 0 || cy < 0 || cx >= GW || cy >= GH || g_seen[d] == 3 || g_seen[d] == 2)
                continue;
            if (g_seen[d] != 1) {
                tested++;
                if (!cell_free(d))
                    continue;
            }
            if (clearance && d != goal && cy > 0 && !cell_free(d - GW))   /* (room above too: a path hugging a slope's surface got
                                                               * him pushed into it - 2026-10-28) */
                continue;
            g_seen[d] = 3;
            g_prev[d] = c;
            g_queue[tail++] = d;
        }
    }
    if (g_seen[goal] != 3) {
        int nfree = 0, nblk = 0, i;
        for (i = 0; i < GW * GH; i++)
            nfree += g_seen[i] == 3, nblk += g_seen[i] == 2;
        test_log("e2e path: none from %d %d to %d %d: %d cells reached, %d blocked, grid at %d %d, start bits %x", sx, sy,
                 tx, ty, nfree, nblk, gx0, gy0, proj_terrain(axl_p));
        for (i = 0; i < GH; i++) {   /* the grid as tested: '#' blocked, '.' reached, ' ' not reached */
            char row[GW + 1];
            int j;
            for (j = 0; j < GW; j++)
                row[j] = g_seen[i * GW + j] == 2 ? '#' : g_seen[i * GW + j] == 3 ? '.' : ' ';
            row[GW] = 0;
            test_log("e2e grid %3d %s", i, row);
        }
        return -2;
    }
    for (n_path = 0, k = goal; k >= 0; k = g_prev[k])
        n_path++;
    for (s = n_path, k = goal; k >= 0; k = g_prev[k])
        path[--s] = k;
    path_at = 0;
    test_log("e2e path: %d cells from %d %d to %d %d (%d cells tested)", n_path, sx, sy, tx, ty, tested);
    return n_path;
}

/* one frame of the scenario (the game's thread, after the frame's keys are set) */
static void seq_frame(void)
{
    int guard, v;
    if (seq_bad) {   /* (steps that did not parse: the scenario is not what was written) */
        seq_fail += seq_bad;
        test_log("e2e FAIL %d scenario steps did not parse (the 'seq:' lines above)", seq_bad);
        seq_bad = 0;
        seq_end();
    }
    if (god && axl_p) {
        char *g = *(char **)0x041D8C8C;
        if ((unsigned char)axl_p[0x84] && (unsigned char)axl_p[0x84] < 32)
            axl_p[0x84] = 32;
        if (g && (unsigned char)g[0x70 + 0x75] < 2)
            g[0x70 + 0x75] = 2;
    }
    if (energy_full && axl_p) {   /* every weapon gauge above 0 back to full (an emptied one stays: it ends a change) */
        short max = *(volatile short *)(axl_p + 0x2CC);   /* (axl_dna.c ENERGY_MAX, ENERGY: P + 0x1A0 + 2k) */
        for (v = 1; v <= 8; v++)
            if (*(volatile short *)(axl_p + 0x1A0 + 2 * v) > 0)
                *(volatile short *)(axl_p + 0x1A0 + 2 * v) = max;
    }
    if (axl_p) {   /* the player dying (init 2): a failure of what was going on */
        static int was_dying;
        int dying = (unsigned char)axl_p[0x0C] == 2;
        if (dying && !was_dying) {
            seq_fail++;
            test_log("e2e FAIL died during %s at %d %d", cur_label, (int)*(volatile float *)(axl_p + 0x14),
                     (int)*(volatile float *)(axl_p + 0x1C));
        }
        was_dying = dying;
    }
    if (trace_n > 0 && axl_p) {   /* (the frame's start: what the last frame left) */
        trace_n--;
        test_log("e2e trace x %.1f y %.1f st %d sub %d anim %d b0 %02x b1 %02x f0 %02x vy %d grav %d", 
                 *(volatile float *)(axl_p + 0x14), *(volatile float *)(axl_p + 0x1C), (unsigned char)axl_p[0x0D],
                 (unsigned char)axl_p[0x0E], (unsigned char)axl_p[0x2B], (unsigned char)axl_p[0xB0],
                 (unsigned char)axl_p[0xB1], (unsigned char)axl_p[0xF0], *(volatile int *)(axl_p + 0x40),
                 *(volatile int *)(axl_p + 0x48));
    }
    if (axl_p && *(volatile float *)(axl_p + 0x1C) < mark_top)
        mark_top = *(volatile float *)(axl_p + 0x1C);
    if (bot)
        bot_keys();
    for (v = 0; v < 256; v++)
        if (seq_down[v] || hold_down[v])
            down[v] = 1;
    if (seq_quit >= 0 && test_frame >= seq_quit) {
        test_log("quit");
        if (dumpf)
            fclose(dumpf);
        TerminateProcess(GetCurrentProcess(), 0);
    }
    for (guard = 0; step_at < n_steps && guard < 16; guard++) {   /* instant steps run on in the same frame */
        step_t *t = &steps[step_at];
        int k, held;
        switch (t->op) {
        case S_WAIT:
            if (cond_holds(&t->c, &v)) {
                test_log("e2e wait %s %s %d met at frame %d (%s = %d)", t->c.name, OPS[t->c.op], t->c.v, test_frame,
                         t->c.name, v);
                break;
            }
            if (test_frame - step_t0 >= t->n) {
                seq_fail++;
                test_log("e2e FAIL wait %s %s %d: not within %d frames (%s = %d)", t->c.name, OPS[t->c.op], t->c.v,
                         t->n, t->c.name, v);
                seq_end();
            }
            return;
        case S_EXPECT:
            if (cond_holds(&t->c, &v)) {
                seq_pass++;
                test_log("e2e PASS %s", t->text);
            } else {
                seq_fail++;
                test_log("e2e FAIL %s (%s = %d, wanted %s %d)", t->text, t->c.name, v, OPS[t->c.op], t->c.v);
            }
            break;
        case S_PRESS:
            held = test_frame - step_t0;
            for (k = 0; k < t->v; k++)
                seq_down[t->vk[k]] = held < t->n;
            for (k = 0; k < t->v; k++)
                if (held < t->n)
                    down[t->vk[k]] = 1;
            if (held < t->n)
                return;
            break;
        case S_IDLE:
            if (test_frame - step_t0 < t->n)
                return;
            break;
        case S_TALK:   /* Z 4 frames in 30 (dialogue pages), not while the player is free, never in the Hunter base
                        * (stage 15: its menus save the game on Z) */
            if (t->v && cond_holds(&t->c, &v)) {
                test_log("e2e talk until %s %s %d met at frame %d", t->c.name, OPS[t->c.op], t->c.v, test_frame);
                break;
            }
            if (test_frame - step_t0 < t->n) {
                int k = test_frame - step_t0;
                if (t->off) {
                    if (k % 30 < 4 && value_of("stage", &v) != 15)
                        down['Z'] = 1;
                } else if (!(free_n >= 30 && test_frame - last_pframe <= 2) && k % 30 < 4 && value_of("stage", &v) != 15)
                    down[k / 30 & 1 ? 'Z' : 0x20] = 1;   /* Space skips a cutscene, Z pages a dialogue */
                return;
            }
            if (t->v) {
                seq_fail++;
                test_log("e2e FAIL talk until %s %s %d: not within %d frames", t->c.name, OPS[t->c.op], t->c.v, t->n);
                seq_end();
                return;
            }
            break;
        case S_HOLD:
            for (k = 0; k < t->v; k++)
                hold_down[t->vk[k]] = 1;
            for (k = 0; k < t->v; k++)
                down[t->vk[k]] = 1;
            break;
        case S_RELEASE:
            memset(hold_down, 0, sizeof hold_down);
            break;
        case S_SET: {
            char *g = *(char **)0x041D8C8C;
            if (g)
                g[0x70 + t->off] = (char)t->v;
            test_log("e2e set st+%x = %x", t->off, t->v);
            break;
        }
        case S_BSET: {
            char *b = objs_boss();
            if (b)
                *(volatile unsigned char *)(b + t->off) = (unsigned char)t->v;
            test_log("e2e bset %p +%x = %x%s", (void *)b, t->off, t->v, b ? "" : " (no boss)");
            break;
        }
        case S_PSET:
            if (axl_p)
                axl_p[t->off] = (char)t->v;
            test_log("e2e pset P+%x = %x", t->off, t->v);
            break;
        case S_SURVEY: {   /* the terrain bits of the player's terrain box at every 16 px cell, a row a line */
            char row[1024];
            int x, y, j;
            float px, py;
            if (!axl_p)
                return;
            px = *(volatile float *)(axl_p + 0x14);
            py = *(volatile float *)(axl_p + 0x1C);
            test_log("e2e survey %d %d %d %d cell %d", t->off, t->v, t->n, t->c.v, CELL);
            for (y = t->v; y < t->c.v; y += CELL) {
                for (j = 0, x = t->off; x < t->n && j < (int)sizeof row - 1; x += CELL, j++) {
                    int b;
                    *(volatile float *)(axl_p + 0x14) = (float)(x + CELL / 2);
                    *(volatile float *)(axl_p + 0x1C) = (float)(y + CELL / 2);
                    b = proj_terrain(axl_p) & 0xF;
                    row[j] = b ? "0123456789abcdef"[b] : '.';
                }
                row[j] = 0;
                test_log("e2e terrain %d %s", y, row);
            }
            *(volatile float *)(axl_p + 0x14) = px;
            *(volatile float *)(axl_p + 0x1C) = py;
            break;
        }
        case S_GOTO: {   /* along the path, t->n px a frame (the camera and the load triggers follow) */
            static float fx, fy;
            int r;
            if (!axl_p)
                return;
            if (step_new && !(free_n >= 30 && test_frame - last_pframe <= 2)) {   /* (dying, a respawn, a cutscene:
                * moving him then kept a respawn from landing - 2026-10-28; wait, at most 1800 frames) */
                if (test_frame - step_t0 > 1800) {
                    seq_fail++;
                    test_log("e2e FAIL goto %d %d: the player never free", t->off, t->v);
                    seq_end();
                }
                return;
            }
            if (step_new) {
                step_new = 0;
                clearance = 1;
                r = find_path(t->off, t->v);
                if (r == -2) {   /* (no path with room above: a narrow way - any free cells) */
                    clearance = 0;
                    r = find_path(t->off, t->v);
                }
                if (r < 0) {
                    seq_fail++;
                    test_log("e2e FAIL goto %d %d: %s", t->off, t->v, r == -1 ? "too far" : "no path");
                    seq_end();
                    return;
                }
                fx = *(volatile float *)(axl_p + 0x14);
                fy = *(volatile float *)(axl_p + 0x1C);
                if (n_path > 1)
                    path_at = 1;   /* (not to the middle of the cell he stands in: under a low overhang the 3 px up
                                    * to it crushed him, 2026-10-28) */
            }
            while (path_at < n_path) {
                float cx = (float)(gx0 + (path[path_at] % GW) * CELL + CELL / 2);
                float cy = (float)(gy0 + (path[path_at] / GW) * CELL + CELL / 2);
                float ex = cx - fx, ey = cy - fy, d = ex * ex + ey * ey;
                if (d <= (float)(t->n * t->n)) {
                    fx = cx;
                    fy = cy;
                    path_at++;
                    continue;
                }
                fx += ex > 0 ? (ex > t->n ? t->n : ex) : (ex < -t->n ? -t->n : ex);
                fy += ey > 0 ? (ey > t->n ? t->n : ey) : (ey < -t->n ? -t->n : ey);
                break;
            }
            {   /* never into the terrain (a crush kills even under god): 4 / 8 / 12 px higher, else stop - 2026-10-28 */
                float ox = *(volatile float *)(axl_p + 0x14), oy = *(volatile float *)(axl_p + 0x1C);
                int up, b;
                for (up = 0; up <= 12; up += 4) {
                    *(volatile float *)(axl_p + 0x14) = fx;
                    *(volatile float *)(axl_p + 0x1C) = fy - up;
                    b = proj_terrain(axl_p) & 0xF;
                    if ((b & 0xC) != 0xC && !((b & 4) && (b & 3)))   /* (bits: 8 floor, 4 ceiling, 1 / 2 walls; floor and ceiling, a ceiling and a wall:
                        * both wall bits alone are the feet on a slope's floor, not walls - 2026-10-28) (floor and ceiling, both
                        * walls, a ceiling and a wall: a crush to be - 0x0D killed him, 2026-10-28; a low corridor's
                        * ceiling alone is fine) */
                        break;
                }
                if (up > 12) {
                    *(volatile float *)(axl_p + 0x14) = ox;
                    *(volatile float *)(axl_p + 0x1C) = oy;
                    seq_fail++;
                    test_log("e2e FAIL goto %d %d: blocked at %d %d (the next step into the terrain)", t->off, t->v,
                             (int)ox, (int)oy);
                    seq_end();
                    return;
                }
                fy -= up;
            }
            *(volatile int *)(axl_p + 0x40) = 0;
            if (path_at < n_path)
                return;
            test_log("e2e went to %d %d", t->off, t->v);
            break;
        }
        case S_SHOW: {   /* 'e2e show NAME=value ...' */
            char out[200], name[16], *q = t->text;
            int len = 0, ok, i;
            while (*q && len < (int)sizeof out - 40) {
                for (i = 0; *q && *q != ' ' && i < (int)sizeof name - 1; i++)
                    name[i] = *q++;
                name[i] = 0;
                while (*q == ' ')
                    q++;
                len += wsprintfA(out + len, " %s=%d", name, value_of(name, &ok));
            }
            out[len] = 0;
            test_log("e2e show%s", out);
            break;
        }
        case S_TRACE:
            trace_n = t->n;
            break;
        case S_NOLIFT: {
            extern int dna_test_nolift, dna_test_nolook;   /* axl_dna.c */
            dna_test_nolift = t->v & 1;
            dna_test_nolook = t->v >> 1 & 1;
            break;
        }
        case S_MARK:
            if (axl_p) {
                extern int proj_made;
                mark_x = *(volatile float *)(axl_p + 0x14);
                mark_top = mark_y = *(volatile float *)(axl_p + 0x1C);
                mark_shots = proj_made;
            }
            break;
        case S_UNCHANGE: {
            void dna_test_end(void);   /* axl_dna.c */
            dna_test_end();
            break;
        }
        case S_GHOST:
            test_ghost = t->v;
            break;
        case S_ENERGY:
            energy_full = t->v;
            break;
        case S_GOD:
            god = t->v;
            break;
        case S_BOT:
            bot = t->v;
            break;
        case S_SNAP:
            test_log("snap %d", test_frame);
            break;
        case S_OBJS:
            objs_types();
            break;
        case S_GDUMP:
            g_dump((unsigned)t->off, t->n, t->v);
            break;
        case S_OWATCH: {
            char *objs_get(int pool, int i);
            char *o = objs_get(t->v, t->off);
            if (o)
                test_watch(o + t->n);
            else
                test_log("e2e owatch: no object %d of pool %d", t->off, t->v);
            break;
        }
        case S_XWATCH: {
            void test_xwatch(void *addr, int then, void *work, int use_eax);
            char *objs_get(int pool, int i);
            test_xwatch((void *)(unsigned)t->off, t->n, t->v >= 0 ? objs_get(t->v / 1000, t->v % 1000) : 0, t->v == -2);
            break;
        }
        case S_GWATCH:
            if (*(char **)(unsigned)t->off)
                test_watch(*(char **)(unsigned)t->off + t->n);
            break;
        case S_LOG:
            lstrcpynA(cur_label, t->text, sizeof cur_label);
            test_log("e2e log %s", t->text);
            break;
        case S_END:
            seq_end();
            return;
        }
        step_at++;
        step_t0 = test_frame;
        step_new = 1;
    }
    if (step_at >= n_steps && seq_quit < 0 && n_steps) {
        test_log("e2e: the scenario ran out without 'end'");
        seq_end();
    }
}

static void test_tick(int from_game)
{
    int i, k;
    if (from_game)
        goto frame;   /* the go file wait runs on the timer (the game may be paused without the focus) */
    if (go_used && !go_wait) {   /* the go file removed: armed again (the next one runs the script again) */
        char path[MAX_PATH];
        game_dir(path, "axl_go.txt");
        if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) {
            go_wait = 1;
            test_log("go file removed: armed again");
        }
    }
    if (go_wait == 1) {
        char path[MAX_PATH];
        game_dir(path, "axl_go.txt");
        if (GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES) {
            go_wait = 2;
            go_ticks = 0;
            EnumWindows(raise_game, 0);
            test_log("go file seen: waiting for the game to run");
        }
    }
    if (go_wait == 2) {   /* the window switch opened LC2's menu: Tab closes it (pressed every 2 s until play runs) */
        memset(down, 0, sizeof down);
        if (go_ticks++ % 120 < 4)
            down[0x09] = 1;
    }
frame:
    if (go_wait || from_game == clock_wall)
        return;
    test_frame++;
    if (test_frame % 600 == 0)
        test_log("clock: frame %d at real time %ld", test_frame, wall_frame);
    memset(down, 0, sizeof down);
    for (i = 0; i < n_runs; i++)
        if (test_frame >= runs[i].f1 && test_frame <= runs[i].f2)
            for (k = 0; k < runs[i].n; k++)
                down[runs[i].vk[k]] = 1;
    if (test_frame >= talk_f1 && test_frame <= talk_f2 && held_now && test_frame % 30 < 4) {
        int ok, stage = value_of("stage", &ok);
        if (stage != 15)   /* (never in the Hunter base: Z there saves the game) */
            down['Z'] = 1;
    }
    if (n_steps && !clock_wall)   /* (the scenario runs on the game's thread only: not with clock wall) */
        seq_frame();
    else if (n_steps && seq_quit < 0) {
        seq_fail++;
        test_log("e2e FAIL the scenario needs the game clock (clock wall: no keyboard-read frames)");
        seq_end();
        quit_f = seq_quit;   /* (seq_frame does not run: the absolute quit ends the process) */
    }
    for (i = 0; i < n_snaps; i++)
        if (snaps[i] == test_frame)
            test_log("snap %d", test_frame);
    for (k = 0; k < n_objdumps; k++)   /* (clock wall: also while the game pauses) */
        if (objdumps[k] >= 0 && test_frame >= objdumps[k]) {
            objdumps[k] = -1 - objdumps[k];   /* done (kept for a repeated go) */
            objs_dump();
        }
    if (etable_f >= 0 && test_frame >= etable_f)
        objs_set_table(etable_va);
    if (hpwatch_f >= 0 && test_frame >= hpwatch_f)
        objs_hp_watch();
    for (i = 0; i < n_stdumps; i++)   /* (clock wall: also while the game pauses) */
        if (stdumps[i] == test_frame)
            st_dump();
    for (i = 0; i < n_pokes; i++) {
        char *g = *(char **)0x041D8C8C;
        int later = 0;
        for (k = i + 1; k < n_pokes; k++)
            later |= pokes[k].off == pokes[i].off && test_frame >= pokes[k].f;
        if (g && test_frame >= pokes[i].f && !later)
            g[0x70 + pokes[i].off] = pokes[i].v;
    }
    if (quit_f >= 0 && test_frame >= quit_f) {
        test_log("quit");
        if (dumpf)
            fclose(dumpf);
        TerminateProcess(GetCurrentProcess(), 0);
    }
}

int test_key(int vk)
{
    vk &= 0xff;
    if (!(polled[vk >> 3] & (1 << (vk & 7)))) {
        polled[vk >> 3] |= 1 << (vk & 7);
        test_log("game polls key 0x%02x", vk);
    }
    return down[vk];
}

static int readable(const void *p, unsigned n)
{
    MEMORY_BASIC_INFORMATION mi;
    if ((unsigned)p < 0x10000 || !VirtualQuery(p, &mi, sizeof mi) || mi.State != MEM_COMMIT ||
        (mi.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
        return 0;
    return (char *)p + n <= (char *)mi.BaseAddress + mi.RegionSize;
}

static void scan_obj(const char *name, const char *o, int depth)
{
    unsigned k;
    char sub[64];
    if (!readable(o, 0x400))
        return;
    for (k = 0; k < 0x400; k += 4) {
        unsigned v = *(const unsigned *)(o + k);
        if (v >= scan_lo && v < scan_hi)
            test_log("scan %s+0x%x = %08x", name, k, v);
        else if (depth && v >= 0x01000000 && (v < 0x00400000 || v >= 0x04C00000)) {
            snprintf(sub, sizeof sub, "%s+%x>", name, k);
            scan_obj(sub, (const char *)v, depth - 1);
        }
    }
}

static void table_dump(void)
{
    char path[MAX_PATH], buf[512];
    unsigned i, **t = (unsigned **)tdump_addr;
    FILE *f;
    game_dir(path, "axl_tdump.bin");
    f = fopen(path, "wb");
    if (!f)
        return;
    fwrite(&tdump_addr, 4, 1, f);
    fwrite(&tdump_n, 4, 1, f);
    fwrite(t, 4, tdump_n, f);
    for (i = 0; i < tdump_n; i++) {
        if (readable(t[i], 512))
            memcpy(buf, t[i], 512);
        else
            memset(buf, 0xff, 512);
        fwrite(buf, 512, 1, f);
    }
    fclose(f);
    test_log("tdump %08x x %u written", tdump_addr, tdump_n);
}

static void memscan(void)
{
    MEMORY_BASIC_INFORMATION mi;
    char *a = (char *)0x10000;
    int hits = 0;
    while (a < (char *)0x7FFF0000 && VirtualQuery(a, &mi, sizeof mi)) {
        char *b = (char *)mi.BaseAddress, *e = b + mi.RegionSize, *q;
        if (mi.State == MEM_COMMIT && !(mi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) &&
            (mi.Protect & (PAGE_READWRITE | PAGE_READONLY | PAGE_EXECUTE_READWRITE | PAGE_WRITECOPY)))
            for (q = b; q + memscan_n <= e; q += 2)
                if (!memcmp(q, memscan_pat, memscan_n)) {
                    test_log("memscan hit %p (region %p size %lx)", q, b, (unsigned long)mi.RegionSize);
                    if (++hits > 40)
                        return;
                }
        a = e;
    }
    test_log("memscan done, %d hits", hits);
}

/* test (strscan): every distinct resource name "X6\\..." in readable memory (which textures the game has loaded) */
static void strscan(void)
{
    MEMORY_BASIC_INFORMATION mi;
    char *a = (char *)0x10000;
    static unsigned seen[1024];
    int n = 0;
    while (a < (char *)0x7FFF0000 && VirtualQuery(a, &mi, sizeof mi)) {
        char *b = (char *)mi.BaseAddress, *e = b + mi.RegionSize, *q;
        if (mi.State == MEM_COMMIT && !(mi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) &&
            (mi.Protect & (PAGE_READWRITE | PAGE_READONLY | PAGE_WRITECOPY)))
            for (q = b; q + 64 <= e; q++)
                if (q[0] == 'X' && q[1] == '6' && q[2] == '\\' && (q == b || !q[-1])) {
                    unsigned h = 5381;
                    int i, k;
                    for (i = 0; i < 64 && q[i] >= 0x20 && q[i] < 0x7f; i++)
                        h = h * 33 + (unsigned char)q[i];
                    if (i < 6 || strstr(q, "\\sound\\"))
                        continue;
                    for (k = 0; k < n && seen[k] != h; k++)
                        ;
                    if (k < n || n >= 1024)
                        continue;
                    seen[n++] = h;
                    test_log("str %p %.*s", q, i, q);
                }
        a = e;
    }
    test_log("strscan done, %d names", n);
}

void test_player(char *self, int phase)
{
    int k;
    if (go_wait == 2 && (*(unsigned char *)(*(char **)(self + 0xD4) + 0x1EC) ||
                         *(unsigned char *)(*(char **)(self + 0xD4) + 0x0C) != 1))
        return;           /* a cutscene / READY still holds the player */
    if (go_wait == 2) {   /* the game pauses without the focus: the script starts with its first frame of play */
        go_wait = 0;
        step_t0 -= test_frame;
        test_frame = 0;
        for (k = 0; k < n_objdumps; k++)
            if (objdumps[k] < 0)
                objdumps[k] = -1 - objdumps[k];
        down[0x09] = 0;
        test_log("go");
    }
    if (go_wait)
        return;
    if (phase == 0 && test_frame >= animlog_f1 && test_frame <= animlog_f2) {
        char *p = *(char **)(self + 0xD4);
        test_log("anim: init %d state %d anim %d cursor %p frame %d tex %d pat %d pal %d layer %d", p[0x0C], p[0x0D],
                 (unsigned char)p[0x2B], *(void **)(p + 0x50), (unsigned char)p[0x66], *(int *)(p + 0x54),
                 *(int *)(p + 0x58), *(unsigned short *)(p + 0x5E), p[0x2A]);
    }
    if (phase == 0) {
        char *p = *(char **)(self + 0xD4);
        held_now = p[0x1EC] || p[0x0C] != 1;
        last_pframe = test_frame;
        free_n = held_now ? 0 : free_n + 1;
        if (skipto_f >= 0 && free_n >= 60 && test_frame < skipto_f) {   /* (once, before that frame) */
            test_log("skipto: frame %d is now %d", test_frame, skipto_f);
            step_t0 += skipto_f - test_frame;   /* (a scenario step's frames go on from where they were) */
            test_frame = skipto_f;
            skipto_f = -1;
            talk_f2 = -1;
        }
    }
    if (strscan_f >= 0 && test_frame >= strscan_f) {
        strscan_f = -1;
        strscan();
    }
    if (memscan_f >= 0 && test_frame >= memscan_f) {
        memscan_f = -1;
        memscan();
    }
    if (phase == 0)
        for (k = 0; k < n_pfloats; k++)
            if (test_frame >= pfloats[k].f1 && test_frame <= pfloats[k].f2)
                *(float *)(*(char **)(self + 0xD4) + pfloats[k].off) = pfloats[k].v;
    if (phase == 0)
        for (k = 0; k < n_ppokes; k++)
            if (test_frame >= ppokes[k].f1 && test_frame <= ppokes[k].f2) {
                char *a = *(char **)(self + 0xD4) + ppokes[k].off;
                if (ppokes[k].bytes == 1)
                    *(unsigned char *)a = (unsigned char)ppokes[k].v;
                else
                    *(unsigned short *)a = ppokes[k].v;
            }
    if (phase == 1)
        for (k = 0; k < n_xanims; k++)
            if (xanims[k][0] >= 0 && test_frame >= xanims[k][0]) {
                X_ANIM_BANK(*(char **)(self + 0xD4), xanims[k][1]);
                test_log("xanim %d", xanims[k][1]);
                xanims[k][0] = -1;
            }
    if (phase == 0)
        for (k = 0; k < n_cdumps; k++)
            if (cdumps[k] >= 0 && test_frame >= cdumps[k]) {
                cdumps[k] = -1;
                clut_dump();
            }
    if (tdump_n) {
        table_dump();
        tdump_n = 0;
    }
    if (scan_f >= 0 && test_frame >= scan_f) {
        scan_f = -1;
        test_log("scan this %p vtable %p P %p", self, *(void **)self, *(void **)(self + 0xD4));
        scan_obj("T", self, 1);
        scan_obj("P", *(char **)(self + 0xD4), 1);
    }
    {
    unsigned hdr[5];
    char *p;
    if (!dumpf || test_frame < pdump_f1 || test_frame > pdump_f2)
        return;
    p = *(char **)(self + 0xD4);
    hdr[0] = 0x504C5841;   /* 'AXLP' */
    hdr[1] = (unsigned)test_frame;
    hdr[2] = (unsigned)self;
    hdr[3] = (unsigned)p;
    hdr[4] = (unsigned)phase;
    fwrite(hdr, sizeof hdr, 1, dumpf);
    fwrite(self, DUMP_THIS, 1, dumpf);
    fwrite(p, DUMP_P, 1, dumpf);
    }
}

/* hardware write watch (test): DR0 on addr (4 bytes); every writer (EIP and caller) is logged once */
static int xwatch_then = -1;   /* xwatch ADDR OFF: on the first break, a write watch on ecx + OFF */
static int xwatch_eax;         /* xwatch ADDR OFF eax: eax instead of ecx */
static DWORD xwatch_work;      /* xwatch ADDR OFF POOL I: only the task whose work (+0xC0) is that object */
static DWORD watch_eips[64];
static int n_watch_eips;

static LONG CALLBACK watch_handler(EXCEPTION_POINTERS *e)
{
    int i;
    DWORD eip;
    if (e->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP)
        return EXCEPTION_CONTINUE_SEARCH;
    if (e->ContextRecord->Dr6 & 2) {   /* DR1: the execution break (test_xwatch): this = ecx, logged up to 8 times */
        static DWORD seen[8];
        static int n_seen;
        DWORD ecx = xwatch_eax ? e->ContextRecord->Eax : e->ContextRecord->Ecx;
        if (xwatch_work && *(DWORD *)(ecx + 0xC0) != xwatch_work) {   /* (0xC0: FUN_02545c30, these tasks) */
            e->ContextRecord->Dr6 = 0;
            e->ContextRecord->EFlags |= 0x10000;
            return EXCEPTION_CONTINUE_EXECUTION;
        }
        for (i = 0; i < n_seen && seen[i] != ecx; i++)
            ;
        if (i == n_seen && n_seen < 8) {
            seen[n_seen++] = ecx;
            test_log("xwatch: %08lx ran with %s %08lx (eax %08lx ecx %08lx edx %08lx ebx %08lx esi %08lx edi %08lx)",
                     e->ContextRecord->Eip, xwatch_eax ? "eax" : "ecx", ecx, e->ContextRecord->Eax, e->ContextRecord->Ecx,
                     e->ContextRecord->Edx, e->ContextRecord->Ebx, e->ContextRecord->Esi, e->ContextRecord->Edi);
            if (n_seen == 1 && xwatch_then >= 0) {   /* then a 2-byte write watch on ecx + off (DR0) */
                e->ContextRecord->Dr0 = ecx + (DWORD)xwatch_then;
                e->ContextRecord->Dr7 = (e->ContextRecord->Dr7 & ~0xF0003u) | 1 | (1u << 16) | (1u << 18);
                test_log("xwatch: watching %08lx", ecx + (DWORD)xwatch_then);
            }
        }
        e->ContextRecord->Dr6 = 0;
        e->ContextRecord->EFlags |= 0x10000;   /* RF: run the instruction this time */
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    eip = e->ContextRecord->Eip;
    {   /* (a writer = its eip and the first code address on the stack: a shared setter's callers apart) */
        DWORD *sp = (DWORD *)e->ContextRecord->Esp, ra[8], key;
        int k, n = 0;
        for (k = 0; k < 256 && n < 8; k++)
            if (sp[k] >= 0x00401000 && sp[k] < 0x03111000)
                ra[n++] = sp[k];
        while (n < 8)
            ra[n++] = 0;
        key = eip ^ (ra[0] * 31u);
        for (i = 0; i < n_watch_eips && watch_eips[i] != key; i++)
            ;
        if (i < n_watch_eips || n_watch_eips >= 64) {
            e->ContextRecord->Dr6 = 0;
            return EXCEPTION_CONTINUE_EXECUTION;
        }
        watch_eips[n_watch_eips++] = key;
        test_log("watch: write by eip %08lx, stack code %08lx %08lx %08lx %08lx %08lx %08lx %08lx %08lx", eip,
                 ra[0], ra[1], ra[2], ra[3], ra[4], ra[5], ra[6], ra[7]);
    }
    e->ContextRecord->Dr6 = 0;
    return EXCEPTION_CONTINUE_EXECUTION;
}

/* execution break (DR1) at code addr: the ecx of each call (up to 8 different) logged */
void test_xwatch(void *addr, int then, void *work, int use_eax)
{
    xwatch_eax = use_eax;
    xwatch_then = then;
    xwatch_work = (DWORD)work;
    CONTEXT c;
    test_watch(0);   /* (the handler) */
    memset(&c, 0, sizeof c);
    c.ContextFlags = CONTEXT_DEBUG_REGISTERS;
    if (!GetThreadContext(GetCurrentThread(), &c))
        return;
    c.Dr1 = (DWORD)addr;
    c.Dr7 = (c.Dr7 & ~(0xFu << 20)) | 4;   /* L1, execute, length 1 */
    if (!SetThreadContext(GetCurrentThread(), &c))
        test_log("xwatch: SetThreadContext failed %lu", GetLastError());
    else
        test_log("xwatch %p", addr);
}

void test_watch(void *addr)
{
    static int handler;
    CONTEXT c;
    if (!handler) {
        AddVectoredExceptionHandler(1, watch_handler);
        handler = 1;
    }
    if (!addr)
        return;
    memset(&c, 0, sizeof c);
    c.ContextFlags = CONTEXT_DEBUG_REGISTERS;
    if (GetThreadContext(GetCurrentThread(), &c) == 0)
        memset(&c, 0, sizeof c);
    c.ContextFlags = CONTEXT_DEBUG_REGISTERS;
    c.Dr0 = (DWORD)addr;
    c.Dr7 = (c.Dr7 & ~0xF0003u) | 1 | (1u << 16) | (3u << 18);   /* L0, write, 4 bytes (DR1 kept) */
    if (!SetThreadContext(GetCurrentThread(), &c))
        test_log("watch: SetThreadContext failed %lu", GetLastError());
    else
        test_log("watch %p", addr);
}
#endif   /* AXL_RELEASE */
