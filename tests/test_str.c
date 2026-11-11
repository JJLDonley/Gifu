#include "test.h"
#include "base/kv.h"

#include "base/arena.h"
#include "base/sexpr.h"
#include "platform/text_encoding.h"
#include "base/vec.h"

static void test_sv_basics(void) {
    StringView s = SV("  hello world \r\n");
    CHECK_SV(sv_trim(s), SV("hello world"));
    StringView rest = SV("a,b,,c");
    CHECK_SV(sv_chop_char(&rest, ','), SV("a"));
    CHECK_SV(sv_chop_char(&rest, ','), SV("b"));
    CHECK_SV(sv_chop_char(&rest, ','), SV(""));
    CHECK_SV(sv_chop_char(&rest, ','), SV("c"));
    CHECK(rest.len == 0);
    CHECK(sv_starts_with(SV("kata-analyze"), SV("kata")));
    CHECK(sv_ends_with(SV("net.bin.gz"), SV(".bin.gz")));
    CHECK_EQ(sv_find(SV("abcabd"), SV("abd")), 3);
    CHECK(sv_eq_nocase(SV("PASS"), SV("pass")));
}

static void test_sv_parse(void) {
    i64 i = 0;
    f64 f = 0;
    CHECK(sv_parse_i64(SV("-42"), &i) && i == -42);
    CHECK(!sv_parse_i64(SV("4x"), &i));
    CHECK(sv_parse_f64(SV("0.5125"), &f));
    CHECK_NEAR(f, 0.5125, 1e-9);
    CHECK(!sv_parse_f64(SV("abc"), &f));
}

static void test_tokens(void) {
    StringView rest = SV("  info move D4\tvisits 12 ");
    CHECK_SV(sv_chop_token(&rest), SV("info"));
    CHECK_SV(sv_chop_token(&rest), SV("move"));
    CHECK_SV(sv_chop_token(&rest), SV("D4"));
    CHECK_SV(sv_chop_token(&rest), SV("visits"));
    CHECK_SV(sv_chop_token(&rest), SV("12"));
    CHECK(sv_chop_token(&rest).len == 0);
}

static void test_string_builder(void) {
    String s = { 0 };
    string_push(&s, SV("B["));
    string_pushf(&s, "%c%c", 'p', 'd');
    string_push_char(&s, ']');
    CHECK_SV(string_sv(&s), SV("B[pd]"));
    CHECK(s.data[s.len] == '\0');
    string_free(&s);
}

static void test_utf8(void) {
    StringView s = SV("围棋a");
    CHECK_EQ(utf8_next(&s), 0x56F4);
    CHECK_EQ(utf8_next(&s), 0x68CB);
    CHECK_EQ(utf8_next(&s), 'a');
    char buf[4];
    CHECK_EQ(utf8_encode(0xAC00, buf), 3);   // Hangul 가
}

static void test_arena_and_vec(void) {
    Arena arena = { 0 };
    for (int i = 0; i < 10000; ++i) {
        int *p = arena_new(&arena, int);
        CHECK(*p == 0);
        *p = i;
    }
    StringView copy = arena_copy_sv(&arena, SV("komi"));
    CHECK_SV(copy, SV("komi"));
    arena_free(&arena);

    Vec(int) v = { 0 };
    for (int i = 0; i < 100; ++i) vec_push(&v, i * 2);
    CHECK_EQ(v.len, 100);
    CHECK_EQ(vec_last(&v), 198);
    vec_free(&v);
}

static void check_convert(const char *charset, const char *bytes, const char *expected) {
    String out = { 0 };
    bool ok = text_to_utf8(sv(charset), sv(bytes), &out);
    if (!ok) fprintf(stderr, "    %s conversion failed\n", charset);
    CHECK(ok);
    CHECK_SV(string_sv(&out), sv(expected));
    string_free(&out);
}

static void test_encodings(void) {
    CHECK(utf8_is_valid(SV("黑棋 abc")));
    CHECK(!utf8_is_valid(SV("\xBA\xDA\xC6\xE5")));
    CHECK(!utf8_is_valid(SV("\xC0\x80")));   // overlong
    check_convert("GB2312", "\xBA\xDA\xC6\xE5", "黑棋");
    check_convert("gbk", "\xBA\xDA\xC6\xE5", "黑棋");
    check_convert("Big5", "\xB6\xC2\xB4\xD1", "黑棋");
    check_convert("Shift_JIS", "\x88\xCD\x8C\xE9", "囲碁");
    check_convert("EUC-KR", "\xC8\xE6\xB9\xE9", "흑백");
    check_convert("ISO-8859-1", "caf\xE9", "café");
    String out = { 0 };
    CHECK(!text_to_utf8(SV("klingon"), SV("x"), &out));
}

static void test_sexpr(void) {
    Arena arena = { 0 };
    Sexpr *root = NULL;
    SexprError err = { 0 };
    String src = string_from(SV("; layout\n(app :title \"Cata Gui\"\n  (row :grow :gap 12\n    (button file.new :icon @plus :label)\n    (tabs a b c :height 2.5)))"));
    CHECK(sexpr_parse(&arena, string_sv(&src), &root, &err));
    string_free(&src);   // the tree must not reference the source text
    const Sexpr *app = root->first;
    CHECK(sexpr_is_list(app, "app"));
    CHECK_SV(sexpr_prop_text(app, "title", SV("")), SV("Cata Gui"));
    const Sexpr *row = sexpr_next_child(app, NULL);
    CHECK(sexpr_is_list(row, "row"));
    CHECK(sexpr_flag(row, "grow"));
    CHECK_NEAR(sexpr_prop_number(row, "gap", 0), 12.0, 1e-9);
    const Sexpr *button = sexpr_next_child(row, NULL);
    CHECK_SV(sexpr_next_arg(button, NULL)->text, SV("file.new"));
    CHECK_SV(sexpr_prop_text(button, "icon", SV("")), SV("@plus"));
    CHECK(sexpr_flag(button, "label"));
    CHECK(!sexpr_flag(button, "grow"));
    const Sexpr *tabs = sexpr_next_child(row, button);
    const Sexpr *arg = sexpr_next_arg(tabs, NULL);
    CHECK_SV(arg->text, SV("a"));
    arg = sexpr_next_arg(tabs, arg);
    CHECK_SV(arg->text, SV("b"));
    arg = sexpr_next_arg(tabs, arg);
    CHECK_SV(arg->text, SV("c"));
    CHECK(sexpr_next_arg(tabs, arg) == NULL);   // 2.5 belongs to :height
    // A flag followed by an atom takes it as its value: args must come first.
    Sexpr *amb = NULL;
    CHECK(sexpr_parse(&arena, SV("(tabs :grow x y)"), &amb, &err));
    CHECK(sexpr_next_arg(amb->first, NULL) == NULL);
    CHECK_SV(sexpr_prop_text(amb->first, "grow", SV("")), SV("x"));
    CHECK(sexpr_next_child(row, tabs) == NULL);

    CHECK(!sexpr_parse(&arena, SV("(app (row)"), &root, &err));
    CHECK_EQ(err.line, 1);
    CHECK(!sexpr_parse(&arena, SV("(a))"), &root, &err));
    CHECK(!sexpr_parse(&arena, SV("(a \"open)"), &root, &err));
    arena_free(&arena);
}

// Values written by kv_write_value read back unchanged (config.kv: Windows paths).
static void test_kv_roundtrip(void) {
    static const StringView VALUES[] = { { "C:\\Users\\player\\Downloads", sizeof("C:\\Users\\player\\Downloads") - 1 }, { " padded ", sizeof(" padded ") - 1 }, { "two\nlines\ttab", sizeof("two\nlines\ttab") - 1 },
                                         { "ends with \\", sizeof("ends with \\") - 1 }, { "\"quoted\"", sizeof("\"quoted\"") - 1 }, { "", sizeof("") - 1 } };
    for (isize i = 0; i < countof(VALUES); ++i) {
        String text = string_from(SV("key = "));
        kv_write_value(&text, VALUES[i]);
        string_push_char(&text, '\n');
        KvFile kv = { 0 };
        kv_parse(&kv, string_sv(&text));
        CHECK_SV(kv_get(&kv, SV("key"), SV("<missing>")), VALUES[i]);
        kv_free(&kv);
        string_free(&text);
    }
}

void register_str_tests(void) {
    test_register("str/kv_roundtrip", test_kv_roundtrip);
    test_register("str/sexpr", test_sexpr);
    test_register("str/encodings", test_encodings);
    test_register("str/sv_basics", test_sv_basics);
    test_register("str/sv_parse", test_sv_parse);
    test_register("str/tokens", test_tokens);
    test_register("str/string_builder", test_string_builder);
    test_register("str/utf8", test_utf8);
    test_register("str/arena_vec", test_arena_and_vec);
}
