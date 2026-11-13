/* test harness (axl_test.c): active only when axl_test.txt is next to RXC2.exe */
extern int test_frame, test_ghost;   /* test_ghost: seq 'ghost on' (axl_hook.c x_box_hook) */
#ifdef AXL_RELEASE
/* the release build: no harness, no log - test_on a constant 0 and test_log nothing, so every test-only branch and log
 * text drops out of the DLL (user request 2026-11-14: "모드 관련 로그 남기거나 하는 부분은 전부 제거하자") */
#define test_on 0
#define test_log(...) ((void)0)
#else
extern int test_on;
void test_log(const char *fmt, ...);
#endif
void test_init(void);
void test_input(void);                   /* the game's keyboard read: one frame of the script */
int test_key(int vk);                    /* GetAsyncKeyState: the script holds this key now */
void test_player(char *self, int phase); /* X main: 0 before the state code, 1 after */
void test_watch(void *addr);
int test_user_drives(void);
void test_projlook(int *tex, int *pat, int *anim);              /* hardware write watch on 4 bytes, writers logged */
