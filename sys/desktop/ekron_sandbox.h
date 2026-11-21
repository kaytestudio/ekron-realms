// Ekron Realms - libekron_sandbox: the C++ desktop sandbox as a library.
//
// The desktop client (sys/desktop/main.cpp: SDL2 window, input, GL renderer,
// audio, world) normally builds as the `ekron-realms` executable. With
// -DENABLE_FPC_CLIENT=ON and Free Pascal installed, the same sources are also
// built as libekron_sandbox.so / .dylib so that another program can own the
// process and run the sandbox in it. The Free Pascal `realms-pascal` program
// (Projects/realms.lpr) does exactly that, and passes in the Realms core it
// compiles itself (realms/pascal), so one native binary runs the Pascal game
// core with the C++ SDL2 platform layer.
#pragma once

#include "realms/realms_bridge.h"

#ifdef __cplusplus
extern "C" {
#endif

// Runs the sandbox exactly like `ekron-realms argv[1..]` (same options:
// --version, --headless-smoke, --engine-info, --assets <dir>, +commands...).
// argv[0] should be the caller's executable path: assets are looked up next to
// it. `pascalCore` may be NULL; otherwise the Realms engine uses that core
// (origin "linked") instead of loading librealms_core. Returns the exit code.
int ekron_sandbox_run(int argc, char **argv, RealmsGetApiFn pascalCore);

#ifdef __cplusplus
}
#endif
