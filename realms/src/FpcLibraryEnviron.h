// FPC's Android RTL (CommonMainAndroid, rtl/android/sysandroid.inc) looks for
// argc/argv by scanning backwards from environ. That only works while environ
// is still the startup block on the stack; after setenv() it is a heap array
// and the scan reads out of bounds. Before loading an FPC library, point
// environ at a copy laid out like the startup block. The copy is never freed
// because the RTL keeps the pointer.
#pragma once

#if defined(__ANDROID__)
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
extern "C" char **environ; // not declared by macOS <unistd.h> (host test)
#endif

namespace realms {
namespace detail {

#if defined(__ANDROID__)
class FpcLibraryEnviron {
public:
    FpcLibraryEnviron() {
        static std::atomic<char **> installed{nullptr};
        if (environ && environ == installed.load()) return;
        char **env = environ;
        size_t count = 0;
        if (env)
            while (env[count]) ++count;
        // argc (0), argv terminator, envp..., NULL
        auto **block = static_cast<char **>(std::calloc(count + 3, sizeof(char *)));
        if (!block) return;
        for (size_t i = 0; i < count; ++i) {
            char *copy = strdup(env[i]);
            block[2 + i] = copy ? copy : env[i];
        }
        installed.store(block + 2);
        environ = block + 2;
    }
    ~FpcLibraryEnviron() {}
    FpcLibraryEnviron(const FpcLibraryEnviron &) = delete;
    FpcLibraryEnviron &operator=(const FpcLibraryEnviron &) = delete;
};
#else
class FpcLibraryEnviron {
public:
    FpcLibraryEnviron() {}
    ~FpcLibraryEnviron() {} // non-trivial, so the unused guard doesn't warn
    FpcLibraryEnviron(const FpcLibraryEnviron &) = delete;
    FpcLibraryEnviron &operator=(const FpcLibraryEnviron &) = delete;
};
#endif

} // namespace detail
} // namespace realms
