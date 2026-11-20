cmake_minimum_required(VERSION 3.16...3.31)
if(APPLE AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS deployment target")
endif()
project(EkronRealms VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

option(ENABLE_DESKTOP_SANDBOX "Build the native C++17 SDL2/OpenGL desktop sandbox client" ON)
option(ENABLE_FPC_CLIENT "Build the Free Pascal desktop launcher when fpc is installed" ON)
option(ENABLE_EXTERNAL_TOOLS "Fetch and build external KayteIDE and KCC repositories" OFF)

set(SHARED_CPP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/sys/android/app/src/main/cpp")
set(SHARED_ASSETS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/sys/android/app/src/main/assets")
set(DESKTOP_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/sys/desktop")

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
        ${EKRON_SDL2_TARGET}
        Threads::Threads
        $<$<PLATFORM_ID:Linux>:${CMAKE_DL_LIBS}>
    )

    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(ekron-realms PRIVATE -Wall -Wextra)
    endif()
endif()

# =============================================================================
# Optional Free Pascal Desktop Launcher (Projects/realms_launcher.lpr)
# =============================================================================
set(FPC_LAUNCHER_BUILT FALSE)
if(ENABLE_FPC_CLIENT)
    find_package(FPC QUIET)
    if(FPC_FOUND)
        message(STATUS "=== Free Pascal Compiler Found: ${FPC_EXECUTABLE} (${FPC_VERSION}) ===")
        set(FPC_EXTRA_FLAGS -Mobjfpc -Scghi -O2)
        if(APPLE)
            list(APPEND FPC_EXTRA_FLAGS -Tdarwin)
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

        add_fpc_executable(realms-launcher
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
            add_dependencies(realms-launcher ekron-realms)
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

if(TARGET ekron-realms)
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
    get_target_property(_FPC_BIN realms-launcher FPC_OUTPUT_PATH)
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
