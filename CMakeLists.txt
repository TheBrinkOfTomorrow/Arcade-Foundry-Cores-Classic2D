cmake_minimum_required(VERSION 3.20)
project(arcade_foundry_cores_classic2d CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release)
endif()

# The game this build makes a core for (MAME's set name), and its board.
set(C2D_ROMSET outrun CACHE STRING "MAME ROM set: outrun, shangon, toutrun, aburner2, thndrbld, thndrbld1, orunners, radr")
set_property(CACHE C2D_ROMSET PROPERTY STRINGS outrun shangon toutrun aburner2 thndrbld thndrbld1 orunners radr)
if(C2D_ROMSET STREQUAL "outrun" OR C2D_ROMSET STREQUAL "shangon" OR C2D_ROMSET STREQUAL "toutrun")
    set(C2D_BOARD outrun)
elseif(C2D_ROMSET STREQUAL "aburner2" OR C2D_ROMSET STREQUAL "thndrbld" OR C2D_ROMSET STREQUAL "thndrbld1")
    set(C2D_BOARD xboard)
elseif(C2D_ROMSET STREQUAL "orunners" OR C2D_ROMSET STREQUAL "radr")
    set(C2D_BOARD system32)
else()
    message(FATAL_ERROR "C2D_ROMSET must be outrun, shangon, toutrun, aburner2, thndrbld, thndrbld1, orunners or radr, not ${C2D_ROMSET}")
endif()
# Where scripts/recompile.py put the recompiled code (C2D_GEN_ROOT/<set>/):
# derived from the ROMs, so it lives under build/ and is never committed.
set(C2D_GEN_ROOT ${CMAKE_BINARY_DIR}/gen CACHE PATH "Recompiled code root")
option(C2D_CORE "Build the game core library (afcore_<set>)" ON)
option(C2D_TOOLS "Build the host tools (recompiler, ROM import)" ON)
option(C2D_FRAMEWORK "Apple: build the core as a framework" ${APPLE})

if(MSVC)
    add_compile_options(/W3)
else()
    add_compile_options(-Wall -fno-strict-aliasing)
    # the generated code is one large function per CPU: -O1 compiles it in
    # minutes, -O2 in far longer for little gain
endif()

find_package(Python3 REQUIRED COMPONENTS Interpreter)

# ymfm (the YM2151), pinned to the upstream commit whose sources match
# MAME's copy (3rdparty/ymfm) for everything this core uses
include(FetchContent)
FetchContent_Declare(ymfm
    GIT_REPOSITORY https://github.com/aaronsgiles/ymfm.git
    GIT_TAG 81aec25ccbb98f4873a255f7551ac4dadac59b4a
    GIT_SHALLOW FALSE)
FetchContent_GetProperties(ymfm)
if(NOT ymfm_POPULATED)
    FetchContent_Populate(ymfm)
endif()
add_library(ymfm_opm STATIC ${ymfm_SOURCE_DIR}/src/ymfm_opm.cpp)
target_include_directories(ymfm_opm PUBLIC ${ymfm_SOURCE_DIR}/src)
# the YM3438 (OPN2) for System 32
add_library(ymfm_opn STATIC ${ymfm_SOURCE_DIR}/src/ymfm_opn.cpp ${ymfm_SOURCE_DIR}/src/ymfm_adpcm.cpp ${ymfm_SOURCE_DIR}/src/ymfm_ssg.cpp)
target_include_directories(ymfm_opn PUBLIC ${ymfm_SOURCE_DIR}/src)
if(NOT MSVC)
    target_compile_options(ymfm_opm PRIVATE -w)
    target_compile_options(ymfm_opn PRIVATE -w)
endif()

# MAME's Z80 step machine, generated from z80.lst by MAME's z80make.py
set(Z80_HXX ${CMAKE_BINARY_DIR}/z80gen/z80.hxx)
add_custom_command(OUTPUT ${Z80_HXX}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/z80gen
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/src/z80/mame/z80make.py ${CMAKE_SOURCE_DIR}/src/z80/mame/z80.lst ${Z80_HXX}
    DEPENDS ${CMAKE_SOURCE_DIR}/src/z80/mame/z80make.py ${CMAKE_SOURCE_DIR}/src/z80/mame/z80.lst
    COMMENT "Generating the Z80 step machine")
add_custom_target(z80gen DEPENDS ${Z80_HXX})

# What the boards share: the scheduler, the 68000 runtime, the Z80, the
# sound section, the video and ROM loading
add_library(c2d_common STATIC
    src/outrun/screen.cpp
    src/outrun/sound.cpp
    src/outrun/video.cpp
    src/outrun/m68k_exec.cpp
    src/cpu68k/interp.cpp
    src/m68k/decode.cpp
    src/fd1094/opcode_image.cpp
    src/sched/scheduler.cpp
    src/z80/z80.cpp
    src/runtime/zip.cpp
    src/runtime/rom_source.cpp
    src/fd1094/fd1094.cpp)
add_dependencies(c2d_common z80gen)
target_include_directories(c2d_common PUBLIC src include PRIVATE ${CMAKE_BINARY_DIR}/z80gen)
target_link_libraries(c2d_common PUBLIC ymfm_opm)

# The boards (everything but the recompiled programs)
add_library(outrun_board STATIC src/outrun/board.cpp)
target_link_libraries(outrun_board PUBLIC c2d_common)
add_library(xboard_board STATIC src/xboard/board.cpp)
target_link_libraries(xboard_board PUBLIC c2d_common)
add_library(system32_board STATIC src/system32/board.cpp src/system32/video.cpp src/system32/v60_exec.cpp
    src/runtime/eeprom93c46.cpp src/runtime/multipcm.cpp src/runtime/rf5c68.cpp src/v60/native.cpp)
target_link_libraries(system32_board PUBLIC c2d_common ymfm_opn)

# ROM import for every set (no board code: it only reads the ROMs)
add_library(c2d_import STATIC src/outrun/rom_import.cpp src/xboard/rom_import.cpp src/system32/rom_import.cpp)
target_link_libraries(c2d_import PUBLIC c2d_common)

# Host tools: the 68000 recompiler and the program images it reads
if(C2D_TOOLS AND NOT CMAKE_CROSSCOMPILING)
    add_executable(m68krecomp tools/m68krecomp/main.cpp src/m68k/decode.cpp src/m68k/reach.cpp)
    target_include_directories(m68krecomp PRIVATE src)
    add_executable(v60recomp tools/v60recomp/main.cpp tools/v60recomp/sym.cpp)
    target_include_directories(v60recomp PRIVATE src tools/v60recomp)
    add_executable(c2d_import_tool tools/c2d_import/main.cpp)
    set_target_properties(c2d_import_tool PROPERTIES OUTPUT_NAME c2d_import)
    target_link_libraries(c2d_import_tool PRIVATE c2d_import)
endif()

set(GEN_DIR ${C2D_GEN_ROOT}/${C2D_ROMSET})
if(C2D_BOARD STREQUAL "system32")
    # v60recomp writes table.cpp and its chunks
    file(GLOB GEN_SOURCES ${GEN_DIR}/*.cpp)
    set(C2D_HAVE_GEN FALSE)
    if(EXISTS ${GEN_DIR}/table.cpp)
        set(C2D_HAVE_GEN TRUE)
    endif()
else()
    set(GEN_SOURCES ${GEN_DIR}/gen_main.cpp ${GEN_DIR}/gen_sub.cpp)
    set(C2D_HAVE_GEN FALSE)
    if(EXISTS ${GEN_DIR}/gen_main.cpp AND EXISTS ${GEN_DIR}/gen_sub.cpp)
        set(C2D_HAVE_GEN TRUE)
    endif()
endif()
if(C2D_CORE AND C2D_HAVE_GEN)
    if(NOT MSVC)
        set_source_files_properties(${GEN_SOURCES} PROPERTIES COMPILE_OPTIONS "-O1;-Wno-unused-label;-Wno-unused-variable;-Wno-unused-but-set-variable")
    endif()
    add_library(afcore_${C2D_ROMSET} SHARED src/${C2D_BOARD}/core.cpp ${GEN_SOURCES})
    target_link_libraries(afcore_${C2D_ROMSET} PRIVATE ${C2D_BOARD}_board c2d_import)
    target_compile_definitions(afcore_${C2D_ROMSET} PRIVATE AFCORE_BUILD)
    if(C2D_ROMSET STREQUAL "shangon")
        target_compile_definitions(afcore_${C2D_ROMSET} PRIVATE C2D_SHANGON)
    elseif(C2D_ROMSET STREQUAL "toutrun")
        target_compile_definitions(afcore_${C2D_ROMSET} PRIVATE C2D_TOUTRUN)
    elseif(C2D_ROMSET STREQUAL "thndrbld")
        target_compile_definitions(afcore_${C2D_ROMSET} PRIVATE C2D_THNDRBLD)
    elseif(C2D_ROMSET STREQUAL "thndrbld1")
        target_compile_definitions(afcore_${C2D_ROMSET} PRIVATE C2D_THNDRBLD1)
    elseif(C2D_ROMSET STREQUAL "radr")
        target_compile_definitions(afcore_${C2D_ROMSET} PRIVATE C2D_RADR)
    endif()
    set_target_properties(afcore_${C2D_ROMSET} PROPERTIES OUTPUT_NAME afcore_${C2D_ROMSET}
        CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)
    if(APPLE)
        if(C2D_FRAMEWORK)
            set_target_properties(afcore_${C2D_ROMSET} PROPERTIES FRAMEWORK TRUE
                MACOSX_FRAMEWORK_IDENTIFIER org.arcadefoundry.core.${C2D_ROMSET}
                MACOSX_FRAMEWORK_SHORT_VERSION_STRING 1.0
                MACOSX_FRAMEWORK_BUNDLE_VERSION 1
                XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED NO)
        endif()
        target_link_options(afcore_${C2D_ROMSET} PRIVATE "LINKER:-exported_symbol,_afcore_api_${C2D_ROMSET}")
    elseif(NOT WIN32)
        file(WRITE ${CMAKE_BINARY_DIR}/afcore.map "{ global: afcore_api_${C2D_ROMSET}; local: *; };\n")
        target_link_options(afcore_${C2D_ROMSET} PRIVATE "LINKER:--version-script=${CMAKE_BINARY_DIR}/afcore.map")
    endif()

    # The game against the core library's interface, headless: frames, an
    # input replay, frame and audio dumps (tools/afcore_run)
    if(NOT CMAKE_CROSSCOMPILING)
        add_executable(afcore_run tools/afcore_run/main.cpp)
        target_include_directories(afcore_run PRIVATE include)
        target_compile_definitions(afcore_run PRIVATE AFCORE_SET=${C2D_ROMSET})
        target_link_libraries(afcore_run PRIVATE afcore_${C2D_ROMSET} ${CMAKE_DL_LIBS})
    endif()
elseif(C2D_CORE)
    message(STATUS "No recompiled code in ${GEN_DIR}: run scripts/recompile.py first (the core is not built)")
endif()
