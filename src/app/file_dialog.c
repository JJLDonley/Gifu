#include "app/file_dialog.h"

#include "base/os.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(OS_WINDOWS)
#include <windows.h>
#include <commdlg.h>

static bool run_dialog(bool save, StringView initial, String *out_path) {
    wchar_t buffer[4096] = { 0 };
    if (save && initial.len) {
        wchar_t *w = os_utf8_to_wide(initial);
        wcsncpy(buffer, w, countof(buffer) - 1);
        free(w);
    }
    wchar_t *dir = !save && initial.len ? os_utf8_to_wide(initial) : NULL;
    OPENFILENAMEW ofn = { 0 };
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = GetActiveWindow();
    ofn.lpstrFilter = L"SGF files (*.sgf)\0*.sgf\0All files\0*.*\0";
    ofn.lpstrFile = buffer;
    ofn.nMaxFile = (DWORD)countof(buffer);
    ofn.lpstrInitialDir = dir;
    ofn.lpstrDefExt = L"sgf";
    ofn.Flags = OFN_NOCHANGEDIR | (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST);
    BOOL ok = save ? GetSaveFileNameW(&ofn) : GetOpenFileNameW(&ofn);
    free(dir);
    if (!ok) return false;
    *out_path = os_wide_to_utf8(buffer);
    return true;
}

bool file_dialog_available(void) { return true; }
bool file_dialog_open_sgf(StringView start_dir, String *out_path) { return run_dialog(false, start_dir, out_path); }
bool file_dialog_save_sgf(StringView suggested_path, String *out_path) { return run_dialog(true, suggested_path, out_path); }

#else

static bool have_program(const char *name) {
    String cmd = string_format("command -v %s >/dev/null 2>&1", name);
    int rc = system(string_cstr(&cmd));
    string_free(&cmd);
    return rc == 0;
}

// Single-quotes `s` for /bin/sh.
static void push_shell_quoted(String *cmd, StringView s) {
    string_push_char(cmd, '\'');
    for (isize i = 0; i < s.len; ++i) {
        if (s.data[i] == '\'') string_push(cmd, SV("'\\''"));
        else string_push_char(cmd, s.data[i]);
    }
    string_push_char(cmd, '\'');
}

static bool run_capture(String *cmd, String *out_path) {
    FILE *pipe = popen(string_cstr(cmd), "r");
    if (!pipe) return false;
    char buffer[4096];
    String result = { 0 };
    while (fgets(buffer, sizeof(buffer), pipe)) string_push_cstr(&result, buffer);
    int rc = pclose(pipe);
    StringView path = sv_trim(string_sv(&result));
    bool ok = rc == 0 && path.len > 0;
    if (ok) *out_path = string_from(path);
    string_free(&result);
    return ok;
}

bool file_dialog_available(void) { return have_program("zenity") || have_program("kdialog"); }

bool file_dialog_open_sgf(StringView start_dir, String *out_path) {
    String cmd = { 0 };
    if (have_program("zenity")) {
        string_push(&cmd, SV("zenity --file-selection --title='Open SGF' --file-filter='SGF files | *.sgf' --filename="));
        String dir = string_format(SV_FMT "/", SV_ARG(start_dir));
        push_shell_quoted(&cmd, string_sv(&dir));
        string_free(&dir);
    } else if (have_program("kdialog")) {
        string_push(&cmd, SV("kdialog --getopenfilename "));
        push_shell_quoted(&cmd, start_dir.len ? start_dir : SV("."));
        string_push(&cmd, SV(" 'SGF files (*.sgf)'"));
    } else {
        return false;
    }
    string_push(&cmd, SV(" 2>/dev/null"));
    bool ok = run_capture(&cmd, out_path);
    string_free(&cmd);
    return ok;
}

bool file_dialog_save_sgf(StringView suggested_path, String *out_path) {
    String cmd = { 0 };
    if (have_program("zenity")) {
        string_push(&cmd, SV("zenity --file-selection --save --confirm-overwrite --title='Save SGF' --filename="));
        push_shell_quoted(&cmd, suggested_path);
    } else if (have_program("kdialog")) {
        string_push(&cmd, SV("kdialog --getsavefilename "));
        push_shell_quoted(&cmd, suggested_path);
        string_push(&cmd, SV(" 'SGF files (*.sgf)'"));
    } else {
        return false;
    }
    string_push(&cmd, SV(" 2>/dev/null"));
    bool ok = run_capture(&cmd, out_path);
    string_free(&cmd);
    return ok;
}
#endif
