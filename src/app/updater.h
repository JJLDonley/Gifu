// updater.h - checks GitHub for a newer Gifu and installs it.
//
// A worker thread checks the releases (app/update.c picks one for this platform
// and channel), downloads the archive, verifies it against the release's
// SHA256SUMS and unpacks it into <install>/.update/new. Applying (on the main
// thread, when the user agrees) swaps each top-level item of the new package in
// (the replaced ones go to <install>/.update/old; katago/ is never touched),
// rolling back if anything fails, then starts the new Gifu. The next start
// removes <install>/.update. An AppImage replaces its own file instead.
#pragma once

#include "app/update.h"

typedef enum UpdaterPhase {
    UPDATER_IDLE,
    UPDATER_CHECKING,
    UPDATER_UP_TO_DATE,
    UPDATER_AVAILABLE,     // a newer release on the channel; waiting for updater_install
    UPDATER_DOWNLOADING,
    UPDATER_VERIFYING,
    UPDATER_READY,         // unpacked and verified; waiting for updater_apply
    UPDATER_FAILED,
} UpdaterPhase;

typedef struct UpdaterStatus {
    UpdaterPhase phase;
    String version, notes, page_url;   // the newer release on the channel, if any (owned copies)
    ReleaseVec releases;               // every release for this platform, newest first (owned copies)
    String installing;                 // the version being downloaded / ready
    i64 done, total;                   // download progress (bytes)
    String error;
    bool can_install;                  // a packaged install (not a build folder) that can be written
} UpdaterStatus;

typedef struct Updater Updater;

// install_dir: the folder of the running program. appimage: $APPIMAGE or "".
// api_url: GitHub's /releases endpoint (config updates.url).
Updater *updater_create(StringView install_dir, StringView appimage, StringView api_url);
void updater_check(Updater *u, UpdateChannel channel);   // starts a check (async)
// Downloads, verifies and unpacks release `version` from the list (async; any
// version, older ones too). Not while checking or downloading.
void updater_install(Updater *u, StringView version);
void updater_status(Updater *u, UpdaterStatus *out);     // a snapshot; free with updater_status_free
void updater_status_free(UpdaterStatus *s);
// READY -> swaps the files in and starts the new version (with `restart_args` and
// "--updated-from <this pid>"). True: the caller should quit now.
bool updater_apply(Updater *u, const StringView *restart_args, isize restart_arg_count, String *error);
void updater_destroy(Updater *u);

// Removes what a previous update left behind (call at startup). `previous_pid`
// (from --updated-from): the old version's process, which may still be exiting and
// holding its executable on Windows: waited for first (on a background thread).
void updater_cleanup(StringView install_dir, i64 previous_pid);
// The program file inside an install folder ("gifu.exe" / "gifu").
StringView updater_program_name(void);
