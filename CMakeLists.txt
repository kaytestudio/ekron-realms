cmake_minimum_required(VERSION 3.16...3.31)
project(EkronRealms VERSION 1.0.0 LANGUAGES C CXX)

if(MSVC)
    # Before any target: static CRT for everything (no Visual C++
    # redistributable needed to run the package), UTF-8 sources and
    # strings (localization), standard conformance, and big objects
    # (nlohmann/json).
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    add_compile_options(/utf-8 /bigobj /permissive- /Zc:__cplusplus)
    add_compile_definitions(NOMINMAX _CRT_SECURE_NO_WARNINGS)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

option(ENABLE_DESKTOP_SANDBOX "Build the native C++17 SDL2/OpenGL desktop sandbox client" ON)
option(ENABLE_FPC_CLIENT "Build the Free Pascal client (realms-pascal + libekron_sandbox) and launcher when fpc is installed" ON)
option(ENABLE_PASCAL_CORE "Build the Free Pascal Realms core (librealms_core, Quake 2 stack) when fpc is installed" ON)
option(ENABLE_GAME_MODULE "Build the Quake 2 game module (game/, librealms_game) for realms::GameServer when fpc is installed" ON)
option(ENABLE_EXTERNAL_TOOLS "Fetch and build external KayteIDE and KCC repositories" OFF)
option(ENABLE_KCC "Build the KCC compiler from tools/kcc (cloned from kaytestudio/kcc when missing)" OFF)

set(SHARED_CPP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/sys/android/app/src/main/cpp")
set(SHARED_ASSETS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/sys/android/app/src/main/assets")
set(DESKTOP_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/sys/desktop")
set(REALMS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/realms")

# =============================================================================
# Realms Engine: C++ bridge/facade (+ C++ reference core) and Free Pascal core
# =============================================================================
add_subdirectory("${REALMS_DIR}" realms)

if(ENABLE_FPC_CLIENT OR ENABLE_PASCAL_CORE OR ENABLE_GAME_MODULE)
    find_package(FPC QUIET)
endif()

set(REALMS_PASCAL_CORE_BUILT FALSE)
if(ENABLE_PASCAL_CORE AND FPC_FOUND)
    if(APPLE)
        set(REALMS_PASCAL_CORE_DIR "${CMAKE_CURRENT_BINARY_DIR}/EkronRealms.app/Contents/Frameworks")
        set(_realms_fpc_flags -Mobjfpc -Sh -O2)
    else()
        set(REALMS_PASCAL_CORE_DIR "${CMAKE_CURRENT_BINARY_DIR}")
        set(_realms_fpc_flags -Mobjfpc -Sh -O2 -Xs)
    endif()
    add_fpc_library(realms_pascal_core
        SOURCE "${REALMS_DIR}/pascal/realms_core.lpr"
        NAME realms_core
        OUTPUT_DIRECTORY "${REALMS_PASCAL_CORE_DIR}"
        COMPILE_FLAGS ${_realms_fpc_flags}
    )
    get_target_property(REALMS_PASCAL_CORE_PATH realms_pascal_core FPC_OUTPUT_PATH)
    set(REALMS_PASCAL_CORE_BUILT TRUE)
    message(STATUS "=== Realms Pascal core: ${REALMS_PASCAL_CORE_PATH} ===")
elseif(ENABLE_PASCAL_CORE)
    message(STATUS "Free Pascal Compiler (fpc) not found; Realms will use its C++ reference core.")
endif()

# The Quake 2 game module (game/, the Free Pascal port of gamex86): Quake 2
# game API v3 behind realms/include/realms/q2game.h, hosted in the C++
# sandbox by realms::GameServer (dlopen + GetGameAPI), next to the core.
set(REALMS_GAME_MODULE_BUILT FALSE)
if(ENABLE_GAME_MODULE AND FPC_FOUND)
    if(APPLE)
        set(REALMS_GAME_MODULE_DIR "${CMAKE_CURRENT_BINARY_DIR}/EkronRealms.app/Contents/Frameworks")
        set(_realms_game_flags -Mdelphi -dGAMEDLL -O2)
    else()
        set(REALMS_GAME_MODULE_DIR "${CMAKE_CURRENT_BINARY_DIR}")
        set(_realms_game_flags -Mdelphi -dGAMEDLL -O2 -Xs)
    endif()
    add_fpc_library(realms_game_module
        SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/game/realms_game.lpr"
        NAME realms_game
        OUTPUT_DIRECTORY "${REALMS_GAME_MODULE_DIR}"
        COMPILE_FLAGS ${_realms_game_flags}
    )
    get_target_property(REALMS_GAME_MODULE_PATH realms_game_module FPC_OUTPUT_PATH)
    set(REALMS_GAME_MODULE_BUILT TRUE)
    message(STATUS "=== Realms game module: ${REALMS_GAME_MODULE_PATH} ===")
elseif(ENABLE_GAME_MODULE)
    message(STATUS "Free Pascal Compiler (fpc) not found; skipping the Quake 2 game module (map mode runs without it).")
endif()

# =============================================================================
# Optional External Tools (KayteIDE / KCC)
# =============================================================================
if(ENABLE_EXTERNAL_TOOLS)
    find_package(KayteIDE REQUIRED)
endif()
if(ENABLE_EXTERNAL_TOOLS OR ENABLE_KCC)
    find_package(KCC REQUIRED)
endif()

# =============================================================================
# Locate SDL2 for Native Desktop Client
# =============================================================================
set(EKRON_SDL2_TARGET "")
if(ENABLE_DESKTOP_SANDBOX)
    find_package(SDL2 CONFIG QUIET)
    if(TARGET SDL2::SDL2)
        set(EKRON_SDL2_TARGET SDL2::SDL2)
    else()
        find_package(PkgConfig QUIET)
        if(PKG_CONFIG_FOUND)
            pkg_check_modules(PC_SDL2 QUIET sdl2)
        endif()
        find_path(EKRON_SDL2_INCLUDE_DIR
            NAMES SDL.h
            HINTS
                ${PC_SDL2_INCLUDE_DIRS}
                /opt/homebrew/include
                /usr/local/include
                /usr/include
            PATH_SUFFIXES SDL2
        )
        find_library(EKRON_SDL2_LIBRARY
            NAMES SDL2 SDL2-2.0
            HINTS
                ${PC_SDL2_LIBRARY_DIRS}
                /opt/homebrew/lib
                /usr/local/lib
                /usr/lib
        )
        if(EKRON_SDL2_INCLUDE_DIR AND EKRON_SDL2_LIBRARY)
            add_library(ekron_sdl2_imported INTERFACE)
            target_include_directories(ekron_sdl2_imported INTERFACE "${EKRON_SDL2_INCLUDE_DIR}")
            target_link_libraries(ekron_sdl2_imported INTERFACE "${EKRON_SDL2_LIBRARY}")
            set(EKRON_SDL2_TARGET ekron_sdl2_imported)
        else()
            message(FATAL_ERROR "SDL2 development headers/libraries not found. Install libsdl2-dev (Linux), brew install sdl2 (macOS), or on Windows mingw-w64-x86_64-SDL2 (MSYS2) / vcpkg sdl2, or pass -DSDL2_DIR=<SDL2 cmake dir>.")
        endif()
    endif()
endif()

# =============================================================================
# Native C++17 Ekron Urban Sandbox Desktop Client
# =============================================================================
if(ENABLE_DESKTOP_SANDBOX)
    find_package(Threads REQUIRED)

    # Everything but main.cpp, compiled once (PIC) for both the ekron-realms
    # executable and libekron_sandbox (the realms-pascal program's sandbox).
    set(DESKTOP_SOURCES
        "${DESKTOP_SRC_DIR}/DesktopRenderer.cpp"
        "${DESKTOP_SRC_DIR}/DesktopOnlineClient.cpp"
        "${DESKTOP_SRC_DIR}/ShopCatalog.cpp"
        "${DESKTOP_SRC_DIR}/Localization.cpp"
        "${DESKTOP_SRC_DIR}/DesktopConsole.cpp"
        "${DESKTOP_SRC_DIR}/DesktopMapMode.cpp"
        "${DESKTOP_SRC_DIR}/DesktopAudio.cpp"
        "${SHARED_CPP_DIR}/MapSession.cpp"
        "${SHARED_CPP_DIR}/SoundDecode.cpp"
        "${SHARED_CPP_DIR}/SoundMixer.cpp"
        "${SHARED_CPP_DIR}/SoundDirector.cpp"
        "${SHARED_CPP_DIR}/SoundSystem.cpp"
        "${SHARED_CPP_DIR}/GameSoundPlayer.cpp"
        "${SHARED_CPP_DIR}/Quake2View.cpp"
        "${SHARED_CPP_DIR}/GltfScene.cpp"
        "${SHARED_CPP_DIR}/ImageDecode.cpp"
        "${DESKTOP_SRC_DIR}/IntroScreen.cpp"
    )

    # Intro video (assets/intro.mp4) backend, see sys/desktop/IntroVideo.h:
    # AVFoundation on macOS, Media Foundation on Windows, FFmpeg's libraries
    # elsewhere when found, else none.
    option(EKRON_INTRO_VIDEO "Play the intro video at startup (AVFoundation / Media Foundation / FFmpeg)" ON)
    option(EKRON_REQUIRE_INTRO_VIDEO "Fail instead of building without an intro video backend (release builds)" OFF)
    set(EKRON_INTRO_BACKEND "none")
    if(EKRON_INTRO_VIDEO AND APPLE)
        set(CMAKE_OBJCXX_STANDARD 17)
        set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
        enable_language(OBJCXX)
        list(APPEND DESKTOP_SOURCES "${DESKTOP_SRC_DIR}/IntroVideoApple.mm")
        set_source_files_properties("${DESKTOP_SRC_DIR}/IntroVideoApple.mm" PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
        set(EKRON_INTRO_BACKEND "AVFoundation")
    elseif(EKRON_INTRO_VIDEO AND WIN32)
        list(APPEND DESKTOP_SOURCES "${DESKTOP_SRC_DIR}/IntroVideoMediaFoundation.cpp")
        set(EKRON_INTRO_BACKEND "MediaFoundation")
    elseif(EKRON_INTRO_VIDEO)
        find_package(PkgConfig QUIET)
        if(PKG_CONFIG_FOUND)
            pkg_check_modules(EKRON_FFMPEG QUIET IMPORTED_TARGET
                libavformat libavcodec>=59.37.100 libswscale libswresample libavutil)
        endif()
        if(EKRON_FFMPEG_FOUND)
            list(APPEND DESKTOP_SOURCES "${DESKTOP_SRC_DIR}/IntroVideoFFmpeg.cpp")
            set(EKRON_INTRO_BACKEND "FFmpeg")
        endif()
    endif()
    if(EKRON_INTRO_BACKEND STREQUAL "none")
        if(EKRON_REQUIRE_INTRO_VIDEO)
            message(FATAL_ERROR "EKRON_REQUIRE_INTRO_VIDEO: no intro video backend. Install FFmpeg 5.1+ development "
                                "files (libavformat-dev libavcodec-dev libswscale-dev libswresample-dev).")
        endif()
        list(APPEND DESKTOP_SOURCES "${DESKTOP_SRC_DIR}/IntroVideoNone.cpp")
        if(EKRON_INTRO_VIDEO)
            message(STATUS "FFmpeg 5.1+ development files not found: the desktop client starts without the intro video "
                           "(install libavformat-dev libavcodec-dev libswscale-dev libswresample-dev)")
        endif()
    endif()
    message(STATUS "Intro video backend: ${EKRON_INTRO_BACKEND}")

    if(WIN32)
        list(APPEND DESKTOP_SOURCES "${DESKTOP_SRC_DIR}/WindowsConsole.cpp")
    endif()

    add_library(ekron_sandbox_objects OBJECT ${DESKTOP_SOURCES})
    set_target_properties(ekron_sandbox_objects PROPERTIES POSITION_INDEPENDENT_CODE ON)
    target_include_directories(ekron_sandbox_objects PUBLIC
        "${DESKTOP_SRC_DIR}"
        "${SHARED_CPP_DIR}"
    )
    target_link_libraries(ekron_sandbox_objects PUBLIC
        realms_quake2
        ${EKRON_SDL2_TARGET}
        Threads::Threads
        $<$<PLATFORM_ID:Linux>:${CMAKE_DL_LIBS}>
    )

    if(EKRON_INTRO_BACKEND STREQUAL "AVFoundation")
        target_link_libraries(ekron_sandbox_objects PUBLIC
            "-framework AVFoundation" "-framework CoreMedia" "-framework CoreVideo" "-framework Foundation")
    elseif(EKRON_INTRO_BACKEND STREQUAL "MediaFoundation")
        # mfplat / mfreadwrite are loaded at run time (IntroVideoMediaFoundation.cpp).
        target_link_libraries(ekron_sandbox_objects PUBLIC mfuuid ole32)
    elseif(EKRON_INTRO_BACKEND STREQUAL "FFmpeg")
        target_link_libraries(ekron_sandbox_objects PUBLIC PkgConfig::EKRON_FFMPEG)
    endif()

    # Presence remains optional when OpenSSL development files are unavailable.
    if(UNIX)
        find_package(OpenSSL 1.1.1 QUIET)
        if(OpenSSL_FOUND)
            target_compile_definitions(ekron_sandbox_objects PUBLIC EKRON_TLS_ONLINE=1)
            target_link_libraries(ekron_sandbox_objects PUBLIC OpenSSL::SSL OpenSSL::Crypto)
        else()
            message(STATUS "OpenSSL development files not found: experimental online presence disabled")
        endif()
    endif()

    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(ekron_sandbox_objects PUBLIC -Wall -Wextra)
    endif()

    if(APPLE)
        add_executable(ekron-realms MACOSX_BUNDLE "${DESKTOP_SRC_DIR}/main.cpp")
        set_target_properties(ekron-realms PROPERTIES
            OUTPUT_NAME "EkronRealms"
            MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Resources/Info.plist"
            MACOSX_BUNDLE_GUI_IDENTIFIER "com.kaytestudio.ekronrealms"
            MACOSX_BUNDLE_BUNDLE_NAME "Ekron Realms"
            MACOSX_BUNDLE_EXECUTABLE_NAME "EkronRealms"
            MACOSX_BUNDLE_ICON_FILE "RINGSCE_v2.png"
            MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
            MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
            MACOSX_BUNDLE_COPYRIGHT "Copyright (c) Kayte Studio. AGPL-3.0."
            BUILD_WITH_INSTALL_RPATH TRUE
            INSTALL_RPATH "@executable_path/../Frameworks"
        )

        # Populate EkronRealms.app/Contents/Resources with shared sandbox assets
        set(MAC_APP_RESOURCES "$<TARGET_BUNDLE_CONTENT_DIR:ekron-realms>/Resources")
        add_custom_command(TARGET ekron-realms POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "${MAC_APP_RESOURCES}"
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${SHARED_ASSETS_DIR}/models" "${MAC_APP_RESOURCES}/models"
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${SHARED_ASSETS_DIR}/textures" "${MAC_APP_RESOURCES}/textures"
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${SHARED_ASSETS_DIR}/catalog" "${MAC_APP_RESOURCES}/catalog"
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${SHARED_ASSETS_DIR}/config" "${MAC_APP_RESOURCES}/config"
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${SHARED_ASSETS_DIR}/sounds" "${MAC_APP_RESOURCES}/sounds"
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${SHARED_ASSETS_DIR}/maps" "${MAC_APP_RESOURCES}/maps"
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${SHARED_ASSETS_DIR}/env" "${MAC_APP_RESOURCES}/env"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${SHARED_ASSETS_DIR}/intro.mp4" "${MAC_APP_RESOURCES}/intro.mp4"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/RINGSCE_v2.png" "${MAC_APP_RESOURCES}/RINGSCE_v2.png"
            COMMENT "Copying Ekron 3D assets into EkronRealms.app/Contents/Resources"
            VERBATIM
        )
    elseif(WIN32)
        # A GUI program (no console window from Explorer); started from a
        # terminal, it writes to that terminal (sys/desktop/WindowsConsole.h).
        set(EKRON_RC_ICON "${CMAKE_CURRENT_SOURCE_DIR}/Resources/ekron-realms.ico")
        set(EKRON_RC_MANIFEST "${CMAKE_CURRENT_SOURCE_DIR}/Resources/ekron-realms.manifest")
        configure_file("${CMAKE_CURRENT_SOURCE_DIR}/Resources/ekron-realms.rc.in"
                       "${CMAKE_CURRENT_BINARY_DIR}/ekron-realms.rc" @ONLY)
        add_executable(ekron-realms WIN32 "${DESKTOP_SRC_DIR}/main.cpp" "${CMAKE_CURRENT_BINARY_DIR}/ekron-realms.rc")
        set_target_properties(ekron-realms PROPERTIES OUTPUT_NAME "ekron-realms")
        if(MINGW)
            # No libstdc++ / libgcc / winpthread DLLs to ship: only SDL2.dll.
            # The subsystem is also passed to the linker directly (clang
            # drivers such as zig cc ignore -mwindows).
            target_link_options(ekron-realms PRIVATE -static -Wl,--subsystem,windows)
        elseif(MSVC)
            target_link_options(ekron-realms PRIVATE /ENTRY:mainCRTStartup /MANIFEST:NO)
        endif()
    else()
        add_executable(ekron-realms "${DESKTOP_SRC_DIR}/main.cpp")
        set_target_properties(ekron-realms PROPERTIES OUTPUT_NAME "ekron-realms")
    endif()

    target_link_libraries(ekron-realms PRIVATE ekron_sandbox_objects)

    if(REALMS_PASCAL_CORE_BUILT)
        # The sandbox loads librealms_core at runtime; build it alongside.
        add_dependencies(ekron-realms realms_pascal_core)
    endif()

    # libekron_sandbox: the same sandbox as a shared library with the C entry
    # point ekron_sandbox_run() (sys/desktop/ekron_sandbox.h), for the Free
    # Pascal realms-pascal program (Projects/realms.lpr, below).
    if(ENABLE_FPC_CLIENT AND FPC_FOUND AND NOT WIN32)
        add_library(ekron_sandbox SHARED "${DESKTOP_SRC_DIR}/main.cpp")
        target_compile_definitions(ekron_sandbox PRIVATE EKRON_SANDBOX_LIBRARY=1)
        target_link_libraries(ekron_sandbox PRIVATE ekron_sandbox_objects)
        if(APPLE)
            set(EKRON_SANDBOX_LIB_DIR "${CMAKE_CURRENT_BINARY_DIR}/EkronRealms.app/Contents/Frameworks")
            set_target_properties(ekron_sandbox PROPERTIES
                INSTALL_NAME_DIR "@rpath"
                BUILD_WITH_INSTALL_RPATH TRUE
                INSTALL_RPATH "@loader_path"
            )
        else()
            set(EKRON_SANDBOX_LIB_DIR "${CMAKE_CURRENT_BINARY_DIR}")
        endif()
        set_target_properties(ekron_sandbox PROPERTIES
            LIBRARY_OUTPUT_DIRECTORY "${EKRON_SANDBOX_LIB_DIR}"
        )
        # The bundle directory exists before the library is linked into it.
        add_dependencies(ekron_sandbox ekron-realms)
    endif()
endif()

# =============================================================================
# Optional Free Pascal Desktop Launcher (Projects/realms_launcher.lpr)
# =============================================================================
set(FPC_LAUNCHER_BUILT FALSE)
set(REALMS_PASCAL_CLIENT_BUILT FALSE)
if(ENABLE_FPC_CLIENT AND WIN32)
    # The Pascal launcher and realms-pascal use the Unix sys layer; Windows
    # builds ship ekron-realms.exe with the Pascal core and the game module.
    message(STATUS "Windows: skipping realms-launcher and realms-pascal (Unix only).")
elseif(ENABLE_FPC_CLIENT)
    if(FPC_FOUND)
        message(STATUS "=== Free Pascal Compiler Found: ${FPC_EXECUTABLE} (${FPC_VERSION}) ===")
        set(FPC_EXTRA_FLAGS -Mobjfpc -Scghi -O2)
        if(APPLE)
            set(FPC_OUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/EkronRealms.app/Contents/MacOS")
            set(FPC_PLATFORM_DIR "${CMAKE_CURRENT_SOURCE_DIR}/sys/mac")
        else()
            list(APPEND FPC_EXTRA_FLAGS -Xs)
            set(FPC_OUT_DIR "${CMAKE_CURRENT_BINARY_DIR}")
            set(FPC_PLATFORM_DIR "${CMAKE_CURRENT_SOURCE_DIR}/sys/linux")
        endif()

        set(FPC_EXTRA_DEPS "")
        if(TARGET ekron-realms)
            list(APPEND FPC_EXTRA_DEPS ekron-realms)
        endif()

        add_fpc_executable(realms_launcher_fpc
            SOURCES
                "Projects/realms_launcher.lpr"
            INCLUDE_DIRS
                "${FPC_PLATFORM_DIR}"
            COMPILE_FLAGS
                ${FPC_EXTRA_FLAGS}
            DEPENDS
                ${FPC_EXTRA_DEPS}
            OUTPUT_NAME "realms-launcher"
            OUTPUT_DIRECTORY "${FPC_OUT_DIR}"
        )
        if(TARGET ekron-realms)
            add_dependencies(realms_launcher_fpc ekron-realms)
        endif()
        set(FPC_LAUNCHER_BUILT TRUE)

        # realms-pascal (Projects/realms.lpr): a Free Pascal program with the
        # Pascal Realms core compiled in, running the C++ SDL2 sandbox from
        # libekron_sandbox in the same process.
        if(TARGET ekron_sandbox)
            file(GLOB _realms_pascal_units "${REALMS_DIR}/pascal/*.pas" "${REALMS_DIR}/pascal/*.inc")
            set(_realms_pascal_flags -Mobjfpc -Sh -O2 "-Fl${EKRON_SANDBOX_LIB_DIR}")
            if(APPLE)
                list(APPEND _realms_pascal_flags "-k-rpath" "-k@executable_path/../Frameworks")
            else()
                # Build tree: next to the program; .deb: <prefix>/lib/ekron-realms.
                list(APPEND _realms_pascal_flags -Xs "-k-rpath" "-k\$ORIGIN:\$ORIGIN/../lib/ekron-realms")
                if(EKRON_SDL2_LIBRARY)
                    # ld checks libekron_sandbox's own dependencies (SDL2) too.
                    get_filename_component(_sdl2_lib_dir "${EKRON_SDL2_LIBRARY}" DIRECTORY)
                    list(APPEND _realms_pascal_flags "-k-rpath-link" "-k${_sdl2_lib_dir}")
                endif()
                foreach(_ffmpeg_lib_dir IN LISTS EKRON_FFMPEG_LIBRARY_DIRS) # and FFmpeg's
                    list(APPEND _realms_pascal_flags "-k-rpath-link" "-k${_ffmpeg_lib_dir}")
                endforeach()
            endif()
            add_fpc_executable(realms_pascal_client
                SOURCES
                    "Projects/realms.lpr"
                    ${_realms_pascal_units}
                INCLUDE_DIRS
                    "${REALMS_DIR}/pascal"
                COMPILE_FLAGS
                    ${_realms_pascal_flags}
                DEPENDS
                    ekron_sandbox
                OUTPUT_NAME "realms-pascal"
                OUTPUT_DIRECTORY "${FPC_OUT_DIR}"
            )
            add_dependencies(realms_pascal_client ekron_sandbox)
            set(REALMS_PASCAL_CLIENT_BUILT TRUE)
        endif()
    else()
        message(STATUS "Free Pascal Compiler (fpc) not found; skipping optional realms-launcher target.")
    endif()
endif()

# =============================================================================
# Installation Rules (.app on macOS, FHS / .deb layout on Linux)
# =============================================================================
include(GNUInstallDirs)

if(TARGET ekron-realms)
    if(APPLE)
        install(TARGETS ekron-realms
            BUNDLE DESTINATION .
        )
    elseif(WIN32)
        # Portable folder: ekron-realms.exe with its DLLs, assets\ next to it
        # (found through SDL_GetBasePath() / "assets", sys/desktop/main.cpp).
        # SDL2.dll is the one DLL to ship (MinGW links its runtime statically;
        # MSVC builds use the static CRT, see CMAKE_MSVC_RUNTIME_LIBRARY).
        set(_sdl2_hints "")
        if(TARGET SDL2::SDL2)
            get_target_property(_sdl2_type SDL2::SDL2 TYPE)
            if(_sdl2_type STREQUAL "SHARED_LIBRARY")
                foreach(_prop IMPORTED_LOCATION_RELEASE IMPORTED_LOCATION_NOCONFIG IMPORTED_LOCATION IMPORTED_LOCATION_DEBUG)
                    get_target_property(_sdl2_loc SDL2::SDL2 ${_prop})
                    if(_sdl2_loc)
                        get_filename_component(_sdl2_dir "${_sdl2_loc}" DIRECTORY)
                        list(APPEND _sdl2_hints "${_sdl2_dir}")
                    endif()
                endforeach()
            endif()
        endif()
        if(SDL2_DIR)
            list(APPEND _sdl2_hints "${SDL2_DIR}/../../../bin" "${SDL2_DIR}/../bin" "${SDL2_DIR}/../../bin")
        endif()
        if(EKRON_SDL2_LIBRARY)
            get_filename_component(_sdl2_dir "${EKRON_SDL2_LIBRARY}" DIRECTORY)
            list(APPEND _sdl2_hints "${_sdl2_dir}" "${_sdl2_dir}/../bin")
        endif()
        find_file(EKRON_SDL2_DLL NAMES SDL2.dll HINTS ${_sdl2_hints} NO_CMAKE_FIND_ROOT_PATH
                  DOC "SDL2.dll copied next to ekron-realms.exe")
        install(TARGETS ekron-realms RUNTIME DESTINATION .)
        if(EKRON_SDL2_DLL)
            install(FILES "${EKRON_SDL2_DLL}" DESTINATION .)
            # Run from the build tree (CTest, debugging) without PATH changes.
            add_custom_command(TARGET ekron-realms POST_BUILD
                COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${EKRON_SDL2_DLL}" "$<TARGET_FILE_DIR:ekron-realms>"
                VERBATIM)
        else()
            message(WARNING "SDL2.dll not found; set EKRON_SDL2_DLL so that the package includes it.")
        endif()
        foreach(_ekron_lib IN ITEMS REALMS_PASCAL_CORE REALMS_GAME_MODULE)
            if(${_ekron_lib}_BUILT)
                install(FILES "${${_ekron_lib}_PATH}" DESTINATION .)
            endif()
        endforeach()
        install(DIRECTORY
            "${SHARED_ASSETS_DIR}/models"
            "${SHARED_ASSETS_DIR}/textures"
            "${SHARED_ASSETS_DIR}/catalog"
            "${SHARED_ASSETS_DIR}/config"
            "${SHARED_ASSETS_DIR}/sounds"
            "${SHARED_ASSETS_DIR}/maps"
            "${SHARED_ASSETS_DIR}/env"
            DESTINATION assets
        )
        install(FILES "${SHARED_ASSETS_DIR}/intro.mp4" DESTINATION assets)
        install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/README.md" "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE" DESTINATION .)
    else()
        install(TARGETS ekron-realms
            RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
        )

        # Install convenience symlink /usr/bin/realms -> ekron-realms
        install(CODE "
            set(_bindir \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}\")
            file(MAKE_DIRECTORY \"\${_bindir}\")
            execute_process(
                COMMAND \"${CMAKE_COMMAND}\" -E create_symlink ekron-realms \"\${_bindir}/realms\"
            )
        ")

        if(FPC_LAUNCHER_BUILT)
            install(PROGRAMS "${CMAKE_CURRENT_BINARY_DIR}/realms-launcher"
                DESTINATION "${CMAKE_INSTALL_BINDIR}"
            )
        endif()

        if(REALMS_PASCAL_CLIENT_BUILT)
            install(PROGRAMS "${CMAKE_CURRENT_BINARY_DIR}/realms-pascal"
                DESTINATION "${CMAKE_INSTALL_BINDIR}"
            )
            install(TARGETS ekron_sandbox
                LIBRARY DESTINATION "lib/ekron-realms"
            )
        endif()

        if(REALMS_GAME_MODULE_BUILT)
            # Probed by realms::GameServer at <bindir>/../lib/ekron-realms.
            install(FILES "${REALMS_GAME_MODULE_PATH}"
                DESTINATION "lib/ekron-realms"
                PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ
            )
        endif()

        if(REALMS_PASCAL_CORE_BUILT)
            # Private library probed by realms::Engine at <bindir>/../lib/ekron-realms.
            install(FILES "${REALMS_PASCAL_CORE_PATH}"
                DESTINATION "lib/ekron-realms"
                PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ
            )
        endif()

        install(DIRECTORY
            "${SHARED_ASSETS_DIR}/models"
            "${SHARED_ASSETS_DIR}/textures"
            "${SHARED_ASSETS_DIR}/catalog"
            "${SHARED_ASSETS_DIR}/config"
            "${SHARED_ASSETS_DIR}/sounds"
            "${SHARED_ASSETS_DIR}/maps"
            "${SHARED_ASSETS_DIR}/env"
            DESTINATION "${CMAKE_INSTALL_DATADIR}/ekron-realms"
        )
        install(FILES "${SHARED_ASSETS_DIR}/intro.mp4"
            DESTINATION "${CMAKE_INSTALL_DATADIR}/ekron-realms"
        )
        install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/Resources/ekron-realms.desktop"
            DESTINATION "${CMAKE_INSTALL_DATADIR}/applications"
        )
        install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/RINGSCE_v2.png"
            DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps"
            RENAME "ekron-realms.png"
        )
        install(FILES
            "${CMAKE_CURRENT_SOURCE_DIR}/README.md"
            "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE"
            DESTINATION "${CMAKE_INSTALL_DOCDIR}"
        )
    endif()
endif()

# =============================================================================
# CTest Smoke Tests
# =============================================================================
enable_testing()

add_executable(ekron_desktop_hud_layout_test tests/native/desktop_hud_layout_test.cpp)
add_test(NAME ekron_desktop_hud_layout COMMAND ekron_desktop_hud_layout_test)

# In-game store catalog / wallet / purchase rules (SDL-independent).
add_executable(ekron_shop_catalog_test
    tests/native/shop_catalog_test.cpp
    sys/desktop/ShopCatalog.cpp
    sys/desktop/Localization.cpp
)
target_include_directories(ekron_shop_catalog_test PRIVATE
    sys/desktop
    sys/android/app/src/main/cpp
)
add_test(NAME ekron_shop_catalog COMMAND ekron_shop_catalog_test)

# Desktop i18n: English / Russian / Portuguese (Portugal) / Spanish (Spain) string table,
# placeholders, locale parsing and pixel-font glyph coverage (SDL-independent).
add_executable(ekron_localization_test
    tests/native/localization_test.cpp
    sys/desktop/Localization.cpp
)
target_include_directories(ekron_localization_test PRIVATE sys/desktop realms/include)
add_test(NAME ekron_localization COMMAND ekron_localization_test)

# GLSL embedded in the desktop and Android renderers: no reserved words as
# names (glslang and NVIDIA accept some, Mesa rejects the whole shader).
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    add_test(NAME ekron_glsl_keywords
             COMMAND Python3::Interpreter "${CMAKE_CURRENT_SOURCE_DIR}/tools/check_glsl_keywords.py")
endif()

# Intro video through FFmpeg or Media Foundation: plays assets/intro.mp4
# against a fake audio output and clock (no sound card, faster than real time).
if(EKRON_INTRO_BACKEND STREQUAL "MediaFoundation")
    add_executable(ekron_intro_video_test
        tests/native/intro_video_test.cpp
        sys/desktop/IntroVideoMediaFoundation.cpp
    )
    target_include_directories(ekron_intro_video_test PRIVATE sys/desktop)
    target_link_libraries(ekron_intro_video_test PRIVATE mfuuid ole32)
    add_test(NAME ekron_intro_video COMMAND ekron_intro_video_test "${SHARED_ASSETS_DIR}/intro.mp4")
elseif(EKRON_INTRO_BACKEND STREQUAL "FFmpeg")
    add_executable(ekron_intro_video_test
        tests/native/intro_video_test.cpp
        sys/desktop/IntroVideoFFmpeg.cpp
    )
    target_include_directories(ekron_intro_video_test PRIVATE sys/desktop)
    target_link_libraries(ekron_intro_video_test PRIVATE PkgConfig::EKRON_FFMPEG)
    add_test(NAME ekron_intro_video COMMAND ekron_intro_video_test "${SHARED_ASSETS_DIR}/intro.mp4")
endif()

# Realms bridge conformance: C++ reference core (+ Free Pascal core below).
add_executable(realms_bridge_test tests/realms/realms_bridge_test.cpp)
target_link_libraries(realms_bridge_test PRIVATE realms_quake2)
add_test(NAME realms_bridge_native COMMAND realms_bridge_test)
set_tests_properties(realms_bridge_native PROPERTIES ENVIRONMENT "REALMS_CORE=")

# glTF maps: collision (realms::MeshMap) and the drawn model (city::Scene in
# source space) line up, on a synthetic room and every shipped .glb.
add_executable(realms_map_render_alignment_test
    tests/realms/map_render_alignment_test.cpp
    "${SHARED_CPP_DIR}/GltfScene.cpp"
    "${SHARED_CPP_DIR}/MapSession.cpp"
    "${SHARED_CPP_DIR}/ImageDecode.cpp"
)
target_include_directories(realms_map_render_alignment_test PRIVATE tests/realms "${SHARED_CPP_DIR}")
target_link_libraries(realms_map_render_alignment_test PRIVATE realms_engine)
add_test(NAME realms_map_render_alignment
         COMMAND realms_map_render_alignment_test "${SHARED_ASSETS_DIR}/models")

# Game sound: every shipped .ogg decodes and is credited; mixer, director,
# console commands (play / soundlist / stopsound) and s_volume cvars.
add_executable(ekron_sound_test
    tests/native/sound_test.cpp
    "${SHARED_CPP_DIR}/SoundDecode.cpp"
    "${SHARED_CPP_DIR}/SoundMixer.cpp"
    "${SHARED_CPP_DIR}/SoundDirector.cpp"
    "${SHARED_CPP_DIR}/SoundSystem.cpp"
)
target_include_directories(ekron_sound_test PRIVATE "${SHARED_CPP_DIR}")
find_package(Threads REQUIRED)
target_link_libraries(ekron_sound_test PRIVATE realms_engine Threads::Threads)
add_test(NAME ekron_sound COMMAND ekron_sound_test "${SHARED_ASSETS_DIR}")
set_tests_properties(ekron_sound PROPERTIES ENVIRONMENT "REALMS_CORE=native")

# Quake 2 game module on realms::GameServer: Q2Pmove on Engine traces, then
# the Free Pascal game (spawn, walk, pickup, door, commands, restart). Without
# the module only the Pmove part runs and the test reports "skipped" (77).
add_executable(realms_game_server_test tests/realms/game_server_test.cpp)
target_include_directories(realms_game_server_test PRIVATE tests/realms)
target_link_libraries(realms_game_server_test PRIVATE realms_quake2)
if(REALMS_GAME_MODULE_BUILT)
    add_dependencies(realms_game_server_test realms_game_module)
    add_test(NAME realms_game_server_native
             COMMAND realms_game_server_test --game "${REALMS_GAME_MODULE_PATH}")
    if(REALMS_PASCAL_CORE_BUILT)
        add_dependencies(realms_game_server_test realms_pascal_core)
        add_test(NAME realms_game_server_pascal
                 COMMAND realms_game_server_test --game "${REALMS_GAME_MODULE_PATH}" --pascal "${REALMS_PASCAL_CORE_PATH}")
    endif()
else()
    add_test(NAME realms_game_server_native COMMAND realms_game_server_test)
    set_tests_properties(realms_game_server_native PROPERTIES SKIP_RETURN_CODE 77)
endif()
set_tests_properties(realms_game_server_native PROPERTIES ENVIRONMENT "REALMS_CORE=;REALMS_GAME_LIBRARY=")

# The Quake 2 stack as one module: realms::Quake2Module and the sandboxes'
# shared map mode on it (mapview::Quake2View) on a lightmapped arena; with
# the game module: walking, the door, a pickup, entities, effects, light
# styles, respawn. Without the module the game part reports "skipped" (77).
add_executable(realms_quake2_module_test
    tests/realms/quake2_module_test.cpp
    "${SHARED_CPP_DIR}/MapSession.cpp"
    "${SHARED_CPP_DIR}/Quake2View.cpp"
)
target_include_directories(realms_quake2_module_test PRIVATE tests/realms "${SHARED_CPP_DIR}")
target_link_libraries(realms_quake2_module_test PRIVATE realms_quake2)
if(REALMS_GAME_MODULE_BUILT)
    add_dependencies(realms_quake2_module_test realms_game_module)
    add_test(NAME realms_quake2_module_native
             COMMAND realms_quake2_module_test --game "${REALMS_GAME_MODULE_PATH}")
    if(REALMS_PASCAL_CORE_BUILT)
        add_dependencies(realms_quake2_module_test realms_pascal_core)
        add_test(NAME realms_quake2_module_pascal
                 COMMAND realms_quake2_module_test --game "${REALMS_GAME_MODULE_PATH}" --pascal "${REALMS_PASCAL_CORE_PATH}")
    endif()
else()
    add_test(NAME realms_quake2_module_native COMMAND realms_quake2_module_test)
    set_tests_properties(realms_quake2_module_native PROPERTIES SKIP_RETURN_CODE 77)
endif()
set_tests_properties(realms_quake2_module_native PROPERTIES ENVIRONMENT "REALMS_CORE=;REALMS_GAME_LIBRARY=")

# A map compiled by id's qbsp3 / qvis3 / qrad3 (tools/build_q2_test_map.sh):
# collision, areas, PVS, qrad lightmaps and styles, then the game on it.
set(REALMS_Q2ROOM_BSP "${CMAKE_CURRENT_SOURCE_DIR}/tests/realms/maps/q2room.bsp")
add_executable(realms_q2_compiled_map_test
    tests/realms/q2_compiled_map_test.cpp
    "${SHARED_CPP_DIR}/MapSession.cpp"
    "${SHARED_CPP_DIR}/Quake2View.cpp"
)
target_include_directories(realms_q2_compiled_map_test PRIVATE "${SHARED_CPP_DIR}")
target_link_libraries(realms_q2_compiled_map_test PRIVATE realms_quake2)
if(REALMS_GAME_MODULE_BUILT)
    add_dependencies(realms_q2_compiled_map_test realms_game_module)
    add_test(NAME realms_q2_compiled_map_native
             COMMAND realms_q2_compiled_map_test --map "${REALMS_Q2ROOM_BSP}" --game "${REALMS_GAME_MODULE_PATH}")
    if(REALMS_PASCAL_CORE_BUILT)
        add_dependencies(realms_q2_compiled_map_test realms_pascal_core)
        add_test(NAME realms_q2_compiled_map_pascal
                 COMMAND realms_q2_compiled_map_test --map "${REALMS_Q2ROOM_BSP}" --game "${REALMS_GAME_MODULE_PATH}"
                         --pascal "${REALMS_PASCAL_CORE_PATH}")
    endif()
else()
    add_test(NAME realms_q2_compiled_map_native COMMAND realms_q2_compiled_map_test --map "${REALMS_Q2ROOM_BSP}")
    set_tests_properties(realms_q2_compiled_map_native PROPERTIES SKIP_RETURN_CODE 77)
endif()
set_tests_properties(realms_q2_compiled_map_native PROPERTIES ENVIRONMENT "REALMS_CORE=;REALMS_GAME_LIBRARY=")

# The sample glTF map the apps ship (assets/maps/courtyard.glb, its skybox in
# assets/env; tools/build_sample_map.py): entities, sky seams, then the game
# through its doors, lift and button.
add_executable(realms_sample_map_test
    tests/realms/sample_map_test.cpp
    "${SHARED_CPP_DIR}/MapSession.cpp"
    "${SHARED_CPP_DIR}/Quake2View.cpp"
)
target_include_directories(realms_sample_map_test PRIVATE "${SHARED_CPP_DIR}")
target_link_libraries(realms_sample_map_test PRIVATE realms_quake2)
if(REALMS_GAME_MODULE_BUILT)
    add_dependencies(realms_sample_map_test realms_game_module)
    add_test(NAME realms_sample_map_native
             COMMAND realms_sample_map_test --assets "${SHARED_ASSETS_DIR}" --game "${REALMS_GAME_MODULE_PATH}")
    if(REALMS_PASCAL_CORE_BUILT)
        add_dependencies(realms_sample_map_test realms_pascal_core)
        add_test(NAME realms_sample_map_pascal
                 COMMAND realms_sample_map_test --assets "${SHARED_ASSETS_DIR}" --game "${REALMS_GAME_MODULE_PATH}"
                         --pascal "${REALMS_PASCAL_CORE_PATH}")
    endif()
else()
    add_test(NAME realms_sample_map_native COMMAND realms_sample_map_test --assets "${SHARED_ASSETS_DIR}")
    set_tests_properties(realms_sample_map_native PROPERTIES SKIP_RETURN_CODE 77)
endif()
set_tests_properties(realms_sample_map_native PROPERTIES ENVIRONMENT "REALMS_CORE=;REALMS_GAME_LIBRARY=")

# Quake 3 .md3 models, the model cache and PCX skins (realms::Md3Model,
# GameModelCache, decodePcx), generated in memory by tests/realms/md3_builder.h.
add_executable(realms_md3_test tests/realms/md3_test.cpp)
target_include_directories(realms_md3_test PRIVATE tests/realms)
target_link_libraries(realms_md3_test PRIVATE realms_quake2)
add_test(NAME realms_md3 COMMAND realms_md3_test)

add_executable(realms_game_layout_test tests/realms/game_layout_test.cpp)
target_link_libraries(realms_game_layout_test PRIVATE realms_quake2)
add_test(NAME realms_game_layout COMMAND realms_game_layout_test)

# Quake 2 client effects (realms::GameEffects): temp entities, muzzle
# flashes, trails, lights, explosions - no game module needed.
add_executable(realms_game_effects_test tests/realms/game_effects_test.cpp)
target_include_directories(realms_game_effects_test PRIVATE tests/realms)
target_link_libraries(realms_game_effects_test PRIVATE realms_quake2)
add_test(NAME realms_game_effects COMMAND realms_game_effects_test)

# Quake 2 BSP lightmaps (realms::MapLighting): the atlas, lightmap
# coordinates, light styles and R_LightPoint on maps from bsp_builder.h.
add_executable(realms_map_lighting_test tests/realms/map_lighting_test.cpp)
target_include_directories(realms_map_lighting_test PRIVATE tests/realms)
target_link_libraries(realms_map_lighting_test PRIVATE realms_quake2)
add_test(NAME realms_map_lighting COMMAND realms_map_lighting_test)

# Map modes (desktop, Android, iOS): entity poses and model vertices in render
# space (the view weapon follows the camera for every yaw / pitch).
# Header-only, no SDL / GL.
add_executable(realms_map_entity_transform_test tests/realms/map_entity_transform_test.cpp)
target_include_directories(realms_map_entity_transform_test PRIVATE sys/android/app/src/main/cpp)
target_link_libraries(realms_map_entity_transform_test PRIVATE realms_quake2)
add_test(NAME realms_map_entity_transform COMMAND realms_map_entity_transform_test)

add_executable(realms_fpc_library_environ_test tests/realms/fpc_library_environ_test.cpp)
target_include_directories(realms_fpc_library_environ_test PRIVATE realms/src)
add_test(NAME realms_fpc_library_environ COMMAND realms_fpc_library_environ_test)
set_tests_properties(realms_fpc_library_environ PROPERTIES SKIP_RETURN_CODE 77)

if(EKRON_SDL2_TARGET)
    # Drop-down console line editing / command submission (no GL needed).
    add_executable(realms_desktop_console_test
        tests/realms/desktop_console_test.cpp
        sys/desktop/DesktopConsole.cpp
    )
    target_include_directories(realms_desktop_console_test PRIVATE sys/desktop sys/android/app/src/main/cpp)
    target_link_libraries(realms_desktop_console_test PRIVATE realms_engine ${EKRON_SDL2_TARGET})
    target_compile_definitions(realms_desktop_console_test PRIVATE EKRON_CONSOLE_TEST_NO_RENDERER=1)
    add_test(NAME realms_desktop_console COMMAND realms_desktop_console_test)
    set_tests_properties(realms_desktop_console PROPERTIES ENVIRONMENT "REALMS_CORE=native")
endif()

if(REALMS_PASCAL_CORE_BUILT)
    add_dependencies(realms_bridge_test realms_pascal_core)
    add_test(NAME realms_bridge_pascal COMMAND realms_bridge_test --pascal "${REALMS_PASCAL_CORE_PATH}")
    add_dependencies(ekron_sound_test realms_pascal_core)
    add_test(NAME ekron_sound_pascal COMMAND ekron_sound_test "${SHARED_ASSETS_DIR}" --pascal "${REALMS_PASCAL_CORE_PATH}")
endif()

if(TARGET ekron-realms)
    add_test(
        NAME ekron_desktop_engine_info
        COMMAND $<TARGET_FILE:ekron-realms> --engine-info --assets "${SHARED_ASSETS_DIR}"
    )
    if(REALMS_PASCAL_CORE_BUILT)
        # The shipped sandbox must actually run on the Free Pascal core.
        add_test(
            NAME ekron_desktop_headless_smoke_pascal
            COMMAND $<TARGET_FILE:ekron-realms> --headless-smoke --assets "${SHARED_ASSETS_DIR}"
        )
        set_tests_properties(ekron_desktop_headless_smoke_pascal PROPERTIES
            ENVIRONMENT "REALMS_CORE=pascal"
            PASS_REGULAR_EXPRESSION "realmsCore=pascal-quake2"
        )
    endif()
    if(REALMS_GAME_MODULE_BUILT)
        # The sandbox finds the game module it ships with (next to it).
        add_test(
            NAME ekron_desktop_game_module
            COMMAND $<TARGET_FILE:ekron-realms> --engine-info --assets "${SHARED_ASSETS_DIR}"
        )
        set_tests_properties(ekron_desktop_game_module PROPERTIES
            ENVIRONMENT "REALMS_GAME_LIBRARY="
            PASS_REGULAR_EXPRESSION "Game: +[^\r\n]*realms_game[^\r\n]*Quake 2 game API 3"
        )
    endif()
    add_test(
        NAME ekron_desktop_version
        COMMAND $<TARGET_FILE:ekron-realms> --version
    )
    add_test(
        NAME ekron_desktop_headless_smoke
        COMMAND $<TARGET_FILE:ekron-realms> --headless-smoke --assets "${SHARED_ASSETS_DIR}"
    )
endif()

if(REALMS_PASCAL_CLIENT_BUILT)
    # The Free Pascal program runs the sandbox on the Pascal core it links in.
    get_target_property(_REALMS_PASCAL_BIN realms_pascal_client FPC_OUTPUT_PATH)
    add_test(
        NAME realms_pascal_client_engine_info
        COMMAND "${_REALMS_PASCAL_BIN}" --engine-info --assets "${SHARED_ASSETS_DIR}"
    )
    set_tests_properties(realms_pascal_client_engine_info PROPERTIES
        ENVIRONMENT "REALMS_CORE="
        PASS_REGULAR_EXPRESSION "Core: +pascal-quake2 \\(Free Pascal\\)[\r\n]+Origin: +linked"
    )
    add_test(
        NAME realms_pascal_client_headless_smoke
        COMMAND "${_REALMS_PASCAL_BIN}" --headless-smoke --assets "${SHARED_ASSETS_DIR}"
    )
    set_tests_properties(realms_pascal_client_headless_smoke PROPERTIES
        ENVIRONMENT "REALMS_CORE=pascal"
        PASS_REGULAR_EXPRESSION "realmsCore=pascal-quake2"
    )
endif()

if(FPC_LAUNCHER_BUILT)
    get_target_property(_FPC_BIN realms_launcher_fpc FPC_OUTPUT_PATH)
    add_test(
        NAME ekron_fpc_launcher_version
        COMMAND "${_FPC_BIN}" --version
    )
endif()

# =============================================================================
# CPack Configuration (.deb on Linux, DragNDrop/TGZ on macOS)
# =============================================================================
set(CPACK_PACKAGE_NAME "ekron-realms")
set(CPACK_PACKAGE_VENDOR "Kayte Studio")
set(CPACK_PACKAGE_CONTACT "Kayte Studio <pedrolvicente@gmail.com>")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Ekron Realms - Native 3D Urban Sandbox")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_RESOURCE_FILE_README "${CMAKE_CURRENT_SOURCE_DIR}/README.md")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/kaytestudio/ekron-realms")
set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")

if(WIN32)
    # ekron-realms-<version>-windows-x64.zip, one top-level folder.
    set(CPACK_GENERATOR "ZIP")
    set(CPACK_PACKAGING_INSTALL_PREFIX "/")
    set(CPACK_PACKAGE_FILE_NAME "ekron-realms-${PROJECT_VERSION}-windows-x64")
elseif(APPLE)
    set(CPACK_GENERATOR "DragNDrop;TGZ")
    set(CPACK_PACKAGING_INSTALL_PREFIX "/")
    set(CPACK_DMG_VOLUME_NAME "Ekron Realms")
    set(CPACK_DMG_FORMAT "UDZO")
elseif(UNIX)
    set(CPACK_GENERATOR "DEB;TGZ")
    set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
    set(CPACK_DEBIAN_PACKAGE_NAME "ekron-realms")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Kayte Studio <pedrolvicente@gmail.com>")
    set(CPACK_DEBIAN_PACKAGE_SECTION "games")
    set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
    set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "https://github.com/kaytestudio/ekron-realms")
    find_program(_EKRON_DPKG_SHLIBDEPS dpkg-shlibdeps)
    find_program(_EKRON_FILE_CMD file)
    if(_EKRON_DPKG_SHLIBDEPS AND _EKRON_FILE_CMD)
        set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
    else()
        set(CPACK_DEBIAN_PACKAGE_DEPENDS "libsdl2-2.0-0, libc6, libstdc++6")
        if(EKRON_INTRO_BACKEND STREQUAL "FFmpeg") # runtime packages are named after the soname
            foreach(_lib avformat avcodec swscale swresample avutil)
                string(REGEX MATCH "^[0-9]+" _major "${EKRON_FFMPEG_lib${_lib}_VERSION}")
                if(_major)
                    string(APPEND CPACK_DEBIAN_PACKAGE_DEPENDS ", lib${_lib}${_major}")
                endif()
            endforeach()
        endif()
    endif()
endif()

include(CPack)
