# FindKCC.cmake - KCC (Kayte C Compiler, https://github.com/kaytestudio/kcc)
#
# Builds tools/kcc (cloned if missing) as an ExternalProject in <build>/kcc.
# KCC's CMakeLists is not added directly because it would also build a GCC
# SH-2 cross compiler.
#
# Usage:
#   find_package(KCC)
#   cmake --build build --target kcc
#
# Cache variables:
#   KCC_REPO           Git URL to clone (default kaytestudio/kcc)
#   KCC_GIT_TAG        Branch or tag to clone (default main)
#   KCC_SOURCE_DIR     Checkout location (default <source>/tools/kcc)
#
# Result variables:
#   KCC_FOUND          TRUE when the KCC sources are present
#   KCC_DIR            Same as KCC_SOURCE_DIR
#   KCC_EXECUTABLE     Path of the kcc compiler once the `kcc` target is built
#
# Targets:
#   kcc                Builds KCC (part of `all`)
#   clean-kcc          Deletes the checkout and the KCC build directory

include(ExternalProject)
include(FindPackageHandleStandardArgs)

set(KCC_REPO "https://github.com/kaytestudio/kcc.git" CACHE STRING "KCC Git repository")
set(KCC_GIT_TAG "main" CACHE STRING "KCC branch or tag to clone")
set(KCC_SOURCE_DIR "${CMAKE_SOURCE_DIR}/tools/kcc" CACHE PATH "KCC checkout (tools/kcc)")
set(KCC_BINARY_DIR "${CMAKE_BINARY_DIR}/kcc")

if(NOT EXISTS "${KCC_SOURCE_DIR}/CMakeLists.txt")
    find_package(Git QUIET)
    if(GIT_FOUND)
        message(STATUS "KCC not found in ${KCC_SOURCE_DIR}; cloning ${KCC_REPO} (${KCC_GIT_TAG})...")
        file(REMOVE_RECURSE "${KCC_SOURCE_DIR}")
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" clone --depth 1 --branch "${KCC_GIT_TAG}" "${KCC_REPO}" "${KCC_SOURCE_DIR}"
            RESULT_VARIABLE _kcc_clone_result
            OUTPUT_QUIET
        )
        if(NOT _kcc_clone_result EQUAL 0)
            message(WARNING "Cloning KCC failed (${_kcc_clone_result}).")
        endif()
    else()
        message(WARNING "git not found; clone ${KCC_REPO} into ${KCC_SOURCE_DIR} by hand.")
    endif()
endif()

if(EXISTS "${KCC_SOURCE_DIR}/CMakeLists.txt")
    set(KCC_DIR "${KCC_SOURCE_DIR}")
    set(KCC_EXECUTABLE "${KCC_BINARY_DIR}/bin/kcc${CMAKE_EXECUTABLE_SUFFIX}")
endif()

find_package_handle_standard_args(KCC
    REQUIRED_VARS KCC_DIR
    FAIL_MESSAGE "Could not find KCC: clone ${KCC_REPO} into ${KCC_SOURCE_DIR}"
)
mark_as_advanced(KCC_REPO KCC_GIT_TAG KCC_SOURCE_DIR)

if(KCC_FOUND AND NOT TARGET kcc)
    set(_kcc_args
        -DCMAKE_BUILD_TYPE=Release
        -DBUILD_GCC_SH2=OFF
        -DINSTALL_GCC_SH2=OFF
        -DBUILD_SATURN_TOOLS=OFF
    )
    if(CMAKE_C_COMPILER)
        list(APPEND _kcc_args "-DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}")
    endif()
    if(CMAKE_CXX_COMPILER)
        list(APPEND _kcc_args "-DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}")
    endif()
    if(CMAKE_OSX_ARCHITECTURES)
        string(REPLACE ";" "$<SEMICOLON>" _kcc_archs "${CMAKE_OSX_ARCHITECTURES}")
        list(APPEND _kcc_args "-DCMAKE_OSX_ARCHITECTURES=${_kcc_archs}")
    endif()
    ExternalProject_Add(kcc
        SOURCE_DIR "${KCC_SOURCE_DIR}"
        BINARY_DIR "${KCC_BINARY_DIR}"
        CMAKE_ARGS ${_kcc_args}
        BUILD_COMMAND "${CMAKE_COMMAND}" --build "${KCC_BINARY_DIR}" --config Release --target kcc
        INSTALL_COMMAND ""
        BUILD_ALWAYS TRUE
        BUILD_BYPRODUCTS "${KCC_EXECUTABLE}"
    )
    message(STATUS "KCC: ${KCC_DIR} -> ${KCC_EXECUTABLE}")

    add_custom_target(clean-kcc
        COMMAND "${CMAKE_COMMAND}" -E remove_directory "${KCC_BINARY_DIR}"
        COMMAND "${CMAKE_COMMAND}" -E remove_directory "${KCC_SOURCE_DIR}"
        COMMENT "Removing the KCC checkout and build; re-run CMake to clone it again."
    )
endif()
