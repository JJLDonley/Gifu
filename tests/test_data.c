// Validates the shipped data files so a typo in JSON fails CI, not the user.
#include "test.h"

#include "app/config.h"
#include "base/sexpr.h"
#include "app/i18n.h"
#include "base/kv.h"
#include "app/keymap.h"
#include "platform/os.h"
#include "engine/engine_profile.h"

static String asset_path(StringView relative) {
    String assets = path_join(SV(GIFU_SOURCE_DIR), SV("assets"));
    String full = path_join(string_sv(&assets), relative);
    string_free(&assets);
    return full;
}

static void test_themes_parse(void) {
    String dir = asset_path(SV("themes"));
    StringVec names = { 0 };
    CHECK(os_list_dir(string_sv(&dir), &names));
    int themes = 0;
    vec_foreach(String, n, &names) {
        if (!sv_ends_with(string_sv(n), SV(".theme"))) continue;
        String path = path_join(string_sv(&dir), string_sv(n));
        KvFile kv = { 0 };
        CHECK(kv_load(&kv, string_sv(&path)));
        CHECK(kv_get(&kv, SV("name"), SV("")).len > 0);
        vec_foreach(KvEntry, e, &kv.entries) {
            // Every key is either "extends", a known group, or a colour.
            static const char *const GROUPS[] = { "extends", "name", "dark", "colors.", "background.", "board.", "stones.", "shape.", "fonts." };
            bool ok = false;
            for (int g = 0; g < (int)countof(GROUPS); ++g) ok |= sv_starts_with(e->key, sv(GROUPS[g]));
            if (!ok) fprintf(stderr, "    %s:%d: unexpected key '%.*s'\n", string_cstr(n), e->line, SV_ARG(e->key));
            CHECK(ok);
        }
        kv_free(&kv);
        string_free(&path);
        themes++;
    }
    CHECK(themes >= 4);
    string_vec_free(&names);
    string_free(&dir);
}

static void test_kv(void) {
    KvFile kv = { 0 };
    kv_parse(&kv, SV("# comment\n; also comment\nname = Coffee Paper\ncolors.text=#33251A\nquoted = \"  spaced # not a comment \"\n"
                     "esc = a\\u00E9b\nlong = one \\\n  two\ndup = 1\ndup = 2\nflag = yes\n"));
    CHECK_SV(kv_get(&kv, SV("name"), SV("")), SV("Coffee Paper"));
    CHECK_SV(kv_get(&kv, SV("colors.text"), SV("")), SV("#33251A"));
    CHECK_SV(kv_get(&kv, SV("quoted"), SV("")), SV("  spaced # not a comment "));
    CHECK_SV(kv_get(&kv, SV("esc"), SV("")), SV("aéb"));
    CHECK_SV(kv_get(&kv, SV("long"), SV("")), SV("one two"));
    CHECK_SV(kv_get(&kv, SV("dup"), SV("")), SV("2"));
    CHECK_EQ(kv.entries.items[1].line, 4);
    bool b = false;
    CHECK(kv_parse_bool(kv_get(&kv, SV("flag"), SV("")), &b) && b);
    kv_free(&kv);
}

static bool load_kv_asset(const char *relative, KvFile *kv) {
    String path = asset_path(sv(relative));
    bool ok = kv_load(kv, string_sv(&path));
    string_free(&path);
    return ok;
}

static void test_keymap(void) {
    KeyChord c;
    CHECK(key_chord_parse(SV("Ctrl+Shift+O"), &c) && c.key == 'O' && c.mods == (KEYMOD_CTRL | KEYMOD_SHIFT));
    CHECK(key_chord_parse(SV("Right"), &c) && c.key == 262 && c.mods == 0);
    CHECK(!key_chord_parse(SV("Ctrl+"), &c));
    KvFile kv = { 0 };
    CHECK(load_kv_asset("data/keymap.kv", &kv));
    Keymap km = { 0 };
    keymap_load_kv(&km, &kv, SV("keymap.kv"));
    CHECK_EQ(km.bindings.len, kv.entries.len);   // every line is a valid binding
    CHECK(keymap_lookup(&km, (KeyChord){ 262, 0 }) != NULL);
    // A user layer overrides a chord.
    KvFile user = { 0 };
    kv_parse(&user, SV("Right = nav.last\n"));
    keymap_load_kv(&km, &user, SV("user"));
    CHECK_SV(string_sv(&keymap_lookup(&km, (KeyChord){ 262, 0 })->command), SV("nav.last"));
    keymap_free(&km);
    kv_free(&user);
    kv_free(&kv);
}

static void test_engine_profiles(void) {
    KvFile kv = { 0 };
    CHECK(load_kv_asset("data/engines.kv", &kv));
    PathVars vars = { string_from(SV(GIFU_SOURCE_DIR)), string_from(SV(".")) };
    EngineProfileVec profiles = { 0 };
    engine_profiles_from_kv(&kv, &vars, &profiles);
    CHECK(profiles.len >= 1);
    vec_foreach(EngineProfile, p, &profiles) {
        CHECK(sv_find(string_sv(&p->program), SV("${")) < 0);
        printf("    profile %-16s available=%d model=%s\n", string_cstr(&p->id), engine_profile_available(p), string_cstr(&p->model));
    }
    // User layer: override one field set, add args.
    KvFile user = { 0 };
    kv_parse(&user, SV("katago-opencl.name = Mine\nkatago-opencl.program = /x/katago\nkatago-opencl.arg = -a\nkatago-opencl.arg = b c\n"));
    engine_profiles_from_kv(&user, &vars, &profiles);
    const EngineProfile *p = engine_profiles_find(&profiles, SV("katago-opencl"));
    CHECK(p && sv_eq(string_sv(&p->name), SV("Mine")) && p->args.len == 2);
    KvFile remote = { 0 };
    kv_parse(&remote, SV("gpu.launcher = ssh -T  user@box\ngpu.program = /opt/katago\n"));
    engine_profiles_from_kv(&remote, &vars, &profiles);
    const EngineProfile *r = engine_profiles_find(&profiles, SV("gpu"));
    CHECK(r && r->launcher.len == 3 && engine_profile_available(r));
    if (r && r->launcher.len == 3) CHECK_SV(string_sv(&r->launcher.items[2]), SV("user@box"));
    kv_free(&remote);
    if (p && p->args.len == 2) CHECK_SV(string_sv(&p->args.items[1]), SV("b c"));
    engine_profiles_free(&profiles);
    // A model folder: the chosen network (Settings -> KataGo) wins over the newest one.
    String dir = path_join(SV(GIFU_TEST_OUTPUT_DIR), SV("test-models"));
    os_remove_tree(string_sv(&dir));
    CHECK(os_make_dirs(string_sv(&dir)));
    String a = path_join(string_sv(&dir), SV("kata1-b6c96-s1-d1.txt.gz")), b = path_join(string_sv(&dir), SV("kata1-b18c384nbt-s2-d2.bin.gz"));
    CHECK(os_write_file(string_sv(&a), SV("x")) && os_write_file(string_sv(&b), SV("x")));
    KvFile models = { 0 };
    String text = string_format("m.model = " SV_FMT "\n", SV_ARG(string_sv(&dir)));
    kv_parse(&models, string_sv(&text));
    vars.network = SV("kata1-b6c96-s1-d1.txt.gz");
    engine_profiles_from_kv(&models, &vars, &profiles);
    const EngineProfile *m = engine_profiles_find(&profiles, SV("m"));
    CHECK(m && sv_eq(string_sv(&m->model), string_sv(&a)));
    engine_profiles_free(&profiles);
    vars.network = SV("missing.bin.gz");   // gone: falls back to the newest
    engine_profiles_from_kv(&models, &vars, &profiles);
    m = engine_profiles_find(&profiles, SV("m"));
    CHECK(m && sv_ends_with(string_sv(&m->model), SV(".gz")));
    engine_profiles_free(&profiles);
    kv_free(&models);
    os_remove_tree(string_sv(&dir));
    string_free(&text), string_free(&a), string_free(&b), string_free(&dir);
    string_free(&vars.root);
    string_free(&vars.exe_dir);
    kv_free(&user);
    kv_free(&kv);
}

static void test_i18n(void) {
    static const char *const LANGS[] = { "en", "zh_CN", "zh_TW", "ja", "ko" };
    for (int i = 0; i < (int)countof(LANGS); ++i) {
        I18n t = { 0 };
        String rel = string_format("l10n/%s.properties", LANGS[i]);
        String path = asset_path(string_sv(&rel));
        CHECK(i18n_load(&t, string_sv(&path)));
        CHECK(i18n_get(&t, SV("command.file.open"), SV("")).len > 0);
        i18n_free(&t);
        string_free(&path);
        string_free(&rel);
    }
}

// Every field in forms.lisp must bind to a real config key.
static void test_settings_keys(void) {
    String path = asset_path(SV("data/forms.lisp"));
    String text = { 0 };
    CHECK(os_read_file(string_sv(&path), &text));
    Arena arena = { 0 };
    Sexpr *root = NULL;
    SexprError err = { 0 };
    bool ok = sexpr_parse(&arena, string_sv(&text), &root, &err);
    if (!ok) fprintf(stderr, "    forms.lisp:%d:%d: %s\n", err.line, err.column, err.message);
    CHECK(ok);
    int fields = 0, forms = 0;
    for (const Sexpr *form = ok ? root->first : NULL; form; form = form->next) {
        CHECK(sexpr_is_list(form, "form"));
        forms++;
        for (const Sexpr *sec = sexpr_next_child(form, NULL); sec; sec = sexpr_next_child(form, sec)) {
            for (const Sexpr *f = sexpr_next_child(sec, NULL); f; f = sexpr_next_child(sec, f), ++fields) {
                const Sexpr *key = sexpr_next_arg(f, NULL);
                StringView k = key ? key->text : SV("");
                if (sexpr_is_list(f, "command")) {   // a command button: checked against the registry at startup
                    CHECK(sv_find_char(k, '.') > 0);
                    continue;
                }
                if (!config_field(k)) fprintf(stderr, "    forms.lisp:%d: unknown settings key '%.*s'\n", f->line, SV_ARG(k));
                CHECK(config_field(k) != NULL);
            }
        }
    }
    CHECK(forms >= 2);
    CHECK(fields >= 15);
    arena_free(&arena);
    string_free(&text);
    string_free(&path);
}

static void test_config_round_trip(void) {
    String dir = path_join(SV(GIFU_TEST_OUTPUT_DIR), SV("test-config"));
    String path = path_join(string_sv(&dir), SV("config.kv"));
    Config a;
    config_init_defaults(&a, string_sv(&path));
    CHECK_SV(string_sv(&a.theme), SV("cream"));
    config_set_string(&a, config_field(SV("theme")), SV("dark"));
    config_set_number(&a, config_field(SV("analysis.interval_cs")), 500);   // clamps to 100
    config_set_bool(&a, config_field(SV("board.show_coordinates")), false);
    CHECK_EQ(a.analysis_interval_cs, 100);
    CHECK(config_save(&a));
    Config b;
    config_init_defaults(&b, string_sv(&path));
    String err = { 0 };
    CHECK(config_load(&b, &err));
    CHECK_SV(string_sv(&b.theme), SV("dark"));
    CHECK_EQ(b.analysis_interval_cs, 100);
    CHECK(!b.show_coordinates);
    String text = { 0 };
    CHECK(os_read_file(string_sv(&path), &text));
    CHECK(sv_find(string_sv(&text), SV("analysis.interval_cs = 100")) >= 0);
    string_free(&text);
    config_free(&a);
    config_free(&b);
    string_free(&err);
    string_free(&path);
    string_free(&dir);
}

static void check_layout_node(const Sexpr *node, int *bad) {
    static const char *const KNOWN[] = { "app", "row", "column", "menubar", "menu", "toolbar", "navbar", "button", "separator",
                                         "spacer", "engine-status", "theme-picker", "move-counter", "status", "board", "panel", "tabs",
                                         "modes", "mode", "mode-switch", "markup-colors", "markup-counts", "board-menu" };
    bool ok = false;
    for (int i = 0; i < (int)countof(KNOWN); ++i) ok |= sv_eq(sexpr_head(node), sv(KNOWN[i]));
    if (!ok) fprintf(stderr, "    layout.lisp:%d: unknown component '%.*s'\n", node->line, SV_ARG(sexpr_head(node)));
    *bad += !ok;
    for (const Sexpr *c = sexpr_next_child(node, NULL); c; c = sexpr_next_child(node, c)) check_layout_node(c, bad);
}

static void test_layout_lisp(void) {
    String path = asset_path(SV("data/layout.lisp"));
    String text = { 0 };
    CHECK(os_read_file(string_sv(&path), &text));
    Arena arena = { 0 };
    Sexpr *root = NULL;
    SexprError err = { 0 };
    bool ok = sexpr_parse(&arena, string_sv(&text), &root, &err);
    if (!ok) fprintf(stderr, "    layout.lisp:%d:%d: %s\n", err.line, err.column, err.message);
    CHECK(ok);
    if (ok) {
        CHECK(sexpr_is_list(root->first, "app"));
        CHECK(root->first->next == NULL);
        int bad = 0;
        check_layout_node(root->first, &bad);
        CHECK_EQ(bad, 0);
        // The board's right-click menus.
        int board_menus = 0;
        for (const Sexpr *c = sexpr_next_child(root->first, NULL); c; c = sexpr_next_child(root->first, c)) board_menus += sexpr_is_list(c, "board-menu");
        CHECK_EQ(board_menus, 2);
    }
    arena_free(&arena);
    string_free(&text);
    string_free(&path);
}

void register_data_tests(void) {
    test_register("data/kv", test_kv);
    test_register("data/layout_lisp", test_layout_lisp);
    test_register("data/settings_keys", test_settings_keys);
    test_register("data/config_round_trip", test_config_round_trip);
    test_register("data/themes_parse", test_themes_parse);
    test_register("data/keymap", test_keymap);
    test_register("data/engine_profiles", test_engine_profiles);
    test_register("data/i18n", test_i18n);
}
