// Realms Engine - shared sandbox boot sequence (see SandboxBoot.h).

#include "realms/SandboxBoot.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace realms::sandbox {

void registerSharedCvars(Engine &engine, const std::string &platform) {
    engine.registerCvar("sensitivity", "3", REALMS_CVAR_ARCHIVE);
    engine.registerCvar("m_invert", "0", REALMS_CVAR_ARCHIVE);
    engine.registerCvar("name", "Player", REALMS_CVAR_ARCHIVE | REALMS_CVAR_USERINFO);
    engine.registerCvar("realms_platform", platform.empty() ? "unknown" : platform, REALMS_CVAR_NOSET);
    engine.forceSetCvar("realms_platform", platform.empty() ? "unknown" : platform);
}

int boot(Engine &engine, const BootOptions &options) {
    registerSharedCvars(engine, options.platform);

    int paks = 0;
    for (int i = 0; i < 10; ++i) {
        const std::string pak = "pak" + std::to_string(i) + ".pak";
        if (options.fileExists && options.fileExists(pak) && engine.addPak(pak) >= 0) ++paks;
    }

    std::vector<unsigned char> probe;
    if (engine.loadFile("config/realms.cfg", probe)) engine.addText("exec config/realms.cfg\n");
    if (options.execUserConfig) {
        if (engine.loadFile("config.cfg", probe)) engine.addText("exec config.cfg\n");
        if (engine.loadFile("autoexec.cfg", probe)) engine.addText("exec autoexec.cfg\n");
    }
    if (!options.commandLine.empty()) engine.addText(options.commandLine);
    engine.executeBuffer();
    return paks;
}

void saveUserConfig(Engine &engine) {
    if (engine.initialized()) engine.executeNow("writeconfig config.cfg");
}

float lookScale(const Engine &engine) {
    const float s = engine.cvarFloat("sensitivity", 3.0f);
    return std::clamp(std::isfinite(s) ? s : 3.0f, 0.05f, 50.0f) / 3.0f;
}

float pitchSign(const Engine &engine) {
    return engine.cvarBool("m_invert") ? -1.0f : 1.0f;
}

} // namespace realms::sandbox
