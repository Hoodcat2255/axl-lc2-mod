/* test harness (axl_test.c): active only when axl_test.txt is next to RXC2.exe */
extern int test_on, test_frame, test_ghost;   /* test_ghost: seq 'ghost on' (axl_hook.c x_box_hook) */
void test_init(void);
void test_input(void);                   /* the game's keyboard read: one frame of the script */
void test_log(const char *fmt, ...);
int test_key(int vk);                    /* GetAsyncKeyState: the script holds this key now */
void test_player(char *self, int phase); /* X main: 0 before the state code, 1 after */
void test_watch(void *addr);
int test_user_drives(void);
void test_projlook(int *tex, int *pat, int *anim);              /* hardware write watch on 4 bytes, writers logged */
