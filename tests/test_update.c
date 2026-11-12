#include "test.h"

#include "app/update.h"
#include "app/updater.h"
#include "base/codec.h"
#include "base/version_info.h"
#include "platform/os.h"

#include <stdio.h>

#include <stdlib.h>
#include <string.h>

static void test_sha256(void) {
    char hex[65];
    sha256_hex(SV(""), hex);
    CHECK_SV(sv(hex), SV("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
    sha256_hex(SV("abc"), hex);
    CHECK_SV(sv(hex), SV("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    sha256_hex(SV("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"), hex);   // crosses a block
    CHECK_SV(sv(hex), SV("248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"));
    char *million = malloc(1000000);
    memset(million, 'a', 1000000);
    Sha256 s;
    u8 digest[32];
    sha256_init(&s);
    for (i32 i = 0; i < 10; ++i) sha256_update(&s, (StringView){ million + i * 100000, 100000 });   // streaming
    sha256_final(&s, digest);
    CHECK(digest[0] == 0xcd && digest[1] == 0xc7 && digest[31] == 0xd0);
    free(million);
}

static void test_versions(void) {
    CHECK(version_compare(SV("0.1.0"), SV("0.2.0")) < 0);
    CHECK(version_compare(SV("v0.10.0"), SV("0.9.9")) > 0);   // numeric, with a v
    CHECK(version_compare(SV("1.0"), SV("1.0.0")) == 0);
    CHECK(version_compare(SV("0.2.0-alpha.1"), SV("0.2.0")) < 0);   // release > its pre-releases
    CHECK(version_compare(SV("0.2.0-alpha.2"), SV("0.2.0-alpha.10")) < 0);
    CHECK(version_compare(SV("0.2.0-alpha"), SV("0.2.0-alpha.1")) < 0);   // fewer identifiers first
    CHECK(version_compare(SV("0.2.0-alpha.9"), SV("0.2.0-beta.1")) < 0);
    CHECK(version_compare(SV("0.2.0-alpha.1"), SV("0.1.9")) > 0);
    CHECK(version_compare(SV("v0.3.0-rc.1"), SV("0.3.0-rc.1")) == 0);
    CHECK(version_is_prerelease(SV("v0.2.0-alpha.1")) && !version_is_prerelease(SV("0.2.0")));
    CHECK(strlen(GIFU_VERSION) >= 5);
    CHECK(update_channel_from(SV("auto"), SV("0.1.0-alpha.1")) == UPDATE_CHANNEL_ALPHA);   // running a test version
    CHECK(update_channel_from(SV("auto"), SV("0.1.0")) == UPDATE_CHANNEL_STABLE);
    CHECK(update_channel_from(SV("stable"), SV("0.1.0-alpha.1")) == UPDATE_CHANNEL_STABLE);   // the user's choice wins
    CHECK(update_channel_from(SV("prerelease"), SV("0.1.0")) == UPDATE_CHANNEL_ALPHA);   // generated from project(VERSION)
}

static const char RELEASES[] =
    "["
    "{\"tag_name\":\"v0.4.0\",\"draft\":true,\"prerelease\":false,\"assets\":["
    "  {\"name\":\"Gifu-0.4.0-windows-x64.zip\",\"browser_download_url\":\"https://x/d4.zip\"},{\"name\":\"SHA256SUMS\",\"browser_download_url\":\"https://x/s4\"}]},"
    "{\"tag_name\":\"v0.3.0-alpha.2\",\"prerelease\":true,\"body\":\"alpha notes\",\"html_url\":\"https://x/a\",\"assets\":["
    "  {\"name\":\"Gifu-0.3.0-alpha.2-windows-x64.zip\",\"size\":123,\"browser_download_url\":\"https://x/a.zip\"},"
    "  {\"name\":\"Gifu-0.3.0-alpha.2-linux-x64.tar.xz\",\"browser_download_url\":\"https://x/a.tar.xz\"},"
    "  {\"name\":\"SHA256SUMS\",\"browser_download_url\":\"https://x/sa\"}]},"
    "{\"tag_name\":\"v0.2.1\",\"prerelease\":false,\"assets\":["
    "  {\"name\":\"Gifu-0.2.1-windows-x64.zip\",\"browser_download_url\":\"https://x/w21.zip\"}]},"   // no SHA256SUMS: skipped
    "{\"tag_name\":\"v0.2.0\",\"prerelease\":false,\"body\":\"stable notes\",\"assets\":["
    "  {\"name\":\"Gifu-0.2.0-windows-x64.zip\",\"browser_download_url\":\"https://x/w20.zip\"},"
    "  {\"name\":\"Gifu-0.2.0-linux-x64.tar.xz\",\"browser_download_url\":\"https://x/l20.tar.xz\"},"
    "  {\"name\":\"SHA256SUMS\",\"browser_download_url\":\"https://x/s20\"}]}"
    "]";

static void test_release_pick(void) {
    ReleaseInfo r;
    CHECK(release_pick(sv(RELEASES), UPDATE_CHANNEL_STABLE, SV("-windows-x64.zip"), &r));
    CHECK_SV(string_sv(&r.version), SV("0.2.0"));   // the draft, the alpha and the unverifiable 0.2.1 are skipped
    CHECK_SV(string_sv(&r.asset_url), SV("https://x/w20.zip"));
    CHECK_SV(string_sv(&r.sums_url), SV("https://x/s20"));
    CHECK_SV(string_sv(&r.notes), SV("stable notes"));
    release_info_free(&r);
    CHECK(release_pick(sv(RELEASES), UPDATE_CHANNEL_ALPHA, SV("-windows-x64.zip"), &r));
    CHECK_SV(string_sv(&r.version), SV("0.3.0-alpha.2"));
    CHECK(r.prerelease && r.asset_size == 123);
    release_info_free(&r);
    CHECK(release_pick(sv(RELEASES), UPDATE_CHANNEL_ALPHA, SV("-linux-x64.tar.xz"), &r));
    CHECK_SV(string_sv(&r.asset_name), SV("Gifu-0.3.0-alpha.2-linux-x64.tar.xz"));
    release_info_free(&r);
    CHECK(!release_pick(sv(RELEASES), UPDATE_CHANNEL_STABLE, SV("-x86_64.AppImage"), &r));   // none for this platform
    ReleaseVec all;
    CHECK(release_list(sv(RELEASES), SV("-windows-x64.zip"), &all));
    CHECK(all.len == 2);   // not the draft, not 0.2.1 (no SHA256SUMS); newest first
    CHECK_SV(string_sv(&all.items[0].version), SV("0.3.0-alpha.2"));
    CHECK(all.items[0].prerelease && !all.items[1].prerelease);
    CHECK_SV(string_sv(&all.items[1].version), SV("0.2.0"));
    release_vec_free(&all);
    CHECK(!release_pick(SV("not json"), UPDATE_CHANNEL_STABLE, SV("-windows-x64.zip"), &r));

    const char *sums = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef  Gifu-0.2.0-linux-x64.tar.xz\n"
                       "FEDCBA9876543210FEDCBA9876543210FEDCBA9876543210FEDCBA9876543210 *Gifu-0.2.0-windows-x64.zip\n";
    char hex[65];
    CHECK(sums_find(sv(sums), SV("Gifu-0.2.0-windows-x64.zip"), hex));
    CHECK_SV(sv(hex), SV("fedcba9876543210fedcba9876543210fedcba9876543210fedcba9876543210"));   // lower-cased
    CHECK(sums_find(sv(sums), SV("Gifu-0.2.0-linux-x64.tar.xz"), hex));
    CHECK(!sums_find(sv(sums), SV("Gifu-0.2.0-x86_64.AppImage"), hex));
}

static void test_install_dir(void) {
    CHECK_SV(updater_install_dir(SV("/Applications/Gifu.app/Contents/MacOS")), SV("/Applications"));   // the bundle is swapped
    CHECK_SV(updater_install_dir(SV("/opt/gifu")), SV("/opt/gifu"));
    CHECK_SV(updater_install_dir(SV("C:/Games/Gifu")), SV("C:/Games/Gifu"));
    CHECK_SV(updater_install_dir(SV("/x/Contents/MacOS")), SV("/x/Contents/MacOS"));   // not inside a .app
}

#ifdef __linux__
static UpdaterPhase wait_phase(Updater *u, UpdaterStatus *st) {
    for (i32 i = 0; i < 1000; ++i) {
        updater_status(u, st);
        if (st->phase != UPDATER_CHECKING && st->phase != UPDATER_DOWNLOADING && st->phase != UPDATER_VERIFYING) return st->phase;
        updater_status_free(st);
        os_sleep_ms(10);
    }
    updater_status(u, st);
    return st->phase;
}

// An AppImage runs from a read-only mount (/tmp/.mount_...): the update is downloaded
// next to the .AppImage file and nothing is written to the program's folder.
static void test_appimage_download(void) {
    String dir = path_join(SV(GIFU_TEST_OUTPUT_DIR), SV("appimage-update"));
    os_remove_tree(string_sv(&dir));
    CHECK(os_make_dirs(string_sv(&dir)));
    String image = path_join(string_sv(&dir), SV("Gifu-old-x86_64.AppImage"));
    String asset = path_join(string_sv(&dir), SV("Gifu-99.0.0-x86_64.AppImage"));
    String sums = path_join(string_sv(&dir), SV("SHA256SUMS")), api = path_join(string_sv(&dir), SV("releases.json"));
    CHECK(os_write_file(string_sv(&image), SV("old image")));
    CHECK(os_write_file(string_sv(&asset), SV("new image")));
    char hex[65];
    sha256_hex(SV("new image"), hex);
    String text = string_format("%s  Gifu-99.0.0-x86_64.AppImage\n", hex);
    CHECK(os_write_file(string_sv(&sums), string_sv(&text)));
    string_free(&text);
    text = string_format("[{\"tag_name\":\"v99.0.0\",\"prerelease\":false,\"assets\":["
                         "{\"name\":\"Gifu-99.0.0-x86_64.AppImage\",\"browser_download_url\":\"file://%s\"},"
                         "{\"name\":\"SHA256SUMS\",\"browser_download_url\":\"file://%s\"}]}]",
                         string_cstr(&asset), string_cstr(&sums));
    CHECK(os_write_file(string_sv(&api), string_sv(&text)));
    string_free(&text);
    String api_url = string_format("file://%s", string_cstr(&api));
    // /proc refuses new folders, like the AppImage's read-only mount.
    Updater *u = updater_create(SV("/proc/gifu-appimage-mount/usr/lib/gifu"), string_sv(&image), string_sv(&api_url));
    UpdaterStatus st;
    updater_check(u, UPDATE_CHANNEL_STABLE);
    CHECK(wait_phase(u, &st) == UPDATER_AVAILABLE);
    updater_status_free(&st);
    updater_install(u, SV("99.0.0"));
    UpdaterPhase phase = wait_phase(u, &st);
    if (phase != UPDATER_READY) fprintf(stderr, "    updater: %s\n", string_cstr(&st.error));
    CHECK(phase == UPDATER_READY);
    updater_status_free(&st);
    String staged = string_format("%s.update", string_cstr(&image)), got = { 0 };
    CHECK(os_read_file(string_sv(&staged), &got));
    CHECK_SV(string_sv(&got), SV("new image"));
    updater_destroy(u);
    string_free(&got), string_free(&staged), string_free(&api_url);
    string_free(&image), string_free(&asset), string_free(&sums), string_free(&api);
    os_remove_tree(string_sv(&dir));
    string_free(&dir);
}

// The .tar.xz install: the archive is unpacked in <install>/.update, its items replace
// the installed ones (katago/ stays), the old ones wait in .update/old, the new
// program starts with --updated-from, and the next start removes .update.
static void test_archive_update(void) {
    String dir = path_join(SV(GIFU_TEST_OUTPUT_DIR), SV("archive-update"));
    os_remove_tree(string_sv(&dir));
    String install = path_join(string_sv(&dir), SV("install")), src = path_join(string_sv(&dir), SV("src"));
    String pkg = path_join(string_sv(&src), SV("Gifu-99.0.0-linux-x64"));
    String marker = path_join(string_sv(&dir), SV("started.txt"));
    // Installed: the old program, its assets and the user's engine.
    String old_assets = path_join(string_sv(&install), SV("assets")), engine = path_join(string_sv(&install), SV("katago"));
    CHECK(os_make_dirs(string_sv(&old_assets)) && os_make_dirs(string_sv(&engine)));
    String f = path_join(string_sv(&install), SV("gifu"));
    CHECK(os_write_file(string_sv(&f), SV("#!/bin/sh\nexit 1\n")));
    string_free(&f);
    f = path_join(string_sv(&install), SV("THIRD_PARTY_NOTICES.md"));   // what marks a packaged install
    CHECK(os_write_file(string_sv(&f), SV("notices")));
    string_free(&f);
    f = path_join(string_sv(&old_assets), SV("old.txt"));
    CHECK(os_write_file(string_sv(&f), SV("old")));
    string_free(&f);
    f = path_join(string_sv(&engine), SV("net.bin.gz"));
    CHECK(os_write_file(string_sv(&f), SV("my network")));
    string_free(&f);
    // The release archive: one top folder, as package.py makes it. Its program says it started.
    String new_assets = path_join(string_sv(&pkg), SV("assets")), new_engine = path_join(string_sv(&pkg), SV("katago"));
    CHECK(os_make_dirs(string_sv(&new_assets)) && os_make_dirs(string_sv(&new_engine)));
    f = path_join(string_sv(&pkg), SV("gifu"));
    String script = string_format("#!/bin/sh\necho \"$@\" > '%s'\n", string_cstr(&marker));
    CHECK(os_write_file(string_sv(&f), string_sv(&script)));
    string_free(&script), string_free(&f);
    f = path_join(string_sv(&pkg), SV("THIRD_PARTY_NOTICES.md"));
    CHECK(os_write_file(string_sv(&f), SV("notices")));
    string_free(&f);
    f = path_join(string_sv(&new_assets), SV("new.txt"));
    CHECK(os_write_file(string_sv(&f), SV("new")));
    string_free(&f);
    f = path_join(string_sv(&new_engine), SV("PUT_KATAGO_HERE.txt"));
    CHECK(os_write_file(string_sv(&f), SV("placeholder")));
    string_free(&f);
    String archive = path_join(string_sv(&dir), SV("Gifu-99.0.0-linux-x64.tar.xz"));
    String cmd = string_format("tar -cJf '%s' -C '%s' Gifu-99.0.0-linux-x64", string_cstr(&archive), string_cstr(&src));
    CHECK(system(string_cstr(&cmd)) == 0);
    string_free(&cmd);
    String bytes = { 0 };
    CHECK(os_read_file(string_sv(&archive), &bytes));
    char hex[65];
    sha256_hex(string_sv(&bytes), hex);
    string_free(&bytes);
    String sums = path_join(string_sv(&dir), SV("SHA256SUMS")), api = path_join(string_sv(&dir), SV("releases.json"));
    String text = string_format("%s  Gifu-99.0.0-linux-x64.tar.xz\n", hex);
    CHECK(os_write_file(string_sv(&sums), string_sv(&text)));
    string_free(&text);
    text = string_format("[{\"tag_name\":\"v99.0.0\",\"prerelease\":false,\"assets\":["
                         "{\"name\":\"Gifu-99.0.0-linux-x64.tar.xz\",\"browser_download_url\":\"file://%s\"},"
                         "{\"name\":\"SHA256SUMS\",\"browser_download_url\":\"file://%s\"}]}]",
                         string_cstr(&archive), string_cstr(&sums));
    CHECK(os_write_file(string_sv(&api), string_sv(&text)));
    string_free(&text);
    String api_url = string_format("file://%s", string_cstr(&api));

    Updater *u = updater_create(string_sv(&install), SV(""), string_sv(&api_url));
    UpdaterStatus st;
    updater_check(u, UPDATE_CHANNEL_STABLE);
    CHECK(wait_phase(u, &st) == UPDATER_AVAILABLE);
    updater_status_free(&st);
    updater_install(u, SV("99.0.0"));
    UpdaterPhase phase = wait_phase(u, &st);
    if (phase != UPDATER_READY) fprintf(stderr, "    updater: %s\n", string_cstr(&st.error));
    CHECK(phase == UPDATER_READY);
    updater_status_free(&st);
    String err = { 0 };
    StringView restart[] = { SV("game.sgf") };
    CHECK(updater_apply(u, restart, 1, &err));
    if (err.len) fprintf(stderr, "    apply: %s\n", string_cstr(&err));
    string_free(&err);
    updater_destroy(u);

    // The new program started, with the file to open and --updated-from <this process>.
    String got = { 0 };
    for (i32 i = 0; i < 300 && !os_read_file(string_sv(&marker), &got); ++i) os_sleep_ms(10);
    String want = string_format("game.sgf --updated-from %lld\n", (long long)os_process_id());
    CHECK_SV(string_sv(&got), string_sv(&want));
    string_free(&got), string_free(&want);
    String path = path_join(string_sv(&install), SV("assets/new.txt"));
    CHECK(os_file_exists(string_sv(&path)));   // new files in place
    string_free(&path);
    path = path_join(string_sv(&install), SV("assets/old.txt"));
    CHECK(!os_file_exists(string_sv(&path)));   // the old assets folder was replaced as a whole
    string_free(&path);
    path = path_join(string_sv(&install), SV("katago/net.bin.gz"));
    CHECK(os_read_file(string_sv(&path), &got) && sv_eq(string_sv(&got), SV("my network")));   // the engine stays
    string_free(&got), string_free(&path);
    path = path_join(string_sv(&install), SV(".update/old/assets/old.txt"));
    CHECK(os_file_exists(string_sv(&path)));   // the previous version, until the next start
    string_free(&path);
    // The new version's first start removes what the update left behind.
    updater_cleanup(string_sv(&install), 0);
    path = path_join(string_sv(&install), SV(".update"));
    for (i32 i = 0; i < 300 && os_dir_exists(string_sv(&path)); ++i) os_sleep_ms(10);
    CHECK(!os_dir_exists(string_sv(&path)));
    string_free(&path);

    string_free(&api_url), string_free(&api), string_free(&sums), string_free(&archive);
    string_free(&new_assets), string_free(&new_engine), string_free(&old_assets), string_free(&engine);
    string_free(&marker), string_free(&pkg), string_free(&src), string_free(&install);
    os_remove_tree(string_sv(&dir));
    string_free(&dir);
}
#endif

void register_update_tests(void) {
#ifdef __linux__
    test_register("update/appimage_download", test_appimage_download);
    test_register("update/archive_update", test_archive_update);
#endif
    test_register("update/install_dir", test_install_dir);
    test_register("update/sha256", test_sha256);
    test_register("update/versions", test_versions);
    test_register("update/release_pick", test_release_pick);
}
