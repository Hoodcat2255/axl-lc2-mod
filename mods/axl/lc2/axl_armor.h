/* Normal X test, from the PC analog of the PS1 armour -> DAT choice (FUN_023a8380, re/lc2/codemap.md):
 *   st = (*(char **)0x041D8C8C + 0x68) + 8      ; FUN_023adc00 -> FUN_023ca130 -> FUN_023c7ee0
 *   st is laid out as the PS1 save area from 0x800CCE94: st[0x74] = PS1 0x800CCF08 character (!= 0 -> pl01 Zero),
 *   st[0x9a] = PS1 0x800CCF2E armour: 1 Falcon -> pl03, 2 -> pl04, 3 -> pl05, other -> pl02 (normal X = Axl).
 */
#define GAME_STATE_PTR ((char **)0x041D8C8Cu)
#define STATE_OFS      0x70
#define ZERO_OFS       0x74
#define ARMOR_OFS      0x9A

static int is_normal_x(char *self)
{
    char *g = *GAME_STATE_PTR, *st;
    char armor;
    (void)self;
    if (!g)
        return 0;
    st = g + STATE_OFS;
    armor = st[ARMOR_OFS];
    return st[ZERO_OFS] == 0 && armor != 1 && armor != 2 && armor != 3;
}
