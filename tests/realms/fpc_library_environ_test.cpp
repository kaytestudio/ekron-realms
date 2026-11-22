// Runs FPC's Android argc/argv scan (CommonMainAndroid, FPC 3.2.2) on the
// host against FpcLibraryEnviron after setenv().
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#if defined(_WIN32)
int main() {
    std::printf("[fpc-environ] skipped: POSIX only\n");
    return 77;
}
#else
#include <unistd.h>

#define __ANDROID__ 1
#include "FpcLibraryEnviron.h"
#undef __ANDROID__

namespace {

int gChecks = 0, gFailures = 0;
#define CHECK(cond)                                                                 \
    do {                                                                            \
        ++gChecks;                                                                  \
        if (!(cond)) {                                                              \
            ++gFailures;                                                            \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);             \
        }                                                                           \
    } while (0)

struct FpcStartup {
    char **envp = nullptr;
    intptr_t argc = 0;
    char **argv = nullptr;
};

FpcStartup commonMainAndroid() {
    FpcStartup s;
    char **p = environ;
    if (!p) return s;
    s.envp = p;
    --p;
    if (*p == nullptr) {
        for (intptr_t i = 0; i < 200; ++i) {
            --p;
            if (reinterpret_cast<intptr_t>(*p) == i) {
                s.argc = i;
                s.argv = p + 1;
                break;
            }
        }
    }
    return s;
}

const char *find(char **env, const char *name) {
    const size_t n = std::strlen(name);
    for (char **e = env; e && *e; ++e)
        if (!std::strncmp(*e, name, n) && (*e)[n] == '=') return *e + n + 1;
    return nullptr;
}

} // namespace

int main() {
    // moves environ to the heap
    CHECK(setenv("REALMS_FPC_ENVIRON_TEST", "game", 1) == 0);
    char **before = environ;
    size_t count = 0;
    while (before[count]) ++count;
    FpcStartup s;
    {
        const realms::detail::FpcLibraryEnviron guard;
        CHECK(environ != before);
        s = commonMainAndroid();
    }
    CHECK(s.envp == environ);
    CHECK(s.argc == 0);
    CHECK(s.argv && s.argv[0] == nullptr);
    size_t copied = 0;
    while (s.envp && s.envp[copied]) ++copied;
    CHECK(copied == count);
    const char *v = find(s.envp, "REALMS_FPC_ENVIRON_TEST");
    CHECK(v && !std::strcmp(v, "game"));

    // unchanged environ: block reused
    {
        const realms::detail::FpcLibraryEnviron guard;
        CHECK(environ == s.envp);
        CHECK(commonMainAndroid().argc == 0);
    }

    CHECK(setenv("REALMS_FPC_ENVIRON_TEST", "changed", 1) == 0);
    const char *now = std::getenv("REALMS_FPC_ENVIRON_TEST");
    CHECK(now && !std::strcmp(now, "changed"));
    CHECK(setenv("REALMS_FPC_ENVIRON_ADDED", "1", 1) == 0);
    CHECK(std::getenv("REALMS_FPC_ENVIRON_ADDED") != nullptr);
    CHECK(std::getenv("REALMS_FPC_ENVIRON_TEST") != nullptr);

    // environ moved again: new block
    {
        const realms::detail::FpcLibraryEnviron guard;
        const FpcStartup again = commonMainAndroid();
        CHECK(again.argc == 0 && again.argv && again.argv[0] == nullptr);
        CHECK(find(again.envp, "REALMS_FPC_ENVIRON_ADDED") != nullptr);
    }

    std::printf("[fpc-environ] %d checks, %d failures\n", gChecks, gFailures);
    return gFailures ? 1 : 0;
}
#endif
