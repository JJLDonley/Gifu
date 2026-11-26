// area_select.c - `gifu --select-area X,Y,W,H [--select-hint TEXT]`.
//
// A borderless, always-on-top window over that desktop rectangle showing a
// snapshot of the screen taken just before: the user drags a box around the
// board of another program on the frozen picture. (Over the live screen, a
// window covering everything makes browsers think they are hidden and stop
// drawing their videos.) Without a snapshot the window is transparent. Prints "x,y,w,h" (desktop coordinates) and exits 0; Escape or a
// click without dragging cancels (exit 1). Run as its own process by the app,
// so its window flags never touch the main window.
#include "ui/area_select.h"

#include "raylib.h"

#include <stdio.h>

int area_select_run(i32 x, i32 y, i32 width, i32 height, const char *hint, const RgbaImage *snapshot) {
    SetTraceLogLevel(LOG_WARNING);
    u32 flags = FLAG_WINDOW_UNDECORATED | FLAG_WINDOW_TOPMOST | FLAG_VSYNC_HINT;
    SetConfigFlags(snapshot ? flags : flags | FLAG_WINDOW_TRANSPARENT);
    InitWindow(width, height, "Gifu - select the board");
    SetWindowPosition(x, y);
    SetExitKey(KEY_NULL);
    SetTargetFPS(60);
    Texture2D picture = { 0 };
    if (snapshot) {
        Image image = { snapshot->pixels, snapshot->width, snapshot->height, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
        picture = LoadTextureFromImage(image);
        SetTextureFilter(picture, TEXTURE_FILTER_BILINEAR);   // a scaled capture (Wayland HiDPI) stays smooth
    }
    const Color accent = { 236, 140, 40, 255 }, dim = { 0, 0, 0, 70 };
    bool dragging = false;
    Vector2 a = { 0 }, b = { 0 };
    int result = 1;
    while (!WindowShouldClose()) {
        if (IsKeyPressed(KEY_ESCAPE)) break;
        Vector2 m = GetMousePosition();
        SetMouseCursor(MOUSE_CURSOR_CROSSHAIR);
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) dragging = true, a = b = m;
        if (dragging) b = m;
        Rectangle r = { a.x < b.x ? a.x : b.x, a.y < b.y ? a.y : b.y, a.x < b.x ? b.x - a.x : a.x - b.x, a.y < b.y ? b.y - a.y : a.y - b.y };
        if (dragging && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
            dragging = false;
            if (r.width >= 40 && r.height >= 40) {
                printf("%d,%d,%d,%d\n", x + (int)r.x, y + (int)r.y, (int)r.width, (int)r.height);
                fflush(stdout);
                result = 0;
                break;
            }
        }
        BeginDrawing();
        ClearBackground(BLANK);
        if (picture.id) {   // the capture can be in other pixels than the window (Wayland scaling): stretch it
            DrawTexturePro(picture, (Rectangle){ 0, 0, (f32)picture.width, (f32)picture.height }, (Rectangle){ 0, 0, (f32)width, (f32)height },
                           (Vector2){ 0 }, 0.0f, WHITE);
        }
        if (dragging) {   // dim everything but the box
            DrawRectangle(0, 0, width, (int)r.y, dim);
            DrawRectangle(0, (int)(r.y + r.height), width, height - (int)(r.y + r.height), dim);
            DrawRectangle(0, (int)r.y, (int)r.x, (int)r.height, dim);
            DrawRectangle((int)(r.x + r.width), (int)r.y, width - (int)(r.x + r.width), (int)r.height, dim);
            DrawRectangleLinesEx(r, 2.0f, accent);
            DrawText(TextFormat("%d x %d", (int)r.width, (int)r.height), (int)r.x + 6, (int)(r.y + r.height) + 6, 20, accent);
        } else {
            DrawRectangle(0, 0, width, height, dim);
        }
        // The hint, on the monitor part nearest the top-left (one line, in a box).
        int tw = MeasureText(hint, 20);
        DrawRectangleRounded((Rectangle){ 24, 24, (f32)tw + 28, 40 }, 0.3f, 6, (Color){ 20, 20, 20, 220 });
        DrawText(hint, 38, 34, 20, RAYWHITE);
        EndDrawing();
    }
    if (picture.id) UnloadTexture(picture);
    CloseWindow();
    return result;
}
