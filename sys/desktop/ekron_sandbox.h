// The desktop sandbox built as a library (libekron_sandbox, with
// -DENABLE_FPC_CLIENT=ON), so realms-pascal (Projects/realms.lpr) can run it
// with its own linked Pascal core.
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
