// os.h - the thin platform layer: files, directories, paths, time.
// All paths are UTF-8 (converted to UTF-16 internally on Windows).
#pragma once

#include "base/base.h"
#include "base/str.h"
#include "base/vec.h"

typedef Vec(String) StringVec;
void string_vec_free(StringVec *v);

NODISCARD bool os_read_file(StringView path, String *out);
NODISCARD bool os_write_file(StringView path, StringView data);
bool os_file_exists(StringView path);
bool os_dir_exists(StringView path);
bool os_make_dirs(StringView path);
// Lists entry names (not full paths) in `dir`, sorted. Returns false if missing.
bool os_list_dir(StringView dir, StringVec *out_names);

f64 os_time_seconds(void);   // monotonic
void os_sleep_ms(i32 ms);

String os_executable_dir(void);
String os_config_dir(void);  // %APPDATA%\CataGui, $XDG_CONFIG_HOME/catagui, ~/.config/catagui
const char *os_platform_name(void);   // "windows" | "linux" | "macos"

String path_join(StringView a, StringView b);
StringView path_basename(StringView path);
StringView path_dirname(StringView path);
StringView path_extension(StringView path);   // includes the dot, or empty

#if defined(OS_WINDOWS)
#include <wchar.h>
// Caller frees with free().
wchar_t *os_utf8_to_wide(StringView s);
String os_wide_to_utf8(const wchar_t *w);
#endif
