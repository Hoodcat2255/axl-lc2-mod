/* axl_data.bin next to dinput8.dll: the game-derived tables (axl_data.h), read once before any hook goes in */
#include <windows.h>
#include <string.h>

#include "axl_data.h"

axl_data_t axl_d;
HINSTANCE axl_module;   /* this DLL (DllMain) */

static unsigned fnv1a(const unsigned char *p, unsigned n)
{
    unsigned h = 2166136261u;
    while (n--)
        h = (h ^ *p++) * 16777619u;
    return h;
}

int axl_data_load(void)
{
    char path[MAX_PATH];
    HANDLE f;
    DWORD n = GetModuleFileNameA(axl_module, path, MAX_PATH), got = 0;
    char *s = path + n;
    int ok, a;

    if (n == 0 || n >= MAX_PATH)
        return 0;
    while (s > path && s[-1] != '\\' && s[-1] != '/')
        s--;
    if ((s - path) + sizeof AXL_DATA_FILE > MAX_PATH)
        return 0;
    memcpy(s, AXL_DATA_FILE, sizeof AXL_DATA_FILE);
    f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE)
        return 0;
    ok = GetFileSize(f, NULL) == sizeof axl_d && ReadFile(f, &axl_d, sizeof axl_d, &got, NULL) && got == sizeof axl_d;
    CloseHandle(f);
    ok = ok && axl_d.magic == AXL_DATA_MAGIC && axl_d.version == AXL_DATA_VERSION && axl_d.size == sizeof axl_d &&
         axl_d.hash == fnv1a((const unsigned char *)&axl_d + 16, sizeof axl_d - 16) &&
         axl_d.n_table > 0 && axl_d.n_table <= AXL_TABLE_MAX && axl_d.n_ents <= AXL_ENTS_MAX &&
         axl_d.n_body_pals <= BODY_PALS_MAX && axl_d.spike_seq_n <= SPIKE_SEQ_MAX &&
         axl_d.n_flame_frames <= FLAME_FRAMES_MAX;
    for (a = 0; ok && a < axl_d.n_table; a++)
        ok = axl_d.start[a] < axl_d.n_ents;   /* (-1: the game's own script) */
    if (!ok)
        ZeroMemory(&axl_d, sizeof axl_d);
    return ok;
}
