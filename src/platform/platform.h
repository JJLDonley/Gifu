// platform.h - the platform layer: everything Gifu needs from the operating
// system besides what raylib already covers (window, input, OpenGL, clipboard,
// drag and drop). Nothing here includes raylib; it sits beside base (strings, logs)
// and under core / engine / app / ui.
//
//   os.h              files, folders, paths, time, the program's and the config folder,
//                     opening URLs, detached processes (restart after an update)
//   thread.h          threads, mutexes
//   process.h         child processes with pipes (engines, tools)
//   line_channel.h    a child process spoken to in lines (portable, on process.h)
//   text_encoding.h   legacy SGF charsets -> UTF-8
//   screen_capture.h  grabbing the screen (board sync)
//   file_dialog.h     native open / save dialogs
//
// Implementations, one file per system (CMake picks them):
//   platform_common.c   portable parts (paths, trees, tool-based capture...)
//   platform_windows.c  Win32
//   platform_posix.c    shared by Linux and macOS
//   platform_linux.c    Linux only (/proc, XDG, X11 / Wayland capture, zenity)
//   platform_macos.c    macOS only (Mach-O paths, Application Support, screencapture, osascript)
// A new port implements what platform_posix.c / platform_linux.c do for it.
// tests/test_platform.c checks the behaviour every system must share.
#pragma once

#include "platform/file_dialog.h"
#include "platform/line_channel.h"
#include "platform/os.h"
#include "platform/process.h"
#include "platform/screen_capture.h"
#include "platform/text_encoding.h"
#include "platform/thread.h"
