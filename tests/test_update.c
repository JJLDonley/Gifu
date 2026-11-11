#include "test.h"

#include "app/update.h"
#include "app/updater.h"
#include "base/codec.h"
#include "base/version_info.h"

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

void register_update_tests(void) {
    test_register("update/install_dir", test_install_dir);
    test_register("update/sha256", test_sha256);
    test_register("update/versions", test_versions);
    test_register("update/release_pick", test_release_pick);
}
