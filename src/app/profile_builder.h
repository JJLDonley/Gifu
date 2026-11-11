// profile_builder.h - builds the user's profile from a folder of games.
//
// A background thread finds the SGF files in the folder (and its subfolders)
// where Black or White is one of the user's names, analyses the ones without a
// fresh cached analysis (Gifu's own `--analyze`, run as a child process, up to
// PROFILE_CHUNK games per run), keeps the analysed copies in the cache folder
// (one file per game, named by a hash of its path; reused while newer than the
// game), and adds up the reports (core/profile.h).
#pragma once

#include "base/str.h"
#include "base/vec.h"
#include "core/profile.h"

enum { PROFILE_CHUNK = 20 };

typedef struct ProfileGame {
    String path;           // the user's file
    String date;           // SGF DT
    String opponent;
    Stone color;           // the user's colour
    ProfileResult result;
    f32 accuracy;          // 0..100, the user's
    i32 moves;             // the user's analysed moves
} ProfileGame;

typedef Vec(ProfileGame) ProfileGameVec;

typedef struct ProfileData {
    ProfileTotals totals;
    ProfileGameVec games;   // newest first (by SGF date)
    i32 skipped;            // matching games that could not be analysed
} ProfileData;

void profile_data_free(ProfileData *d);

typedef enum ProfilePhase { PROFILE_IDLE, PROFILE_SCANNING, PROFILE_ANALYSING, PROFILE_ADDING, PROFILE_DONE, PROFILE_FAILED } ProfilePhase;

typedef struct ProfileStatus {
    ProfilePhase phase;
    i32 done, total;       // games analysed so far / to analyse (ANALYSING), added / matching (ADDING)
    char current[160];     // the game being worked on
    char error[200];
} ProfileStatus;

typedef struct ProfileBuilder ProfileBuilder;

typedef struct ProfileBuildSpec {
    StringView folder;      // the user's games
    StringView names;       // comma-separated player names
    StringView program;     // this Gifu (runs `--analyze`)
    StringView cache_dir;   // <config_dir>/profile
    i32 visits;             // analysis.game_visits
} ProfileBuildSpec;

ProfileBuilder *profile_builder_start(const ProfileBuildSpec *spec);
ProfileStatus profile_builder_status(ProfileBuilder *b);
// Once DONE: moves the result out (true once).
bool profile_builder_take(ProfileBuilder *b, ProfileData *out);
void profile_builder_cancel(ProfileBuilder *b);
void profile_builder_destroy(ProfileBuilder *b);   // cancels and joins
