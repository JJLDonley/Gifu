#include "test.h"

#include "app/installer.h"
#include "platform/os.h"
#include "base/zip.h"

// Trimmed shape of https://api.github.com/repos/lightvector/KataGo/releases (newest first):
// v1.18.2 is CUDA-only, v1.18.1 has OpenCL/Eigen, v1.15.0 carries the human SL network.
static const char RELEASES[] =
    "[{\"tag_name\":\"v1.18.2\",\"draft\":false,\"assets\":["
    "{\"name\":\"katago-v1.18.2-cuda12.8-cudnn9.8.0-windows-x64.zip\",\"size\":100,\"browser_download_url\":\"https://x/cuda-win.zip\"},"
    "{\"name\":\"katago-v1.18.2-cuda12.8-cudnn9.8.0-linux-x64+bs50.zip\",\"size\":101,\"browser_download_url\":\"https://x/cuda-bs50.zip\"}]},"
    "{\"tag_name\":\"v1.18.1\",\"draft\":false,\"assets\":["
    "{\"name\":\"katago-v1.18.1-opencl-windows-x64+bs50.zip\",\"size\":5,\"browser_download_url\":\"https://x/bs50.zip\"},"
    "{\"name\":\"katago-v1.18.1-opencl-windows-x64.zip\",\"size\":15000000,\"browser_download_url\":\"https://x/opencl-win.zip\"},"
    "{\"name\":\"katago-v1.18.1-eigen-linux-x64.zip\",\"size\":7,\"browser_download_url\":\"https://x/eigen-linux.zip\"},"
    "{\"name\":\"katago-v1.18.1-eigenavx2-linux-x64.zip\",\"size\":8,\"browser_download_url\":\"https://x/avx2-linux.zip\"}]},"
    "{\"tag_name\":\"v1.15.0\",\"draft\":false,\"assets\":["
    "{\"name\":\"b18c384nbt-humanv0.ckpt\",\"size\":3,\"browser_download_url\":\"https://x/human.ckpt\"},"
    "{\"name\":\"b18c384nbt-humanv0.bin.gz\",\"size\":99066230,\"browser_download_url\":\"https://x/human.bin.gz\"}]}]";

static void test_pick_engine(void) {
    DownloadAsset a = { 0 };
    CHECK(katago_pick_engine_asset(sv_from_parts(RELEASES, (isize)sizeof(RELEASES) - 1), SV("opencl"), SV("windows"), &a));
    CHECK_SV(string_sv(&a.url), SV("https://x/opencl-win.zip"));   // skips +bs50 and the CUDA-only release
    CHECK_SV(string_sv(&a.version), SV("v1.18.1"));
    CHECK_EQ(a.size, 15000000);
    download_asset_free(&a);
    CHECK(katago_pick_engine_asset(sv_from_parts(RELEASES, (isize)sizeof(RELEASES) - 1), SV("cuda12.8-cudnn9.8.0"), SV("windows"), &a));
    CHECK_SV(string_sv(&a.version), SV("v1.18.2"));                // newest wins when it has the backend
    download_asset_free(&a);
    CHECK(katago_pick_engine_asset(sv_from_parts(RELEASES, (isize)sizeof(RELEASES) - 1), SV("eigen"), SV("linux"), &a));
    CHECK_SV(string_sv(&a.url), SV("https://x/eigen-linux.zip"));   // not eigenavx2
    download_asset_free(&a);
    CHECK(!katago_pick_engine_asset(sv_from_parts(RELEASES, (isize)sizeof(RELEASES) - 1), SV("opencl"), SV("macos"), &a));
    CHECK(!katago_pick_engine_asset(SV("not json"), SV("opencl"), SV("linux"), &a));
}

static void test_pick_human(void) {
    DownloadAsset a = { 0 };
    CHECK(katago_pick_human_asset(sv_from_parts(RELEASES, (isize)sizeof(RELEASES) - 1), &a));
    CHECK_SV(string_sv(&a.name), SV("b18c384nbt-humanv0.bin.gz"));
    CHECK_EQ(a.size, 99066230);
    download_asset_free(&a);
}

static void test_strongest_network(void) {
    String path = path_join(SV(GIFU_SOURCE_DIR), SV("tests/data/networks_snippet.html"));
    String html = { 0 };
    CHECK(os_read_file(string_sv(&path), &html));
    String name = { 0 };
    CHECK(katago_strongest_network(string_sv(&html), &name));
    CHECK(sv_starts_with(string_sv(&name), SV("kata1-")));
    CHECK(!sv_ends_with(string_sv(&name), SV(".bin.gz")));
    String url = katago_network_url(string_sv(&name));
    CHECK(sv_starts_with(string_sv(&url), SV("https://media.katagotraining.org/uploaded/networks/models/kata1/kata1-")));
    CHECK(sv_ends_with(string_sv(&url), SV(".bin.gz")));
    CHECK(!katago_strongest_network(SV("<html>nothing here</html>"), &name));
    // Best of one family: the snippet has two b18c384nbt rows (13619.4 and 13627.2).
    CHECK(katago_best_network(string_sv(&html), SV("b18c384nbt"), 1000, &name));
    CHECK_SV(string_sv(&name), SV("kata1-b18c384nbt-s9996604416-d4316597426"));
    CHECK(katago_best_network(string_sv(&html), SV("tf3-b11c768"), 1000, &name));
    CHECK_SV(string_sv(&name), SV("kata1-tf3-b11c768-s12382M-d6533M"));
    CHECK(!katago_best_network(string_sv(&html), SV("b18c384"), 1000, &name));   // family must match exactly
    // Too few rating games is not confident.
    StringView few = SV("<tr><td> kata1-b18c384nbt-s1-d1 </td><td> x </td><td> 99999.0 \xC2\xB1 300.0 - (12 games) </td></tr>"
                        "<tr><td> kata1-b18c384nbt-s2-d2 </td><td> x </td><td> 100.5 \xC2\xB1 3.0 - (2,500 games) </td></tr>");
    CHECK(katago_best_network(few, SV("b18c384nbt"), 1000, &name));
    CHECK_SV(string_sv(&name), SV("kata1-b18c384nbt-s2-d2"));
    string_free(&url);
    string_free(&name);
    string_free(&html);
    string_free(&path);
}

static void test_network_families(void) {
    CHECK(katago_family_find(SV("b18c384nbt")) >= 0 && katago_family_find(SV("b20c256")) < 0);
    CHECK(katago_default_families() == 1u << katago_family_find(SV("b18c384nbt")));
    // The page links older networks as .txt.gz: the URL follows the page.
    StringView html = SV("<a href=\"https://media.katagotraining.org/uploaded/networks/models/kata1/kata1-b6c96-s1-d2.txt.gz\">"
                         "<a href=\"https://media.katagotraining.org/uploaded/networks/models/kata1/kata1-b18c384nbt-s3-d4.bin.gz\">");
    String url = katago_network_url_in(html, SV("kata1-b6c96-s1-d2"));
    CHECK(sv_ends_with(string_sv(&url), SV("/kata1-b6c96-s1-d2.txt.gz")));
    string_free(&url);
    url = katago_network_url_in(html, SV("kata1-b18c384nbt-s3-d4"));
    CHECK(sv_ends_with(string_sv(&url), SV("/kata1-b18c384nbt-s3-d4.bin.gz")));
    string_free(&url);
    // Detection: which families are in a networks folder.
    String dir = path_join(SV(GIFU_TEST_OUTPUT_DIR), SV("test-families"));
    String nets = path_join(string_sv(&dir), SV("networks"));
    os_remove_tree(string_sv(&dir));
    CHECK(os_make_dirs(string_sv(&nets)));
    const char *files[] = { "kata1-b6c96-s1-d2.txt.gz", "kata1-b28c512nbt-s5-d6.bin.gz", "kata1-b18c384-s1-d1.bin.gz" /* not b18c384nbt */ };
    for (i32 i = 0; i < countof(files); ++i) {
        String f = path_join(string_sv(&nets), sv(files[i]));
        CHECK(os_write_file(string_sv(&f), SV("x")));
        string_free(&f);
    }
    const KataGoBackend *backend = katago_backend_at(0);
    InstallStatus st = installer_detect(string_sv(&dir), backend);
    CHECK(st.network);
    CHECK(st.families == ((1u << katago_family_find(SV("b6c96"))) | (1u << katago_family_find(SV("b28c512nbt")))));
    os_remove_tree(string_sv(&dir));
    string_free(&nets);
    string_free(&dir);
}

static void test_zip(void) {
    String zip = path_join(SV(GIFU_SOURCE_DIR), SV("tests/data/sample.zip"));
    String dest = path_join(SV(GIFU_TEST_OUTPUT_DIR), SV("test-zip"));
    ZipStats stats;
    String err = { 0 };
    bool ok = zip_extract(string_sv(&zip), string_sv(&dest), &stats, &err);
    if (!ok) fprintf(stderr, "    %s\n", string_cstr(&err));
    CHECK(ok);
    CHECK_EQ(stats.files, 2);
    String f1 = path_join(string_sv(&dest), SV("stored.txt")), f2 = path_join(string_sv(&dest), SV("dir/deflated.txt"));
    String t1 = { 0 }, t2 = { 0 };
    CHECK(os_read_file(string_sv(&f1), &t1) && sv_eq(string_sv(&t1), SV("hello stored\n")));
    CHECK(os_read_file(string_sv(&f2), &t2) && t2.len == 2200 && sv_starts_with(string_sv(&t2), SV("deflate me deflate me")));
    // Zip-slip: an entry named "../evil" must be refused.
    static const unsigned char EVIL[] = {
        'P','K',3,4, 10,0, 0,0, 0,0, 0,0,0,0, 0,0,0,0, 1,0,0,0, 1,0,0,0, 7,0, 0,0, '.','.','/','e','v','i','l', 'x',
        'P','K',1,2, 20,0, 10,0, 0,0, 0,0, 0,0,0,0, 0,0,0,0, 1,0,0,0, 1,0,0,0, 7,0, 0,0, 0,0, 0,0, 0,0, 0,0,0,0, 0,0,0,0,
        '.','.','/','e','v','i','l',
        'P','K',5,6, 0,0, 0,0, 1,0, 1,0, 53,0,0,0, 38,0,0,0, 0,0 };
    string_free(&err);
    CHECK(!zip_extract_memory(sv_from_parts((const char *)EVIL, sizeof(EVIL)), string_sv(&dest), &stats, &err));
    CHECK(sv_find(string_sv(&err), SV("escapes")) >= 0);
    string_free(&err);
    string_free(&t1);
    string_free(&t2);
    string_free(&f1);
    string_free(&f2);
    string_free(&zip);
    string_free(&dest);
}

void register_installer_tests(void) {
    test_register("installer/pick_engine", test_pick_engine);
    test_register("installer/pick_human", test_pick_human);
    test_register("installer/strongest_network", test_strongest_network);
    test_register("installer/network_families", test_network_families);
    test_register("installer/zip", test_zip);
}
