#include "test.h"

#include "ui/theme.h"

#include <string.h>

// theme_write produces a file that reads back to the same theme.
static void test_theme_roundtrip(void) {
    Theme t = theme_fallback();
    t.dark = true;
    t.colors.accent = (Color){ 0x12, 0x34, 0x56, 0xFF };
    t.colors.hover = (Color){ 0xAA, 0xBB, 0xCC, 0x20 };
    t.board.style = BOARD_STYLE_WOOD;
    t.stones.style = STONE_STYLE_INK;
    t.shape.radius = 10.5f;
    string_free(&t.name);
    t.name = string_from(SV("My \"theme\""));
    String text = theme_write(&t, SV("test"));
    KvFile kv = { 0 };
    kv_parse(&kv, string_sv(&text));
    Theme u = theme_fallback();
    theme_apply_kv(&u, &kv, SV("roundtrip"));
    CHECK(u.dark);
    CHECK_SV(string_sv(&u.name), SV("My \"theme\""));
    CHECK(u.colors.accent.r == 0x12 && u.colors.accent.g == 0x34 && u.colors.accent.b == 0x56 && u.colors.accent.a == 0xFF);
    CHECK(u.colors.hover.a == 0x20);
    CHECK_EQ(u.board.style, BOARD_STYLE_WOOD);
    CHECK_EQ(u.stones.style, STONE_STYLE_INK);
    CHECK_NEAR(u.shape.radius, 10.5f, 1e-4f);
    CHECK(memcmp(&u.colors, &t.colors, sizeof(ThemeColors)) == 0);
    kv_free(&kv);
    string_free(&text);
    theme_free(&t);
    theme_free(&u);
}

void register_theme_tests(void) { test_register("theme/roundtrip", test_theme_roundtrip); }
