/* Axl for Mega Man X Legacy Collection 2 (RXC2.exe, Steam build 21253892): dinput8.dll proxy.
 *
 * The game imports DINPUT8.dll for DirectInput8Create only. This DLL forwards that call to the system dinput8 and,
 * on the first call (the code section is decrypted by then: SteamStub runs before the game's own code), installs:
 *   - new game armour (0x023BA3DC `push 1; mov ecx,esi; call s8_9a`): 0 instead of 1, so the intro stage is played
 *     by normal X (= Axl) instead of the Falcon Armor (user request 2026-10-26).
 *   - X main (FUN_02539bf0 = PS1 FUN_80034fbc): the call of vtable slot 0x54 (PS1 FUN_8003f048, before the state
 *     code) and of FUN_0253cac0 (PS1 FUN_8003ce38, after the buster) go through axl_frame / axl_post (axl_x.c);
 *   - X main's last step, the hurt box (tail jump to FUN_025447a0 = PS1 FUN_8003c3c8), goes through axl_box.
 *   - the object maker FUN_023b1160 (entry, trampoline): every object made is reported to axl_spawned (axl_x.c).
 *     axl_frame puts Axl's animation table in the player's anim component as PS1 axl_frame did (X_TABLE).
 * The DirectInput object is wrapped for the test harness (axl_test.c) only.
 * Addresses: re/lc2/README.md section 5, re/lc2/codemap.md. Proton: WINEDLLOVERRIDES="dinput8=n,b" %command%
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>

#include "axl_data.h"      /* the game-derived tables (axl_data.bin) */
#include "axl_lc2.h"
#include "axl_proj.h"
#include "axl_test.h"

#define NEW_GAME_ARMOR 0x023BA3DCu   /* push 1 (Falcon) */
#define X_MAIN_SLOT54  0x02539E42u   /* mov eax,[esi]; mov ecx,esi; call [eax+0x54] */
#define X_MAIN_POST    0x02539EA7u   /* call 0x0253CAC0 */
#define X_CHARGE       0x0253CAC0u
#define X_MAIN_BOX     0x02539EB5u   /* tail jmp 0x025447A0 = hurt box (PS1 FUN_8003c3c8) */
#define X_BOX          0x025447A0u
#define MAKE_OBJECT    0x023B1160u   /* make object (kind, id, sub, owner, slot): entry hooked via a trampoline */
#define BOSS_HP_SET_CALL 0x023CFCE0u   /* the damage routine (PS1 FUN_800315cc): call FUN_020a6490 (the new hit points) */
#define KBD_READ       0x01E74FA0u   /* keyboard read (thiscall, 1 arg; GetAsyncKeyState x 256 when the DirectInput
                                        keyboard is null, as under Proton): once per input update = the harness's frame */

void *axl_table[AXL_TABLE_MAX];
extern HINSTANCE axl_module;   /* axl_data.c */

/* X's table is filled by the game's start-up code: copied on the first player frame */
void axl_table_init(void)
{
    int a;
    for (a = 0; a < AXL_N_TABLE; a++)
        axl_table[a] = axl_start[a] >= 0 ? (void *)&axl_ents[axl_start[a]] : X_TABLE[a < N_X_ANIMS ? a : 5];
}
int axl_all_weapons, axl_all_parts, axl_all_slots;

/* axl_options.txt next to RXC2.exe, one word per line (testing): all_weapons = every boss weapon (DNA Change) for
 * Axl, nothing saved (axl_x.c dna_owned); all_parts = every enhancement part owned (the parts word st + 0xAC bits
 * 2-25 set each frame: a save made then keeps them); all_slots = 4 parts + 1 limited at every hunter rank (the stage
 * select's tables 0x031DC730 slots per rank 04 03 02 02 01 00 00 00, 0x031DC738 limited allowed 01 01 01 00 ..) */
#ifndef AXL_RELEASE
static void read_options(void)
{
    char path[MAX_PATH], line[64], *s;
    FILE *f;

    GetModuleFileNameA(NULL, path, MAX_PATH);
    s = strrchr(path, '\\');
    if (!s)
        return;
    lstrcpyA(s + 1, "axl_options.txt");
    f = fopen(path, "r");
    if (!f)
        return;
    while (fgets(line, sizeof line, f))
        if (!strncmp(line, "all_weapons", 11))
            axl_all_weapons = 1;
        else if (!strncmp(line, "all_parts", 9))
            axl_all_parts = 1;
        else if (!strncmp(line, "all_slots", 9))
            axl_all_slots = 1;
    fclose(f);
    test_log("options: all_weapons %d all_parts %d all_slots %d", axl_all_weapons, axl_all_parts, axl_all_slots);
    if (axl_all_slots) {
        static const unsigned char want[16] = {4, 3, 2, 2, 1, 0, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0};
        unsigned char *t = (unsigned char *)0x031DC730;
        DWORD old;
        if (!memcmp(t, want, 16) && VirtualProtect(t, 16, PAGE_READWRITE, &old)) {
            memset(t, 4, 8);
            memset(t + 8, 1, 8);
            VirtualProtect(t, 16, old, &old);
        } else
            test_log("all_slots: rank tables not as expected");
    }
}
#endif

static HMODULE real_dinput8;
static LONG installed;

typedef HRESULT(WINAPI *di8create_t)(HINSTANCE, DWORD, REFIID, LPVOID *, LPUNKNOWN);

/* X main, before the state code: this -> vtable slot 0x54 */
static void __fastcall x_frame_hook(char *self)
{
    x_fn orig = (x_fn)(*(void ***)self)[0x54 / 4];
    dna_unlook();   /* the draw-time look back first (also before the test harness pokes the player) */
    if (test_on) {
        static int stack_logged;
        if (!stack_logged++ ) {
            test_log("X main on thread %lu", GetCurrentThreadId());   /* test: the return addresses above us (who runs the player's update) */
            unsigned *sp = (unsigned *)__builtin_frame_address(0);
            int i;
            for (i = 0; i < 256; i++)
                if (sp[i] >= 0x00401000u && sp[i] < 0x03111000u)
                    test_log("stack +%03x: %08x", i * 4, sp[i]);
        }
        test_player(self, 0);
        {   /* test: the player while his hit points are 0 (death) */
            static int n_dead;
            char *p = *(char **)(self + 0xD4);
            if (p && !p[0x84] && n_dead < 150) {
                n_dead++;
                test_log("dead: init %d state %d sub %d anim %d frame %d tex %d vy %x", (unsigned char)p[0x0C],
                         (unsigned char)p[0x0D], (unsigned char)p[0x0E], (unsigned char)p[0x2B], (unsigned char)p[0x66],
                         *(int *)(p + 0x54), *(int *)(p + 0x40));
            }
        }
    }
    axl_frame(self, orig);
}

static void __fastcall x_post_hook(char *self)
{
    char *p = *(char **)(self + 0xD4);
    ((x_fn)X_CHARGE)(self);   /* (FUN_0253cac0 is the weapon palette switch, not the charge: Axl's no-charge is
                               * axl_x.c shoot_before, the shot button never held for X) */
    axl_post(self);
    if (test_on)
        test_player(self, 1);
}

/* FUN_023b1160(kind, id, sub, owner P, slot): thiscall, 5 stack args, returns the new object (work, like P) */
typedef char *(__fastcall *make_t)(void *, void *, int, int, int, char *, int);

static make_t make_orig;   /* trampoline: the 6 moved bytes (mov ecx, [0x041D8CD0]) + jmp MAKE_OBJECT + 6 */

static char *__fastcall spawn_hook(void *ecx, void *edx, int kind, int id, int sub, char *owner, int slot)
{
    char *w = make_orig(ecx, edx, kind, id, sub, owner, slot);
    if (w)
        axl_spawned(w, kind, id, sub, owner);
    return w;
}

#ifndef AXL_RELEASE   /* (the test harness's keyboard read hook, scripted keys: not in the release build at all) */
typedef int(__fastcall *kbd_t)(void *, void *, void *);
static kbd_t kbd_orig;   /* trampoline: sub esp, 0x100 + jmp KBD_READ + 6 */
static int __fastcall kbd_hook(void *self, void *edx, void *out)
{
    static int logged;
    if (!logged++)
        test_log("keyboard read on thread %lu", GetCurrentThreadId());   /* (the X main's: x_frame_hook logs its own) */
    test_input();
    return kbd_orig(self, edx, out);
}
#endif

static void __fastcall x_box_hook(char *self)
{
    ((x_fn)X_BOX)(self);
    axl_box(self);
    if (test_on && test_ghost)   /* test (ghost on): no hurt box, enemies pass through; the terrain box stays */
        *(void **)(*(char **)(self + 0xD4) + 0x7C) = 0;
}

/* test only: the pool allocator FUN_023b16b0(pool, a, b) (thiscall, ret 0xC), to find which objects appear */
#define POOL_ALLOC 0x023B16B0u
typedef char *(__fastcall *alloc_t)(void *, void *, int, int, int);
static alloc_t alloc_orig;
static char *__fastcall alloc_hook(void *ecx, void *edx, int pool, int a, int b)
{
    char *o = alloc_orig(ecx, edx, pool, a, b);
    if (o)
        axl_allocated(o, pool, a, b);
    return o;
}

/* the solids' terrain list (FUN_023d1320, thiscall on the map, no stack args): built every frame after the objects
 * moved - where a press would meet a changed boss is found there first (axl_dna.c dna_press_hold, 2026-11-02) */
#define SOLID_LIST 0x023D1320u
typedef void(__fastcall *solid_list_t)(void *, void *);
static solid_list_t solid_list_orig;
static void __fastcall solid_list_hook(void *map, void *edx)
{
    dna_press_hold();
    solid_list_orig(map, edx);
}

/* X's muzzle flash's task (0x02536540: its state code, then its draw 0x02535B50): the call of the state code goes
 * through flash_state_hook, so a flash is turned to the aim before it is drawn - its first update, which sets its
 * anim, draws it too (axl_x.c axl_flash_frame; user request 2026-11-04) */
#define FLASH_STATE_CALL 0x02536543u   /* call 0x02536470 */
#define FLASH_STATE 0x02536470u
static void __fastcall flash_state_hook(char *task)
{
    ((x_fn)FLASH_STATE)(task);
    axl_flash_frame();
}

/* the Metal Shark stage's press: at 0x024FC6AC (mov ecx, edi; mov eax, [esp + 0x14]) eax becomes its task, whose
 * offset (+ 0x6A) sets screen layer 1 right after - a stub calls dna_press_task(eax) there to read it (user request
 * 2026-11-02) */
#define PRESS_SITE 0x024FC6ACu
/* and coming down (state 4, FUN_024fcf00) its contact flag call: dna_press_contact (a thud on a changed boss's top) */
#define PRESS_CONTACT_SITE 0x024FCF1Bu

/* trampolines and stubs: written while read / write, then read / execute (never writable and executable at once) */
void *code_alloc(unsigned long n)
{
    return VirtualAlloc(NULL, n, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
}

void code_seal(void *t, unsigned long n)
{
    DWORD old;
    VirtualProtect(t, n, PAGE_EXECUTE_READ, &old);
    FlushInstructionCache(GetCurrentProcess(), t, n);
}

static int patch(DWORD at, const void *bytes, DWORD len)
{
    DWORD old;
    if (!VirtualProtect((void *)at, len, PAGE_EXECUTE_READWRITE, &old))
        return 0;
    memcpy((void *)at, bytes, len);
    VirtualProtect((void *)at, len, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void *)at, len);
    return 1;
}

static void put_rel(unsigned char *p, DWORD next, void *to)
{
    *(LONG *)p = (LONG)((DWORD)to - next);
}

static int expect(DWORD at, const unsigned char *bytes, DWORD len)
{
    return memcmp((void *)at, bytes, len) == 0;
}

/* Heatnix's fire immunity (user request 2026-11-12: changed into Heatnix, no harm from fire - the boss Heatnix's
 * Magma Stream, the lava of his and the gate's stages, his stage's fire traps). The player's hurt FUN_023ce940
 * (thiscall, the attacking object as its one stack arg, 1 = hurt) asks dna_fire_immune first; the death tiles'
 * kill (X main's call of FUN_023ca4f0 at 0x0253AB30, when the death flag P+0xB9 is set) asks dna_lava_immune; the
 * two tile tests that set that flag (FUN_023cd540 / FUN_023cf800, cdecl (obj, code[, side]): codes 0x3E / 0x3F)
 * report the code (dna_death_tile) */
#define HURT_PLAYER     0x023CE940u   /* push ecx; push ebx; push esi; push edi; mov esi, ecx (6 bytes moved) */
#define DEATH_TILE_CALL 0x0253AB30u   /* call FUN_023ca4f0 */
#define DEATH_TILE_FN   0x023CA4F0u
#define TILE_TEST_A     0x023CD540u   /* movzx eax, byte [esp+8]; add eax, -0x38 (8 bytes moved) */
#define TILE_TEST_B     0x023CF800u   /* cmp dword [esp+0xc], 0; movzx eax, byte [esp+8] (10 bytes moved) */
typedef int(__fastcall *hurt_t)(void *, void *, char *);
typedef int(__cdecl *tile_t)(void *, int, int);
static hurt_t hurt_orig;
static tile_t tile_a_orig, tile_b_orig;

static int __fastcall hurt_hook(void *ecx, void *edx, char *att)
{
    if (dna_fire_immune(att))
        return 0;
    return hurt_orig(ecx, edx, att);
}

/* the damage itself (FUN_023ce200, thiscall, stack (attacker, victim), ret 8): FUN_023ce940 calls it, and 7 attackers
 * call it directly - his stage's red floor flames hurt through it (play log 2026-11-14: blocked at FUN_023ce940, still
 * 8 damage) */
#define HURT_APPLY 0x023CE200u   /* sub esp, 8; push ebp; mov ebp, [esp+0x14] (8 bytes moved) */
typedef int(__fastcall *apply_t)(void *, void *, char *, char *);
static apply_t apply_orig;

static int __fastcall apply_hook(void *ecx, void *edx, char *att, char *vic)
{
    int r;
    unsigned char *box;
    if (vic == axl_p && dna_fire_immune(att))
        return 0;
    r = apply_orig(ecx, edx, att, vic);
    if (r && test_on && att && vic == axl_p) {   /* (what really hurt him: the immunity probe, 2026-11-14) */
        box = *(unsigned char **)(att + 0x78);
        test_log("hurt BY type %02x sub %02x box %dx%d at %.0f %.0f", *(unsigned char *)(att + 9),
                 *(unsigned char *)(att + 10), box ? box[6] : 0, box ? box[7] : 0, *(float *)(att + 0x14),
                 *(float *)(att + 0x1C));
    }
    return r;
}

static void __fastcall death_tile_hook(void *ecx)
{
    if (dna_lava_immune())
        return;
    ((void(__fastcall *)(void *))DEATH_TILE_FN)(ecx);
}

static int __cdecl tile_a_hook(void *o, int code, int side)
{
    dna_death_tile(o, code & 0xFF);
    return tile_a_orig(o, code, side);
}

static int __cdecl tile_b_hook(void *o, int code, int side)
{
    dna_death_tile(o, code & 0xFF);
    return tile_b_orig(o, code, side);
}

/* an entry hook: the first n bytes (position independent) moved to a trampoline that jumps back after them */
static void *entry_hook(DWORD at, int n, void *to)
{
    unsigned char *t = code_alloc(32), b[16];
    int i;
    if (!t)
        return 0;
    memcpy(t, (void *)at, n);
    t[n] = 0xE9;
    put_rel(t + n + 1, (DWORD)t + n + 5, (void *)(at + n));
    code_seal(t, 32);
    b[0] = 0xE9;
    put_rel(b + 1, at + 5, to);
    for (i = 5; i < n; i++)
        b[i] = 0x90;
    patch(at, b, n);
    return t;
}

static void fire_install(void)
{
    static const unsigned char hurt_entry[6] = {0x51, 0x53, 0x56, 0x57, 0x8B, 0xF1};
    static const unsigned char apply_entry[8] = {0x83, 0xEC, 0x08, 0x55, 0x8B, 0x6C, 0x24, 0x14};
    static const unsigned char tile_a[8] = {0x0F, 0xB6, 0x44, 0x24, 0x08, 0x83, 0xC0, 0xC8};
    static const unsigned char tile_b[10] = {0x83, 0x7C, 0x24, 0x0C, 0x00, 0x0F, 0xB6, 0x44, 0x24, 0x08};
    unsigned char b[5] = {0xE8};
    if (!expect(HURT_PLAYER, hurt_entry, 6) || !expect(HURT_APPLY, apply_entry, 8) || !expect(TILE_TEST_A, tile_a, 8) || !expect(TILE_TEST_B, tile_b, 10) ||
        *(unsigned char *)DEATH_TILE_CALL != 0xE8 ||
        DEATH_TILE_CALL + 5 + *(LONG *)(DEATH_TILE_CALL + 1) != DEATH_TILE_FN) {
        test_log("fire immunity: sites not as expected, not installed");
        return;
    }
    hurt_orig = (hurt_t)entry_hook(HURT_PLAYER, 6, (void *)hurt_hook);
    apply_orig = (apply_t)entry_hook(HURT_APPLY, 8, (void *)apply_hook);
    tile_a_orig = (tile_t)entry_hook(TILE_TEST_A, 8, (void *)tile_a_hook);
    tile_b_orig = (tile_t)entry_hook(TILE_TEST_B, 10, (void *)tile_b_hook);
    put_rel(b + 1, DEATH_TILE_CALL + 5, (void *)death_tile_hook);
    patch(DEATH_TILE_CALL, b, 5);
}


/* every sound request (FUN_023be620, thiscall on the sound manager, 3 stack args: kind, id, obj): the ones of our
 * projectiles' muzzle flashes are dropped (they cut Axl's change voice: user report 2026-10-26); logged when testing */
typedef void(__fastcall *se_play_t)(void *mgr, void *edx, int kind, int id, char *obj);
static se_play_t se_orig;

static void *se_mgr;
static DWORD siren_t, warn_until;

/* X8's warning siren, again once the last one ended, while the game's WARNING requests come (at them and every X
 * frame) */
void warn_tick(void)
{
    static int on;
    DWORD now = GetTickCount();
    if (!se_mgr || (LONG)(warn_until - now) <= 0) {
        if (on) {   /* (the siren ended with the game's warning: no tail) */
            axl_sound_stop(SND_SIREN);
            on = 0;
        }
        return;
    }
    on = 1;
    if (now - siren_t >= SIREN_MS) {
        siren_t = now;
        se_orig(se_mgr, 0, 1, SND_SIREN, 0);
    }
}

static void __fastcall se_hook(void *mgr, void *edx, int kind, int id, char *obj)
{
    int drop = objs_muted(obj);   /* X's muzzle flash of one of our projectiles (axl_objs.c) */
    /* the rope's grab (kind 0 id 38) while changed: X takes the rope every frame a changed boss overlaps it and Axl's
     * code lets go at once (axl_x.c) - it rattled (user report 2026-10-29) */
    if (kind == 0 && id == 38 && obj && obj == axl_p && dna_boss)
        drop = 1;
    /* X's beam-in (his request 13, X8's appear sound): Axl's "ikuzo" with it, request 32 (user request 2026-10-29) */
    if (kind == 1 && id == 13 && is_normal_x(0))
        se_orig(mgr, edx, 1, SND_IKUZO, obj);
    /* READY (common request 10, at the stage start and every respawn) and WARNING (common 19, every 61 frames, 8
     * times, before a boss) for normal X: Axl's "Ready" voice / X8's siren back to back (warn_tick) in place of the
     * game's (X's "Ready" voice / a BLANK wave), his requests 28 / 33 (user requests 2026-10-29; lc2_build READY_SOUND /
     * SIREN_SOUND) - no object: X's update first runs at the beam-in, 47 frames after READY */
    if (kind == 0 && (id == 10 || id == 19) && is_normal_x(0)) {
        drop = 1;
        se_mgr = mgr;
        if (id == 10)
            se_orig(mgr, edx, 1, SND_READY, 0);
        else {
            warn_until = GetTickCount() + 1500;   /* (the game's next request, 1.02 s later, keeps it on) */
            warn_tick();
        }
    }
    /* pl00 requests 9 / 12 (X's, never heard from Axl in the logs: his charge shot's, a guess) carry Heatnix's "Ha!" /
     * "Teya!" now (user request 2026-11-12, lc2_build HEAT_VOICES): the game's own (the Overdrive's charged buster,
     * maybe) play Axl's shot instead */
    if (kind == 1 && (id == 9 || id == 12) && !axl_se_own)
        id = 8;
    if (test_on)
        test_log("se kind %d id %d obj %p%s%s", kind, id, obj, obj && obj == axl_p ? " (player)" : "",
                 drop ? " dropped" : "");
    if (!drop)
        se_orig(mgr, edx, kind, id, obj);
}

static void se_log_install(void)
{
    static const unsigned char entry[5] = {0x53, 0x8B, 0x5C, 0x24, 0x0C};   /* push ebx; mov ebx, [esp+0xC] */
    unsigned char *f = (unsigned char *)0x023BE620, *t;
    DWORD old;

    if (memcmp(f, entry, 5))
        return;
    t = code_alloc(16);
    if (!t)
        return;
    memcpy(t, entry, 5);
    t[5] = 0xE9;
    *(int *)(t + 6) = (int)(f + 5) - (int)(t + 10);
    code_seal(t, 16);
    se_orig = (se_play_t)t;
    if (!VirtualProtect(f, 5, PAGE_EXECUTE_READWRITE, &old))
        return;
    f[0] = 0xE9;
    *(int *)(f + 1) = (int)(void *)se_hook - (int)(f + 5);
    VirtualProtect(f, 5, old, &old);
    if (test_on)
        test_log("sound requests hooked");
}

static void install(void)
{
    static const unsigned char slot54[7] = {0x8B, 0x06, 0x8B, 0xCE, 0xFF, 0x50, 0x54};
    static const unsigned char post[5] = {0xE8, 0x14, 0x2C, 0x00, 0x00};
    static const unsigned char new_armor[5] = {0x6A, 0x01, 0x8B, 0xCE, 0xE8};
    static const unsigned char make_entry[6] = {0x8B, 0x0D, 0xD0, 0x8C, 0x1D, 0x04};
    static const unsigned char box[5] = {0xE9, 0xE6, 0xA8, 0x00, 0x00};
    unsigned char b[12];

    if (InterlockedExchange(&installed, 1))
        return;
    test_init();
    /* the game-derived tables (axl_data.bin next to this DLL): without them nothing is patched */
    if (!axl_data_load()) {
#ifndef AXL_RELEASE
        OutputDebugStringA("axl_lc2: " AXL_DATA_FILE " missing or not for this build, hooks not installed\n");
        test_log(AXL_DATA_FILE " missing or not for this build, hooks not installed");
#endif
        return;
    }
#ifndef AXL_RELEASE
    read_options();   /* (the release build: no options - the testing ones, all_weapons) */
#endif
    /* only patch the exact build this was made for: every site is checked before anything is written */
    if (!expect(X_MAIN_SLOT54, slot54, 7) || !expect(X_MAIN_POST, post, 5) ||
        !expect(NEW_GAME_ARMOR, new_armor, 5) || !expect(MAKE_OBJECT, make_entry, 6) ||
        !expect(X_MAIN_BOX, box, 5)) {
#ifndef AXL_RELEASE
        OutputDebugStringA("axl_lc2: RXC2.exe build mismatch, hooks not installed\n");
        test_log("build mismatch, hooks not installed");
#endif
        return;
    }
    b[0] = 0x6A;
    b[1] = 0x00;
    patch(NEW_GAME_ARMOR, b, 2);
    b[0] = 0x8B;
    b[1] = 0xCE;   /* mov ecx, esi */
    b[2] = 0xE8;
    put_rel(b + 3, X_MAIN_SLOT54 + 7, (void *)x_frame_hook);
    patch(X_MAIN_SLOT54, b, 7);
    b[0] = 0xE8;
    put_rel(b + 1, X_MAIN_POST + 5, (void *)x_post_hook);
    patch(X_MAIN_POST, b, 5);
    {
        unsigned char *t = code_alloc(16);
        if (t) {
            memcpy(t, make_entry, 6);
            t[6] = 0xE9;
            put_rel(t + 7, (DWORD)t + 11, (void *)(MAKE_OBJECT + 6));
            code_seal(t, 16);
            make_orig = (make_t)t;
            b[0] = 0xE9;
            put_rel(b + 1, MAKE_OBJECT + 5, (void *)spawn_hook);
            b[5] = 0x90;
            patch(MAKE_OBJECT, b, 6);
        }
    }
    b[0] = 0xE9;   /* still a tail jump: ecx = this */
    put_rel(b + 1, X_MAIN_BOX + 5, (void *)x_box_hook);
    patch(X_MAIN_BOX, b, 5);
    se_log_install();
    fire_install();
    if (!menu_install())
        test_log("menu: unit move pass not as expected");
    if (!proj_install())
        test_log("projectiles: shot state table not as expected");
    if (!dna_hud_install())
        test_log("hud: weapon number site not as expected");
    if (!dna_giga_row_install())
        test_log("pause menu: giga row site not as expected");
    if (!dna_rematch_install())
        test_log("rematch: stage select / boss room sites not as expected");
    if (!dna_player_select_install())
        test_log("player select: cursor site not as expected");
    {   /* the pool allocator: every object made is remembered (axl_objs.c: targets, enemy bullets) */
        static const unsigned char alloc_entry[5] = {0x83, 0xEC, 0x60, 0x53, 0x56};
        unsigned char *t = code_alloc(16);
        if (t && expect(POOL_ALLOC, alloc_entry, 5)) {
            memcpy(t, alloc_entry, 5);
            t[5] = 0xE9;
            put_rel(t + 6, (DWORD)t + 10, (void *)(POOL_ALLOC + 5));
            code_seal(t, 16);
            alloc_orig = (alloc_t)t;
            b[0] = 0xE9;
            put_rel(b + 1, POOL_ALLOC + 5, (void *)alloc_hook);
            patch(POOL_ALLOC, b, 5);
        }
    }
    {   /* the press's offset before layer 1 is set: the 6 bytes (mov ecx, edi; mov eax, [esp + 0x14] = the task),
         * pushad, push eax, call, add esp 4, popad, jmp back */
        static const unsigned char site[6] = {0x8B, 0xCF, 0x8B, 0x44, 0x24, 0x14};
        unsigned char *t = code_alloc(32);
        if (t && expect(PRESS_SITE, site, 6)) {
            memcpy(t, site, 6);
            t[6] = 0x60;
            t[7] = 0x50;
            t[8] = 0xE8;
            put_rel(t + 9, (DWORD)t + 13, (void *)dna_press_task);
            t[13] = 0x83;
            t[14] = 0xC4;
            t[15] = 0x04;
            t[16] = 0x61;
            t[17] = 0xE9;
            put_rel(t + 18, (DWORD)t + 22, (void *)(PRESS_SITE + 6));
            code_seal(t, 32);
            b[0] = 0xE9;
            put_rel(b + 1, PRESS_SITE + 5, t);
            b[5] = 0x90;
            patch(PRESS_SITE, b, 6);
        } else
            test_log("press: layer 1 site not as expected");
    }
    {   /* the press coming down: its contact flag call (call FUN_023b6cd0) */
        unsigned char call[5];
        call[0] = 0xE8;
        put_rel(call + 1, PRESS_CONTACT_SITE + 5, (void *)0x023B6CD0);
        if (expect(PRESS_CONTACT_SITE, call, 5)) {
            put_rel(call + 1, PRESS_CONTACT_SITE + 5, (void *)dna_press_contact);
            patch(PRESS_CONTACT_SITE, call, 5);
        } else
            test_log("press: contact call site not as expected");
    }
    {   /* the damage routine's hit points write (axl_x.c axl_boss_hp_set: the copy shot's chance) */
        unsigned char call[5];
        call[0] = 0xE8;
        put_rel(call + 1, BOSS_HP_SET_CALL + 5, (void *)0x020A6490);
        if (expect(BOSS_HP_SET_CALL, call, 5)) {
            put_rel(call + 1, BOSS_HP_SET_CALL + 5, (void *)axl_boss_hp_set);
            patch(BOSS_HP_SET_CALL, call, 5);
        } else
            test_log("boss hold: hit points call site not as expected");
    }
    {   /* the muzzle flash's state code call */
        unsigned char call[5];
        call[0] = 0xE8;
        put_rel(call + 1, FLASH_STATE_CALL + 5, (void *)FLASH_STATE);
        if (expect(FLASH_STATE_CALL, call, 5)) {
            put_rel(call + 1, FLASH_STATE_CALL + 5, (void *)flash_state_hook);
            patch(FLASH_STATE_CALL, call, 5);
        } else
            test_log("muzzle flash: state call site not as expected");
    }
    {   /* the solids' terrain list: where a press meets a changed boss */
        static const unsigned char list_entry[5] = {0x51, 0x53, 0x55, 0x33, 0xC0};
        unsigned char *t = code_alloc(16);
        if (t && expect(SOLID_LIST, list_entry, 5)) {
            memcpy(t, list_entry, 5);
            t[5] = 0xE9;
            put_rel(t + 6, (DWORD)t + 10, (void *)(SOLID_LIST + 5));
            code_seal(t, 16);
            solid_list_orig = (solid_list_t)t;
            b[0] = 0xE9;
            put_rel(b + 1, SOLID_LIST + 5, (void *)solid_list_hook);
            patch(SOLID_LIST, b, 5);
        } else
            test_log("solids: terrain list site not as expected");
    }
#ifndef AXL_RELEASE
    if (test_on) {   /* the harness's frame clock: the game's keyboard read */
        static const unsigned char kbd_entry[6] = {0x81, 0xEC, 0x00, 0x01, 0x00, 0x00};
        unsigned char *t = code_alloc(16);
        if (t && expect(KBD_READ, kbd_entry, 6)) {
            memcpy(t, kbd_entry, 6);
            t[6] = 0xE9;
            put_rel(t + 7, (DWORD)t + 11, (void *)(KBD_READ + 6));
            code_seal(t, 16);
            kbd_orig = (kbd_t)t;
            b[0] = 0xE9;
            put_rel(b + 1, KBD_READ + 5, (void *)kbd_hook);
            b[5] = 0x90;
            patch(KBD_READ, b, 6);
        } else {
            extern int test_clock_wall;
            test_clock_wall = 1;   /* (no game frames: the frames from real time) */
            test_log("keyboard read not as expected: frames from real time (clock wall)");
        }
    }
#endif
#ifndef AXL_RELEASE   /* (the release build: no messages at all, user request 2026-11-14 "로그 남기는 부분은 전부 제거") */
    OutputDebugStringA("axl_lc2: hooks installed\n");
    test_log("hooks installed");
#endif
}

/* ---- DirectInput wrapping (test harness: scripted keys; not in the release build) ---- */
#ifndef AXL_RELEASE
typedef HRESULT(WINAPI *create_device_t)(void *, REFGUID, void **, void *);
typedef HRESULT(WINAPI *get_state_t)(void *, DWORD, void *);
static create_device_t real_create_device;
static get_state_t real_get_state;

static HRESULT WINAPI get_state_hook(void *dev, DWORD cb, void *data)
{
    return real_get_state(dev, cb, data);
}

/* the game's keyboard: GetAsyncKeyState (import slot 0x03111350) */
#define IAT_GET_ASYNC_KEY_STATE ((void **)0x03111350)
typedef SHORT(WINAPI *gaks_t)(int);
static gaks_t real_gaks;

static SHORT WINAPI gaks_hook(int vk)
{
    SHORT r = test_user_drives() ? real_gaks(vk) : 0;   /* (scripted runs: only the script's keys - the user types
                                                         * in other windows meanwhile, 2026-11-03) */
    if (test_key(vk))
        r |= (SHORT)0x8001;
    return r;
}

/* the game asks whether its window is the foreground one (once a frame, 0x01E5E000: GetForegroundWindow() == its
 * window + 0x12C, else LC2 pauses with its menu): a scripted run says yes, so the game runs on behind other windows
 * while the user works on the desktop (user request 2026-11-03) */
#define IAT_GET_FOREGROUND_WINDOW ((void **)0x03111320)
typedef HWND(WINAPI *gfw_t)(void);
static gfw_t real_gfw;
static HWND game_hwnd;

static BOOL CALLBACK find_game_window(HWND h, LPARAM unused)
{
    DWORD pid;
    (void)unused;
    GetWindowThreadProcessId(h, &pid);
    if (pid != GetCurrentProcessId() || !IsWindowVisible(h) || GetWindow(h, GW_OWNER))
        return TRUE;
    game_hwnd = h;
    return FALSE;
}

/* and the window's messages: losing the focus (another window clicked) the game stopped reading its input (2026-11-03,
 * the run stood at frame 1) - the deactivations reach it as activations, the focus loss not at all */
static WNDPROC real_wndproc;

static LRESULT CALLBACK game_wndproc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (!test_user_drives()) {
        if (m == WM_ACTIVATEAPP && !w)
            w = TRUE;
        else if (m == WM_ACTIVATE && LOWORD(w) == WA_INACTIVE)
            w = WA_ACTIVE;
        else if (m == WM_NCACTIVATE && !w)
            w = TRUE;
        else if (m == WM_KILLFOCUS)
            return 0;
    }
    return CallWindowProcW(real_wndproc, h, m, w, l);
}

static HWND WINAPI gfw_hook(void)
{
    if (test_user_drives())
        return real_gfw();
    if (!game_hwnd || !IsWindow(game_hwnd)) {
        game_hwnd = 0;
        EnumWindows(find_game_window, 0);
        if (game_hwnd) {
            real_wndproc = (WNDPROC)SetWindowLongPtrW(game_hwnd, GWLP_WNDPROC, (LONG_PTR)game_wndproc);
            test_log("test focus: window %p held active", (void *)game_hwnd);
        }
    }
    return game_hwnd ? game_hwnd : real_gfw();
}

static void patch_slot(void **vt, int slot, void *to, void **old)
{
    DWORD prot;
    if (vt[slot] == to)
        return;
    if (!VirtualProtect(&vt[slot], sizeof(void *), PAGE_READWRITE, &prot))
        return;
    *old = vt[slot];
    vt[slot] = to;
    VirtualProtect(&vt[slot], sizeof(void *), prot, &prot);
}

static HRESULT WINAPI create_device_hook(void *di, REFGUID guid, void **out, void *outer)
{
    HRESULT r = real_create_device(di, guid, out, outer);
    if (SUCCEEDED(r) && out && *out) {
        test_log("CreateDevice %08lx -> %p", guid ? guid->Data1 : 0, *out);
        patch_slot(*(void ***)*out, 9, (void *)get_state_hook, (void **)&real_get_state);
    }
    return r;
}

#endif

static FARPROC real_proc(const char *name)
{
    if (!real_dinput8) {
        char path[MAX_PATH];
        UINT n = GetSystemDirectoryA(path, MAX_PATH);
        if (n == 0 || n > MAX_PATH - 14)
            return NULL;
        lstrcatA(path, "\\dinput8.dll");
        real_dinput8 = LoadLibraryA(path);
        if (!real_dinput8)
            return NULL;
    }
    return GetProcAddress(real_dinput8, name);
}

HRESULT WINAPI DirectInput8Create(HINSTANCE inst, DWORD ver, REFIID riid, LPVOID *out, LPUNKNOWN outer)
{
    HRESULT r;
    di8create_t f;
    install();
    f = (di8create_t)real_proc("DirectInput8Create");
    if (!f)
        return E_FAIL;
    r = f(inst, ver, riid, out, outer);
#ifndef AXL_RELEASE
    if (test_on && SUCCEEDED(r) && out && *out) {
        patch_slot(*(void ***)*out, 3, (void *)create_device_hook, (void **)&real_create_device);
        if (!real_gaks)
            patch_slot(IAT_GET_ASYNC_KEY_STATE, 0, (void *)gaks_hook, (void **)&real_gaks);
        if (!real_gfw)
            patch_slot(IAT_GET_FOREGROUND_WINDOW, 0, (void *)gfw_hook, (void **)&real_gfw);
    }
#endif
    return r;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        axl_module = inst;   /* (axl_data.c: axl_data.bin is next to this DLL) */
        DisableThreadLibraryCalls(inst);
    }
    return TRUE;
}
