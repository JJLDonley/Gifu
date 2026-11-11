// installer.h - downloads KataGo, kata1 networks (one per chosen size) and the
// human SL network into <root>/katago (the layout the shipped engine profiles expect):
//
//   katago/<platform>-<backend>/katago[.exe] + cfgs     engine (release zip)
//   katago/networks/kata1-<family>-....bin.gz           networks
//   katago/networks/human/b18c384nbt-humanv0.bin.gz     human SL network
//
// Discovery is data-light: the newest GitHub release that has an asset for the
// chosen backend, and katagotraining.org's "strongest confidently-rated"
// network. Work runs on a background thread; the UI polls installer_state().
#pragma once

#include "base/str.h"
#include "base/thread.h"

typedef struct KataGoBackend {
    const char *id;       // asset tag, e.g. "opencl", "eigenavx2", "cuda12.8-cudnn9.8.0"
    const char *dir;      // install folder suffix, e.g. "opencl" -> katago/<platform>-opencl
    const char *label;
} KataGoBackend;

isize katago_backend_count(void);
const KataGoBackend *katago_backend_at(isize index);
const KataGoBackend *katago_backend_find(StringView id);

typedef struct DownloadAsset {
    String name;
    String url;
    String version;   // release tag
    i64 size;         // -1 if unknown
} DownloadAsset;
void download_asset_free(DownloadAsset *a);

// ---- Pure helpers (unit-tested) ------------------------------------------------------------
// From the GitHub releases API JSON (newest first): the first release with
// "katago-v<ver>-<backend>-<os>-x64.zip".
bool katago_pick_engine_asset(StringView releases_json, StringView backend, StringView os_name, DownloadAsset *out);
// The newest "*humanv*.bin.gz" release asset.
bool katago_pick_human_asset(StringView releases_json, DownloadAsset *out);
// From katagotraining.org/networks/: the strongest confidently-rated kata1 network name.
bool katago_strongest_network(StringView html, String *name);
// The highest-rated network of one architecture family (e.g. "b18c384nbt") with
// at least `min_games` rating games. Transformer nets (tf*) rate higher per
// visit but run an order of magnitude slower on OpenCL / CPU (13 vs 155
// visits/s on an Arc A770), so the installer asks for the fast convnet family.
bool katago_best_network(StringView html, StringView family, i64 min_games, String *name);
enum { KATAGO_NETWORK_MIN_GAMES = 1000 };
#define KATAGO_NETWORK_FAMILY "b18c384nbt"

// kata1 network families by size (blocks x channels), smallest first. Each can be
// downloaded (its best-rated network); the engine profiles pick one in Settings.
typedef struct NetworkFamily {
    const char *id;       // e.g. "b18c384nbt" (the file name's family part)
    const char *label;    // e.g. "18 blocks"
    const char *detail;   // speed / strength, size
} NetworkFamily;
isize katago_family_count(void);
const NetworkFamily *katago_family_at(isize index);
isize katago_family_find(StringView id);   // -1 if unknown
u32 katago_default_families(void);         // bit of KATAGO_NETWORK_FAMILY
String katago_network_url(StringView name);                       // the .bin.gz URL
String katago_network_url_in(StringView html, StringView name);   // as the networks page links it (.bin.gz or .txt.gz)

// ---- What is installed ---------------------------------------------------------------------
typedef struct InstallStatus {
    bool engine, network, human;   // network: any kata1 network
    bool leela;                    // katago/leela/leela[.exe] (the original Leela, an opponent)
    u32 families;                  // bit i: a network of katago_family_at(i) is installed
} InstallStatus;
InstallStatus installer_detect(StringView katago_dir, const KataGoBackend *backend);

// ---- Background installer ------------------------------------------------------------------
typedef struct InstallRequest {
    String katago_dir;
    const KataGoBackend *backend;
    bool engine, human;
    u32 networks;   // bit i: download the best network of katago_family_at(i)
    bool leela;     // the original Leela 0.11 (sjeng.org), checked against a pinned SHA-256
} InstallRequest;

typedef struct InstallState {
    bool running, finished, failed;
    char label[192];   // what is happening now
    i64 done, total;   // bytes of the current download (total -1 = unknown)
    char error[256];
} InstallState;

typedef struct Installer {
    Thread thread;
    Mutex lock;
    InstallState state;   // guarded by lock
    InstallRequest request;
    volatile bool cancel;
    bool started;
} Installer;

void installer_start(Installer *in, StringView katago_dir, const KataGoBackend *backend, bool engine, u32 networks, bool human, bool leela);

// The original Leela (Gian-Carlo Pascutto, 2017): the GTP build for this platform.
#define LEELA_ZIP_URL "https://www.sjeng.org/dl/Leela0110GTP.zip"
#define LEELA_ZIP_SHA256 "8c7d8c120ab8b940ded35c28f2a2d5b2b56448dbf9a9000f61c3de1da896c537"
enum { LEELA_ZIP_SIZE = 82629955 };
InstallState installer_state(Installer *in);
void installer_cancel(Installer *in);
void installer_finish(Installer *in);   // joins the thread (call once finished or after cancel)
