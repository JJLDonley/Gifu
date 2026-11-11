#include "test.h"

#include "base/codec.h"
#include "core/sgf.h"
#include "core/sgf_analysis.h"

// Reference data made with Python's gzip / base64 / json exactly like KaTrain's
// analysis_dumps (ownership as 4 half floats, empty policy, main JSON).
static const char *const KT_SAMPLE[3] = {
    "H4sIAP4S82oC/2OwYNjCYMPAAAB8hpg2CAAAAA==",
    "H4sIAP4S82oC/wMAAAAAAAAAAAA=",
    "H4sIAP4S82oC/2WNvQ7CMAyEX6XKDFFSwu8KI2+AGKrWQySKKyeEoeq7Y4eBlE65O1/uG1WPCYI6VaM6b/IjAQuxq0ohdUBsLevkg4/SdYbd2z+piVI12sk5tEhwhabjaG31lqMhsb7J1H1ie3EzAtuCYEqCrReI/T/C6MMP8d3KICERYsys2cRuOXEsqbUx8rnFfnhABKlEesH0AeMSV+YmAQAA",
};

static void test_codecs(void) {
    String enc = { 0 }, dec = { 0 };
    base64_encode(SV("Many hands make light work."), &enc);
    CHECK_SV(string_sv(&enc), SV("TWFueSBoYW5kcyBtYWtlIGxpZ2h0IHdvcmsu"));
    CHECK(base64_decode(string_sv(&enc), &dec));
    CHECK_SV(string_sv(&dec), SV("Many hands make light work."));
    string_clear(&enc), string_clear(&dec);
    base64_encode(SV("ab"), &enc);
    CHECK_SV(string_sv(&enc), SV("YWI="));
    CHECK_EQ(crc32_update(0, SV("123456789")), 0xCBF43926u);
    // Python's gzip (real deflate) decodes; our stored gzip round-trips.
    String gz = { 0 };
    CHECK(base64_decode(SV("H4sIAP4S82oC/8tIzcnJV8hAkACAiPnlEQAAAA=="), &gz));
    string_clear(&dec);
    CHECK(gzip_decompress(string_sv(&gz), &dec));
    CHECK_SV(string_sv(&dec), SV("hello hello hello"));
    string_clear(&gz), string_clear(&dec);
    gzip_compress_stored(SV("stored block"), &gz);
    CHECK(gzip_decompress(string_sv(&gz), &dec));
    CHECK_SV(string_sv(&dec), SV("stored block"));
    string_clear(&gz), string_clear(&dec);
    gzip_compress_stored(SV(""), &gz);
    CHECK(gzip_decompress(string_sv(&gz), &dec));
    CHECK_EQ(dec.len, 0);
    string_free(&enc), string_free(&dec), string_free(&gz);
}

static void test_katrain_kt(void) {
    StringView values[3] = { sv(KT_SAMPLE[0]), sv(KT_SAMPLE[1]), sv(KT_SAMPLE[2]) };
    NodeEval e;
    CHECK(kt_parse(values, 3, 19, 19, &e));
    CHECK_NEAR(e.black_winrate, 0.46f, 1e-5f);
    CHECK_NEAR(e.black_score, -0.9f, 1e-5f);
    CHECK_EQ(e.visits, 200);
    CHECK_EQ(e.top_count, 2);
    Point d4, c3;
    CHECK(point_from_gtp(SV("D4"), 19, 19, &d4) && point_from_gtp(SV("C3"), 19, 19, &c3));
    CHECK(point_eq(e.top[0], d4) && point_eq(e.top[1], c3));   // by "order", not by key
    // Our KT round-trips.
    String parts[3];
    kt_format(&e, 19, 19, parts);
    StringView ours[3] = { string_sv(&parts[0]), string_sv(&parts[1]), string_sv(&parts[2]) };
    NodeEval f;
    CHECK(kt_parse(ours, 3, 19, 19, &f));
    CHECK_NEAR(f.black_winrate, 0.46f, 1e-5f);
    CHECK_EQ(f.visits, 200);
    CHECK(f.top_count == 1 && point_eq(f.top[0], d4));
    for (i32 i = 0; i < 3; ++i) string_free(&parts[i]);
    CHECK(!kt_parse(values, 2, 19, 19, &e));
}

static void test_lizzie_lz(void) {
    // LizzieYzy after Black's move (White to play): 45.4 = Black's winrate, score for White (to move).
    NodeEval e;
    CHECK(lz_parse(SV("KataGo 45.4 1.2k 1.5 12.0\nmove D4 visits 800 winrate 5460 prior 30 scoreMean 1.50 pv D4 C3 info move C3 visits 200 winrate 5200"),
                   STONE_WHITE, 19, 19, &e));
    CHECK_NEAR(e.black_winrate, 0.454f, 1e-4f);
    CHECK_NEAR(e.black_score, -1.5f, 1e-4f);
    CHECK_EQ(e.visits, 1200);
    CHECK_EQ(e.top_count, 2);
    String text = lz_format(&e, STONE_WHITE, 19, 19);
    NodeEval f;
    CHECK(lz_parse(string_sv(&text), STONE_WHITE, 19, 19, &f));
    CHECK_NEAR(f.black_winrate, 0.454f, 1e-3f);
    CHECK_NEAR(f.black_score, -1.5f, 1e-2f);
    CHECK_EQ(f.visits, 1200);
    string_free(&text);

    // Loading: LZ / KT become evaluations; saving writes them back (and KTV on the root).
    SgfError err;
    GameTree *t = sgf_parse(SV("(;GM[1]SZ[19];B[pd]LZ[KataGo 45.4 300 1.5 0.0\nmove dp visits 300 winrate 5460 pv D4];W[dp])"), &err);
    CHECK(t != NULL);
    GameNode *b1 = t->root->first_child;
    CHECK(b1->eval.valid);
    CHECK_NEAR(b1->eval.black_winrate, 0.454f, 1e-4f);
    game_tree_store_interop(t, true, true);
    String sgf = sgf_write(t);
    CHECK(sv_find(string_sv(&sgf), SV("KTV[1.0]")) > 0);
    CHECK(sv_find(string_sv(&sgf), SV("KT[")) > 0);
    GameTree *u = sgf_parse(string_sv(&sgf), &err);
    CHECK(u && u->root->first_child->eval.valid);
    string_free(&sgf);
    game_tree_free(u);
    game_tree_free(t);
}

void register_interop_tests(void) {
    test_register("interop/codecs", test_codecs);
    test_register("interop/katrain_kt", test_katrain_kt);
    test_register("interop/lizzie_lz", test_lizzie_lz);
}
