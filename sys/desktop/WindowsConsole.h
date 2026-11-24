#pragma once
// Windows: ekron-realms.exe is a GUI-subsystem program (no console window
// when started from Explorer). When it is started from a terminal, its
// output (--help, --version, --engine-info, the log) goes back to that
// terminal; redirected output (pipes, files, CTest) is left alone.

namespace ekron::desktop {

#if defined(_WIN32)
void attachParentConsole();
#else
inline void attachParentConsole() {}
#endif

} // namespace ekron::desktop
