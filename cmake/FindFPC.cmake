# FindFPC.cmake - Find Free Pascal Compiler
# Locates fpc and provides add_fpc_executable() for Linux, macOS (Intel & Apple Silicon), and Windows.

find_program(FPC_EXECUTABLE
    NAMES fpc ppcx64 ppca64
    PATHS
        /usr/bin
        /usr/local/bin
        /opt/fpc/bin
        /opt/homebrew/bin
        /opt/local/bin
        /Library/Developer/fpc/bin
        /usr/local/lib/fpc/bin
    PATH_SUFFIXES
        bin
    DOC "Free Pascal Compiler"
)

set(FPC_DARWIN_SDK_FLAGS "")

if(FPC_EXECUTABLE)
    execute_process(
        COMMAND ${FPC_EXECUTABLE} -iV
        OUTPUT_VARIABLE FPC_VERSION
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )

    if(FPC_VERSION MATCHES "^([0-9]+)\\.([0-9]+)\\.([0-9]+)")
        set(FPC_VERSION_MAJOR ${CMAKE_MATCH_1})
        set(FPC_VERSION_MINOR ${CMAKE_MATCH_2})
        set(FPC_VERSION_PATCH ${CMAKE_MATCH_3})
    endif()

    execute_process(
        COMMAND ${FPC_EXECUTABLE} -iTP
        OUTPUT_VARIABLE FPC_TARGET_CPU
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )

    execute_process(
        COMMAND ${FPC_EXECUTABLE} -iTO
        OUTPUT_VARIABLE FPC_TARGET_OS
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )

    if(APPLE)
        execute_process(
            COMMAND uname -m
            OUTPUT_VARIABLE MACOS_ARCH
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
        if(MACOS_ARCH STREQUAL "arm64")
            set(FPC_MACOS_ARCH "Apple Silicon (ARM64)")
            set(FPC_TARGET_ARCH "aarch64")
        else()
            set(FPC_MACOS_ARCH "Intel (x86_64)")
            set(FPC_TARGET_ARCH "x86_64")
        endif()

        set(_FPC_SDK "${CMAKE_OSX_SYSROOT}")
        if(NOT _FPC_SDK)
            execute_process(
                COMMAND xcrun --sdk macosx --show-sdk-path
                OUTPUT_VARIABLE _FPC_SDK
                OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_QUIET
            )
        endif()
        if(_FPC_SDK AND EXISTS "${_FPC_SDK}")
            list(APPEND FPC_DARWIN_SDK_FLAGS "-XR${_FPC_SDK}")
        endif()
    endif()
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(FPC
    REQUIRED_VARS FPC_EXECUTABLE
    VERSION_VAR FPC_VERSION
)

mark_as_advanced(FPC_EXECUTABLE)

function(add_fpc_executable target)
    set(options)
    set(oneValueArgs OUTPUT_NAME OUTPUT_DIRECTORY)
    set(multiValueArgs SOURCES UNITS INCLUDE_DIRS COMPILE_FLAGS DEPENDS)
    cmake_parse_arguments(FPC "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if(NOT FPC_SOURCES)
        message(FATAL_ERROR "add_fpc_executable: SOURCES argument is required")
    endif()
    if(NOT FPC_OUTPUT_NAME)
        set(FPC_OUTPUT_NAME ${target})
    endif()
    if(NOT FPC_OUTPUT_DIRECTORY)
        set(FPC_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    endif()

    set(UNIT_OUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/fpc-units/${target}")
    file(MAKE_DIRECTORY "${UNIT_OUT_DIR}")
    file(MAKE_DIRECTORY "${FPC_OUTPUT_DIRECTORY}")

    set(INCLUDE_FLAGS "-FE${FPC_OUTPUT_DIRECTORY}" "-FU${UNIT_OUT_DIR}")
    foreach(dir ${FPC_INCLUDE_DIRS})
        list(APPEND INCLUDE_FLAGS "-Fu${dir}" "-Fi${dir}")
    endforeach()
    foreach(unit ${FPC_UNITS})
        list(APPEND INCLUDE_FLAGS "-Fu${unit}")
    endforeach()

    list(GET FPC_SOURCES 0 MAIN_SOURCE)
    if(IS_ABSOLUTE "${MAIN_SOURCE}")
        set(MAIN_SOURCE_ABS "${MAIN_SOURCE}")
    else()
        set(MAIN_SOURCE_ABS "${CMAKE_CURRENT_SOURCE_DIR}/${MAIN_SOURCE}")
    endif()

    set(ABS_DEPS "")
    foreach(src ${FPC_SOURCES})
        if(IS_ABSOLUTE "${src}")
            list(APPEND ABS_DEPS "${src}")
        else()
            list(APPEND ABS_DEPS "${CMAKE_CURRENT_SOURCE_DIR}/${src}")
        endif()
    endforeach()

    set(OUTPUT_FILENAME "${FPC_OUTPUT_NAME}${CMAKE_EXECUTABLE_SUFFIX}")
    set(OUTPUT_PATH "${FPC_OUTPUT_DIRECTORY}/${OUTPUT_FILENAME}")

    add_custom_command(
        OUTPUT "${OUTPUT_PATH}"
        COMMAND ${FPC_EXECUTABLE}
            ${INCLUDE_FLAGS}
            ${FPC_DARWIN_SDK_FLAGS}
            ${FPC_COMPILE_FLAGS}
            "-o${OUTPUT_FILENAME}"
            "${MAIN_SOURCE_ABS}"
        DEPENDS ${ABS_DEPS} ${FPC_DEPENDS}
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
        COMMENT "Compiling Free Pascal executable ${FPC_OUTPUT_NAME}"
        VERBATIM
    )

    add_custom_target(${target} ALL DEPENDS "${OUTPUT_PATH}")
    set_target_properties(${target} PROPERTIES
        OUTPUT_NAME "${FPC_OUTPUT_NAME}"
        FPC_OUTPUT_PATH "${OUTPUT_PATH}"
    )
endfunction()

# add_fpc_library(<target> SOURCE <library.lpr> NAME <libname> [OUTPUT_DIRECTORY dir]
#                 [UNIT_DIRS ...] [COMPILE_FLAGS ...] [DEPENDS ...])
# Builds a Free Pascal `library` as a shared object for the host platform
# (lib<NAME>.so on Linux, lib<NAME>.dylib on macOS, <NAME>.dll on Windows).
# The output path is stored in the FPC_OUTPUT_PATH target property.
function(add_fpc_library target)
    set(options)
    set(oneValueArgs SOURCE NAME OUTPUT_DIRECTORY)
    set(multiValueArgs UNIT_DIRS COMPILE_FLAGS DEPENDS)
    cmake_parse_arguments(FPCL "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if(NOT FPCL_SOURCE OR NOT FPCL_NAME)
        message(FATAL_ERROR "add_fpc_library: SOURCE and NAME are required")
    endif()
    if(NOT FPCL_OUTPUT_DIRECTORY)
        set(FPCL_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    endif()
    if(NOT IS_ABSOLUTE "${FPCL_SOURCE}")
        set(FPCL_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/${FPCL_SOURCE}")
    endif()

    if(WIN32)
        set(_lib_file "${FPCL_NAME}.dll")
    elseif(APPLE)
        set(_lib_file "lib${FPCL_NAME}.dylib")
    else()
        set(_lib_file "lib${FPCL_NAME}.so")
    endif()
    set(_out "${FPCL_OUTPUT_DIRECTORY}/${_lib_file}")
    set(_unit_out "${CMAKE_CURRENT_BINARY_DIR}/fpc-units/${target}")
    file(MAKE_DIRECTORY "${_unit_out}")
    file(MAKE_DIRECTORY "${FPCL_OUTPUT_DIRECTORY}")

    set(_flags "-FE${FPCL_OUTPUT_DIRECTORY}" "-FU${_unit_out}")
    get_filename_component(_src_dir "${FPCL_SOURCE}" DIRECTORY)
    list(APPEND _flags "-Fu${_src_dir}" "-Fi${_src_dir}")
    set(_deps "${FPCL_SOURCE}")
    foreach(dir ${FPCL_UNIT_DIRS})
        list(APPEND _flags "-Fu${dir}" "-Fi${dir}")
        file(GLOB _units "${dir}/*.pas" "${dir}/*.pp" "${dir}/*.inc")
        list(APPEND _deps ${_units})
    endforeach()
    file(GLOB _src_units "${_src_dir}/*.pas" "${_src_dir}/*.pp" "${_src_dir}/*.inc")
    list(APPEND _deps ${_src_units})
    if(NOT APPLE AND NOT WIN32)
        list(APPEND _flags "-Cg") # position independent code for shared objects
    endif()

    add_custom_command(
        OUTPUT "${_out}"
        COMMAND ${FPC_EXECUTABLE}
            ${_flags}
            ${FPC_DARWIN_SDK_FLAGS}
            ${FPCL_COMPILE_FLAGS}
            "${FPCL_SOURCE}"
        DEPENDS ${_deps} ${FPCL_DEPENDS}
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
        COMMENT "Compiling Free Pascal library ${_lib_file}"
        VERBATIM
    )
    add_custom_target(${target} ALL DEPENDS "${_out}")
    set_target_properties(${target} PROPERTIES FPC_OUTPUT_PATH "${_out}")
endfunction()
