// theme.h - data-driven visual themes: key=value files (assets/themes/*.theme,
// plus user themes in <config_dir>/themes). `extends = id` inherits another
// theme (by file stem) and overrides only the keys it sets.
#pragma once

#include "base/kv.h"
#include "base/os.h"

#include <raylib.h>

typedef struct ThemeColors {
    Color background, surface, surface_alt, border;
    Color text, text_muted, accent, accent_text, hover, danger;
    Color board, board_line, board_label;
    Color black_stone, white_stone, black_stone_edge, white_stone_edge, last_move;
    Color winrate_black, winrate_white, graph_line, score_line;
    Color candidate_best, candidate_good, candidate_bad, candidate_text;
    Color human_policy;   // human SL network move rings
} ThemeColors;

typedef enum BackgroundKind { BACKGROUND_SOLID, BACKGROUND_IMAGE, BACKGROUND_PAPER } BackgroundKind;
typedef enum BackgroundFit { BACKGROUND_FIT_COVER, BACKGROUND_FIT_STRETCH, BACKGROUND_FIT_TILE } BackgroundFit;
typedef enum BoardStyle { BOARD_STYLE_FLAT, BOARD_STYLE_WOOD, BOARD_STYLE_PAPER, BOARD_STYLE_IMAGE } BoardStyle;
typedef enum StoneStyle { STONE_STYLE_FLAT, STONE_STYLE_GLOSSY, STONE_STYLE_INK } StoneStyle;

typedef struct PaperParams {
    f32 grain;      // per-pixel noise amplitude
    f32 fibers;     // streaky fiber texture strength
    f32 vignette;   // edge darkening
    i32 stains;     // number of coffee rings
    Color stain_color;
    u32 seed;
} PaperParams;

typedef struct Theme {
    String id;
    String name;
    bool dark;
    ThemeColors colors;
    struct {
        BackgroundKind kind;
        String image;   // path relative to assets/ or absolute
        BackgroundFit fit;
        f32 opacity;
        PaperParams paper;
    } background;
    struct {
        BoardStyle style;
        String texture;
        f32 line_width;
        bool shadow;
    } board;
    struct {
        StoneStyle style;
        f32 shadow;
    } stones;
    struct {
        f32 radius;
        f32 border_width;
        f32 gap;
        f32 padding;
    } shape;
    struct {
        String ui, ui_bold, mono;
    } fonts;
} Theme;

typedef Vec(Theme) ThemeVec;

Theme theme_fallback(void);
void theme_free(Theme *theme);
Theme theme_clone(const Theme *theme);
// Applies every key of a theme file (warnings name `source` and the line).
void theme_apply_kv(Theme *theme, const KvFile *kv, StringView source);
// Loads every *.theme in each dir (later dirs override same ids). Resolves "extends".
void themes_load(ThemeVec *out, const StringView *dirs, isize dir_count);
void themes_free(ThemeVec *themes);
isize themes_find(const ThemeVec *themes, StringView id);

NODISCARD bool color_parse(StringView text, Color *out);   // #RGB, #RRGGBB, #RRGGBBAA
Color color_with_alpha(Color c, f32 alpha);
Color color_lerp(Color a, Color b, f32 t);
