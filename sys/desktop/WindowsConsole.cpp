#include "WindowsConsole.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdio>
#include <iostream>

namespace ekron::desktop {

namespace {

bool usable(DWORD which) {
    const HANDLE h = GetStdHandle(which);
    return h && h != INVALID_HANDLE_VALUE && GetFileType(h) != FILE_TYPE_UNKNOWN;
}

} // namespace

void attachParentConsole() {
    const bool out = usable(STD_OUTPUT_HANDLE), err = usable(STD_ERROR_HANDLE);
    if (out && err) return; // redirected, or already a console program
    if (!AttachConsole(ATTACH_PARENT_PROCESS)) return; // started from Explorer: no console
    FILE *f = nullptr;
    if (!out) freopen_s(&f, "CONOUT$", "w", stdout);
    if (!err) freopen_s(&f, "CONOUT$", "w", stderr);
    std::cout.clear();
    std::cerr.clear();
    std::clog.clear();
}

} // namespace ekron::desktop
