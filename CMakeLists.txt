cmake_minimum_required(VERSION 3.18)
project(accelerator_local C CXX ASM)

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

if(DEFINED ENV{HL2SDKCS2})
    set(SOURCESDK_DIR $ENV{HL2SDKCS2})
else()
    message(FATAL_ERROR "Environment variable HL2SDKCS2 is not set!")
endif()

if(DEFINED ENV{MMSOURCE_DEV})
    set(METAMOD_DIR $ENV{MMSOURCE_DEV})
else()
    message(FATAL_ERROR "Environment variable MMSOURCE_DEV is not set!")
endif()

if(DEFINED ENV{CSGO_PROTO})
    set(CSGO_PROTO_DIR $ENV{CSGO_PROTO})
else()
    message(FATAL_ERROR "Environment variable CSGO_PROTO is not set!")
endif()

include(makefiles/shared.cmake)
include(makefiles/protobuf.cmake)

file(GLOB_RECURSE SOURCE_FILES
    src/*.cpp
    src/*.h
    src/*.hpp
)

list(APPEND SOURCE_FILES
    ${SOURCESDK_DIR}/public/tier0/memoverride.cpp
    ${SOURCESDK_DIR}/tier1/keyvalues3.cpp
    ${METAMOD_DIR}/core/sourcehook/sourcehook.cpp
    ${METAMOD_DIR}/core/sourcehook/sourcehook_impl_chookidman.cpp
    ${METAMOD_DIR}/core/sourcehook/sourcehook_impl_chookmaninfo.cpp
    ${METAMOD_DIR}/core/sourcehook/sourcehook_impl_cvfnptr.cpp
    ${METAMOD_DIR}/core/sourcehook/sourcehook_impl_cproto.cpp
)

foreach(f ${SOURCE_FILES})
    message(STATUS "Zdrojový soubor: ${f}")
endforeach()

file(GLOB BREAKPAD_SOURCE_FILES
    ${CMAKE_SOURCE_DIR}/vendor/breakpad/src/common/dwarf_cfi_to_module.cc
    ${CMAKE_SOURCE_DIR}/vendor/breakpad/src/common/dwarf_cu_to_module.cc
    ${CMAKE_SOURCE_DIR}/vendor/breakpad/src/common/dwarf_line_to_module.cc
    ${CMAKE_SOURCE_DIR}/vendor/breakpad/src/common/dwarf_range_list_handler.cc
    ${CMAKE_SOURCE_DIR}/vendor/breakpad/src/common/language.cc
    ${CMAKE_SOURCE_DIR}/vendor/breakpad/src/common/module.cc
    ${CMAKE_SOURCE_DIR}/vendor/breakpad/src/common/path_helper.cc
    ${CMAKE_SOURCE_DIR}/vendor/breakpad/src/common/stabs_reader.cc
    ${CMAKE_SOURCE_DIR}/vendor/breakpad/src/common/stabs_to_module.cc
    ${CMAKE_SOURCE_DIR}/vendor/breakpad/src/common/dwarf/bytereader.cc
    ${CMAKE_SOURCE_DIR}/vendor/breakpad/src/common/dwarf/dwarf2diehandler.cc
    ${CMAKE_SOURCE_DIR}/vendor/breakpad/src/common/dwarf/dwarf2reader.cc
    ${CMAKE_SOURCE_DIR}/vendor/breakpad/src/common/dwarf/elf_reader.cc
    ${CMAKE_SOURCE_DIR}/vendor/breakpad/src/common/linux/crc32.cc
    ${CMAKE_SOURCE_DIR}/vendor/breakpad/src/common/linux/dump_symbols.cc
    ${CMAKE_SOURCE_DIR}/vendor/breakpad/src/common/linux/elf_symbols_to_module.cc
)

set_source_files_properties(${BREAKPAD_SOURCE_FILES} PROPERTIES LANGUAGE CXX)

add_library(${PROJECT_NAME} SHARED ${SOURCE_FILES} ${BREAKPAD_SOURCE_FILES})

find_package(Git QUIET)
if (GIT_FOUND)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE GIT_SHA
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
else()
    set(GIT_SHA "nogit")
endif()

if (CMAKE_BUILD_TYPE STREQUAL "Release")
    find_program(STRIP_EXECUTABLE strip)
    if (STRIP_EXECUTABLE)
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${STRIP_EXECUTABLE} --strip-unneeded $<TARGET_FILE:${PROJECT_NAME}>
            COMMENT "Stripping symbols from $<TARGET_FILE:${PROJECT_NAME}>"
        )
    endif()
endif()

if (LINUX)
    include(${CMAKE_SOURCE_DIR}/makefiles/linux.base.cmake)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        PREFIX ""
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/addons/accelerator_local/bin/linuxsteamrt64"
    )
endif()

target_link_libraries(${PROJECT_NAME} ${LINK_LIBRARIES})

add_custom_command(
    TARGET ${PROJECT_NAME} PRE_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}/configs ${CMAKE_BINARY_DIR}
)
