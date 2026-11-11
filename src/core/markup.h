// markup.h - marks drawn on a position (pure; unit-tested).
//
// Standard SGF, readable by other programs:
//   TR SQ CR MA   triangle / square / circle / cross on points ("aa" or "aa:cc" ranges)
//   LB            labels  "pt:text"
//   AR LN         arrows / lines "pt:pt"
//   TB TW         black / white territory
// CataGui only:
//   CGS[rrggbb:pt]...            area shading, any colour
//   CGP[rrggbb:x,y x,y ...]      freehand pen strokes (board coordinates, 0 = first line)
// A node's markup is read into a Markup, edited, and written back as a whole.
#pragma once

#include "core/game_tree.h"

typedef enum MarkShape { MARK_NONE, MARK_TRIANGLE, MARK_SQUARE, MARK_CIRCLE, MARK_CROSS, MARK_SHAPES } MarkShape;

enum { MARKUP_MAX_LABEL = 8, MARKUP_MAX_SEGMENTS = 128, MARKUP_MAX_STROKES = 64, MARKUP_MAX_STROKE_POINTS = 256 };

typedef struct MarkLabel {
    char text[MARKUP_MAX_LABEL];   // empty = none
} MarkLabel;

typedef struct MarkSegment {
    Point from, to;
} MarkSegment;

typedef struct PenStroke {
    u32 rgb;                       // 0xRRGGBB
    i32 count;
    f32 points[MARKUP_MAX_STROKE_POINTS][2];   // board coordinates
} PenStroke;

typedef struct Markup {
    i32 width, height;
    u8 shape[BOARD_MAX_POINTS];          // MarkShape
    MarkLabel label[BOARD_MAX_POINTS];
    u8 territory[BOARD_MAX_POINTS];      // Stone (TB / TW)
    bool shaded[BOARD_MAX_POINTS];
    u32 shade[BOARD_MAX_POINTS];         // 0xRRGGBB where shaded
    MarkSegment arrows[MARKUP_MAX_SEGMENTS];
    i32 arrow_count;
    MarkSegment lines[MARKUP_MAX_SEGMENTS];
    i32 line_count;
    PenStroke *strokes;                  // heap (markup_free)
    i32 stroke_count;
} Markup;

void markup_read(const GameNode *node, i32 width, i32 height, Markup *out);   // node may be NULL (empty)
void markup_write(GameTree *tree, GameNode *node, const Markup *markup);      // replaces the node's markup properties
void markup_free(Markup *markup);
bool markup_is_empty(const Markup *markup);

// Editing helpers.
bool markup_toggle_segment(MarkSegment *list, i32 *count, Point from, Point to);   // adds, or removes an equal one
PenStroke *markup_add_stroke(Markup *markup, u32 rgb);
void markup_erase_at(Markup *markup, Point p);   // every mark on / through that point
void markup_clear(Markup *markup);
// Next free label: "A".."Z" then "a".."z" (letters), or "1".. (numbers).
void markup_next_label(const Markup *markup, bool numbers, char out[MARKUP_MAX_LABEL]);

// Shaded points per colour (up to `cap` colours, most points first). With
// `empty_only`, points with a stone on `board` are not counted.
typedef struct ShadeCount {
    u32 rgb;
    i32 points;
} ShadeCount;
i32 markup_shade_counts(const Markup *markup, const Board *board, bool empty_only, ShadeCount *out, i32 cap);
// Empty points connected to `p` (4-neighbours) on `board`: the region a fill covers. Returns the count.
i32 markup_empty_region(const Board *board, Point p, bool *out_region);
