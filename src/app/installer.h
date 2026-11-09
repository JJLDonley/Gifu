// installer.h - downloads KataGo, the strongest kata1 network and the human SL
// network into <root>/katago (the layout the shipped engine profiles expect):
//
//   katago/<platform>-<backend>/katago[.exe] + cfgs     engine (release zip)
//   katago/networks/kata1-....bin.gz                    strongest network
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
String katago_network_url(StringView name);

// ---- What is installed ---------------------------------------------------------------------
typedef struct InstallStatus {
    bool engine, network, human;
} InstallStatus;
InstallStatus installer_detect(StringView katago_dir, const KataGoBackend *backend);

// ---- Background installer ------------------------------------------------------------------
typedef struct InstallRequest {
    String katago_dir;
    const KataGoBackend *backend;
    bool engine, network, human;
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

void installer_start(Installer *in, StringView katago_dir, const KataGoBackend *backend, bool engine, bool network, bool human);
InstallState installer_state(Installer *in);
void installer_cancel(Installer *in);
void installer_finish(Installer *in);   // joins the thread (call once finished or after cancel)
