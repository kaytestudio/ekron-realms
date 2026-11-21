cmake_minimum_required(VERSION 3.16...3.31)
project(EkronRealms VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

option(ENABLE_DESKTOP_SANDBOX "Build the native C++17 SDL2/OpenGL desktop sandbox client" ON)
option(ENABLE_FPC_CLIENT "Build the Free Pascal desktop launcher when fpc is installed" ON)
option(ENABLE_PASCAL_CORE "Build the Free Pascal Realms core (librealms_core, Quake 2 stack) when fpc is installed" ON)
option(ENABLE_EXTERNAL_TOOLS "Fetch and build external KayteIDE and KCC repositories" OFF)

set(SHARED_CPP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/sys/android/app/src/main/cpp")
set(SHARED_ASSETS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/sys/android/app/src/main/assets")
set(DESKTOP_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/sys/desktop")
set(REALMS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/realms")

# =============================================================================
# Realms Engine: C++ bridge/facade (+ C++ reference core) and Free Pascal core
# =============================================================================
add_subdirectory("${REALMS_DIR}" realms)

if(ENABLE_FPC_CLIENT OR ENABLE_PASCAL_CORE)
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

# =============================================================================
# Optional External Tools (KayteIDE / KCC)
# =============================================================================
if(ENABLE_EXTERNAL_TOOLS)
    find_package(KayteIDE REQUIRED)
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
            message(FATAL_ERROR "SDL2 development headers/libraries not found. Install libsdl2-dev (Linux) or brew install sdl2 (macOS).")
        endif()
    endif()
endif()

# =============================================================================
# Native C++17 Ekron Urban Sandbox Desktop Client
# =============================================================================
if(ENABLE_DESKTOP_SANDBOX)
    find_package(Threads REQUIRED)

    set(DESKTOP_SOURCES
        "${DESKTOP_SRC_DIR}/main.cpp"
        "${DESKTOP_SRC_DIR}/DesktopRenderer.cpp"
        "${DESKTOP_SRC_DIR}/DesktopOnlineClient.cpp"
        "${DESKTOP_SRC_DIR}/ShopCatalog.cpp"
        "${DESKTOP_SRC_DIR}/Localization.cpp"
        "${DESKTOP_SRC_DIR}/DesktopConsole.cpp"
        "${DESKTOP_SRC_DIR}/DesktopMapMode.cpp"
        "${SHARED_CPP_DIR}/GltfScene.cpp"
        "${SHARED_CPP_DIR}/ImageDecode.cpp"
    )

    if(APPLE)
        add_executable(ekron-realms MACOSX_BUNDLE ${DESKTOP_SOURCES})
        set_target_properties(ekron-realms PROPERTIES
            OUTPUT_NAME "EkronRealms"
            MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Resources/Info.plist"
            MACOSX_BUNDLE_GUI_IDENTIFIER "com.kaytestudio.ekronrealms"
            MACOSX_BUNDLE_BUNDLE_NAME "Ekron Realms"
            MACOSX_BUNDLE_EXECUTABLE_NAME "EkronRealms"
            MACOSX_BUNDLE_ICON_FILE "RINGSCE_v2.png"
            MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
            MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
            MACOSX_BUNDLE_COPYRIGHT "Copyright (c) Kayte Studio. GPL-3.0."
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
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${SHARED_ASSETS_DIR}/intro.mp4" "${MAC_APP_RESOURCES}/intro.mp4"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/RINGSCE_v2.png" "${MAC_APP_RESOURCES}/RINGSCE_v2.png"
            COMMENT "Copying Ekron 3D assets into EkronRealms.app/Contents/Resources"
            VERBATIM
        )
    else()
        add_executable(ekron-realms ${DESKTOP_SOURCES})
        set_target_properties(ekron-realms PROPERTIES OUTPUT_NAME "ekron-realms")
    endif()

    target_include_directories(ekron-realms PRIVATE
        "${DESKTOP_SRC_DIR}"
        "${SHARED_CPP_DIR}"
    )

    target_link_libraries(ekron-realms PRIVATE
        realms_engine
        ${EKRON_SDL2_TARGET}
        Threads::Threads
        $<$<PLATFORM_ID:Linux>:${CMAKE_DL_LIBS}>
    )

    # Presence remains optional when OpenSSL development files are unavailable.
    if(UNIX)
        find_package(OpenSSL 1.1.1 QUIET)
        if(OpenSSL_FOUND)
            target_compile_definitions(ekron-realms PRIVATE EKRON_TLS_ONLINE=1)
            target_link_libraries(ekron-realms PRIVATE OpenSSL::SSL OpenSSL::Crypto)
        else()
            message(STATUS "OpenSSL development files not found: experimental online presence disabled")
        endif()
    endif()

    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(ekron-realms PRIVATE -Wall -Wextra)
    endif()

    if(REALMS_PASCAL_CORE_BUILT)
        # The sandbox loads librealms_core at runtime; build it alongside.
        add_dependencies(ekron-realms realms_pascal_core)
    endif()
endif()

# =============================================================================
# Optional Free Pascal Desktop Launcher (Projects/realms_launcher.lpr)
# =============================================================================
set(FPC_LAUNCHER_BUILT FALSE)
if(ENABLE_FPC_CLIENT)
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

# Desktop i18n: English / Russian / Portuguese (Portugal) string table,
# placeholders, locale parsing and pixel-font glyph coverage (SDL-independent).
add_executable(ekron_localization_test
    tests/native/localization_test.cpp
    sys/desktop/Localization.cpp
)
target_include_directories(ekron_localization_test PRIVATE sys/desktop)
add_test(NAME ekron_localization COMMAND ekron_localization_test)

# Realms bridge conformance: C++ reference core (+ Free Pascal core below).
add_executable(realms_bridge_test tests/realms/realms_bridge_test.cpp)
target_link_libraries(realms_bridge_test PRIVATE realms_engine)
add_test(NAME realms_bridge_native COMMAND realms_bridge_test)
set_tests_properties(realms_bridge_native PROPERTIES ENVIRONMENT "REALMS_CORE=")

# glTF maps: collision (realms::MeshMap) and the drawn model (city::Scene in
# source space) line up, on a synthetic room and every shipped .glb.
add_executable(realms_map_render_alignment_test
    tests/realms/map_render_alignment_test.cpp
    "${SHARED_CPP_DIR}/GltfScene.cpp"
    "${SHARED_CPP_DIR}/ImageDecode.cpp"
)
target_include_directories(realms_map_render_alignment_test PRIVATE tests/realms "${SHARED_CPP_DIR}")
target_link_libraries(realms_map_render_alignment_test PRIVATE realms_engine)
add_test(NAME realms_map_render_alignment
         COMMAND realms_map_render_alignment_test "${SHARED_ASSETS_DIR}/models")

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
    add_test(
        NAME ekron_desktop_version
        COMMAND $<TARGET_FILE:ekron-realms> --version
    )
    add_test(
        NAME ekron_desktop_headless_smoke
        COMMAND $<TARGET_FILE:ekron-realms> --headless-smoke --assets "${SHARED_ASSETS_DIR}"
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

if(APPLE)
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
    endif()
endif()

include(CPack)
