#include "test.h"

#include <string.h>

typedef struct TestCase {
    const char *name;
    TestFn fn;
} TestCase;

static TestCase g_tests[256];
static int g_test_count;
static int g_failures;
static const char *g_current;

void test_register(const char *name, TestFn fn) {
    if (g_test_count < (int)countof(g_tests)) g_tests[g_test_count++] = (TestCase){ name, fn };
}

void test_fail(const char *file, int line, const char *message) {
    g_failures++;
    fprintf(stderr, "  FAIL %s (%s:%d): %s\n", g_current, file, line, message);
}

void register_str_tests(void);
void register_board_tests(void);
void register_sgf_tests(void);
void register_analysis_tests(void);
void register_data_tests(void);
void register_installer_tests(void);
void register_theme_tests(void);
void register_layout_tests(void);
void register_interop_tests(void);
void register_text_edit_tests(void);
void register_image_tests(void);
void register_board_reader_tests(void);
void register_stone_anim_tests(void);
void register_markup_tests(void);
void register_update_tests(void);
void register_estimate_tests(void);
void register_profile_tests(void);
void register_style_tests(void);
void register_platform_tests(void);
int platform_child_main(const char *mode);

int main(int argc, char **argv) {
    if (argc > 2 && strcmp(argv[1], "--platform-child") == 0) return platform_child_main(argv[2]);   // tests/test_platform.c
    register_str_tests();
    register_board_tests();
    register_sgf_tests();
    register_analysis_tests();
    register_data_tests();
    register_installer_tests();
    register_theme_tests();
    register_layout_tests();
    register_interop_tests();
    register_text_edit_tests();
    register_image_tests();
    register_board_reader_tests();
    register_stone_anim_tests();
    register_markup_tests();
    register_update_tests();
    register_estimate_tests();
    register_profile_tests();
    register_style_tests();
    register_platform_tests();
    const char *filter = argc > 1 ? argv[1] : NULL;
    int run = 0;
    for (int i = 0; i < g_test_count; ++i) {
        if (filter && !strstr(g_tests[i].name, filter)) continue;
        g_current = g_tests[i].name;
        int before = g_failures;
        g_tests[i].fn();
        run++;
        printf("%s %s\n", g_failures == before ? "ok  " : "FAIL", g_tests[i].name);
    }
    printf("\n%d tests, %d failures\n", run, g_failures);
    return g_failures ? 1 : 0;
}
