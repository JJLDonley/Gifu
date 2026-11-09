// file_dialog.h - native open/save dialogs.
// Windows: comdlg32. Linux: zenity or kdialog when installed. Returns false
// when cancelled or no dialog backend exists (the UI then suggests drag & drop).
#pragma once

#include "base/str.h"

bool file_dialog_open_sgf(StringView start_dir, String *out_path);
bool file_dialog_save_sgf(StringView suggested_path, String *out_path);
bool file_dialog_available(void);
