cmake_minimum_required(VERSION 3.20)

project(th08-modern LANGUAGES CXX)

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(TH08_PORTABLE_NATIVE_LAYOUT ON)
elseif(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "TH08 requires a 32-bit or 64-bit toolchain.")
endif()

find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(TH08_DX8_SDK_ROOT "${CMAKE_SOURCE_DIR}/scripts/prefix/mssdk" CACHE PATH
    "DirectX 8 SDK root containing include/ and lib/")
if(WIN32 AND NOT EXISTS "${TH08_DX8_SDK_ROOT}/include/d3dx8.h")
    message(FATAL_ERROR
        "DirectX 8 SDK headers were not found. Set TH08_DX8_SDK_ROOT or run the existing prefix setup first.")
endif()

set(TH08_GENERATED_DIR "${CMAKE_BINARY_DIR}/generated")
add_custom_command(
    OUTPUT "${TH08_GENERATED_DIR}/i18n.hpp"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${TH08_GENERATED_DIR}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/scripts/generate_i18n.py"
            -o "${TH08_GENERATED_DIR}/i18n.hpp" --shift-jis
            "${CMAKE_SOURCE_DIR}/config/i18n.csv"
    DEPENDS
        "${CMAKE_SOURCE_DIR}/config/i18n.csv"
        "${CMAKE_SOURCE_DIR}/scripts/generate_i18n.py"
    VERBATIM
)
add_custom_target(th08-modern-generated DEPENDS "${TH08_GENERATED_DIR}/i18n.hpp")

set(TH08_GAME_SOURCES
    src/AnmManager.cpp
    src/AsciiManager.cpp
    src/AsciiManagerBossMarker.cpp
    src/AsciiManagerGauge.cpp
    src/AsciiManagerGuiMode.cpp
    src/AsciiManagerScale.cpp
    src/Background.cpp
    src/BulletManager.cpp
    src/EclDependencies.cpp
    src/EclExIns.cpp
    src/EclGlobals.cpp
    src/EclHelpers.cpp
    src/EclManager.cpp
    src/EclOperandsFloat.cpp
    src/EclOperandsInt.cpp
    src/EclRun.cpp
    src/EffectManager.cpp
    src/Ending.cpp
    src/EnemyManager.cpp
    src/EnemyManagerUpdate.cpp
    src/EnemyTimeline.cpp
    src/GameManager.cpp
    src/Global.cpp
    src/Gui.cpp
    src/ItemManager.cpp
    src/Midi.cpp
    src/MusicRoom.cpp
    src/Player.cpp
    src/PlayerBomb.cpp
    src/ReplayManager.cpp
    src/ResultScreen.cpp
    src/ScoreDat.cpp
    src/ScreenEffect.cpp
    src/SoundPlayer.cpp
    src/Spellcard.cpp
    src/Supervisor.cpp
    src/TextHelper.cpp
    src/TitleScreen.cpp
    src/main.cpp
    src/utils.cpp
    src/zwave.cpp
    src/pbg/Lzss.cpp
    src/pbg/PbgArchive.cpp
    src/pbg/PbgFile.cpp
)

if(WIN32)
    list(APPEND TH08_GAME_SOURCES src/modern/windows_runtime.cpp)
    add_executable(th08-modern WIN32 ${TH08_GAME_SOURCES})
elseif(ANDROID)
    # Android loads a shared library from SDLActivity. SDL's bootstrap renames
    # main to SDL_main, which linux_runtime.cpp provides, so the entry path is
    # shared with the desktop software build.
    set(TH08_ANDROID_ADAPTER_SOURCES
        src/modern/software/software_d3d8.cpp
        src/modern/software/software_compat.cpp
        src/modern/software/sdl_present.cpp
        src/modern/linux/d3dx8_compat.cpp
        src/modern/linux/linux_compat.cpp
        src/modern/linux/linux_runtime.cpp
        src/modern/touch/touch_controls.cpp
    )
    list(APPEND TH08_GAME_SOURCES ${TH08_ANDROID_ADAPTER_SOURCES})
    add_library(th08-modern SHARED ${TH08_GAME_SOURCES})
elseif(UNIX AND NOT APPLE)
    option(TH08_SOFTWARE_RENDERER "Use the CPU Direct3D 8 back end (Android port)" OFF)
    if(TH08_SOFTWARE_RENDERER)
        set(TH08_SOFTWARE_ADAPTER_SOURCES
            src/modern/software/software_d3d8.cpp
            src/modern/software/software_compat.cpp
            src/modern/software/sdl_present.cpp
            src/modern/linux/d3dx8_compat.cpp
            src/modern/linux/linux_compat.cpp
            src/modern/linux/linux_runtime.cpp
            src/modern/touch/touch_controls.cpp
        )
        list(APPEND TH08_GAME_SOURCES ${TH08_SOFTWARE_ADAPTER_SOURCES})
    else()
        set(TH08_LINUX_ADAPTER_SOURCES
            src/modern/linux/d3d8_compat.cpp
            src/modern/linux/d3dx8_compat.cpp
            src/modern/linux/linux_compat.cpp
            src/modern/linux/linux_runtime.cpp
            src/modern/linux/render_audit.cpp
        )
        set(TH08_LINUX_HOT_ADAPTER_SOURCES
            src/modern/linux/d3d8_compat.cpp
            src/modern/linux/d3dx8_compat.cpp
            src/modern/linux/linux_compat.cpp
        )
        list(APPEND TH08_GAME_SOURCES ${TH08_LINUX_ADAPTER_SOURCES})
    endif()
    add_executable(th08-modern ${TH08_GAME_SOURCES})
else()
    message(FATAL_ERROR "The modern target currently supports Windows and Linux.")
endif()
add_dependencies(th08-modern th08-modern-generated)
option(TH08_ENABLE_REPLAY_TRACE "Enable portable replay state test hooks" OFF)
if(TH08_ENABLE_REPLAY_TRACE)
    if(NOT UNIX OR APPLE)
        message(FATAL_ERROR "Replay state test hooks currently require Linux.")
    endif()
    target_sources(th08-modern PRIVATE src/modern/linux/replay_trace.cpp)
    target_compile_definitions(th08-modern PRIVATE TH08_ENABLE_REPLAY_TRACE)
endif()
target_compile_features(th08-modern PRIVATE cxx_std_98)
target_compile_definitions(th08-modern PRIVATE
    TH08_MODERN_PORT
    WIN32_LEAN_AND_MEAN
)
if(TH08_PORTABLE_NATIVE_LAYOUT)
    target_compile_definitions(th08-modern PRIVATE TH08_PORTABLE_NATIVE_LAYOUT)
endif()
target_include_directories(th08-modern PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${TH08_GENERATED_DIR}"
)

if(MINGW)
    # MinGW ships compatible D3D8/DirectInput/DirectSound headers and import
    # libraries, but not D3DX8. Search the legacy SDK only after the toolchain
    # headers so its old Windows SDK does not shadow modern system headers.
    set(TH08_DX8_SANITIZED_INCLUDE "${CMAKE_BINARY_DIR}/dx8-include")
    file(COPY "${TH08_DX8_SDK_ROOT}/include/" DESTINATION "${TH08_DX8_SANITIZED_INCLUDE}")
    file(GLOB TH08_DX8_HEADERS "${TH08_DX8_SANITIZED_INCLUDE}/*")
    string(ASCII 26 TH08_DOS_EOF)
    foreach(TH08_DX8_HEADER IN LISTS TH08_DX8_HEADERS)
        if(NOT IS_DIRECTORY "${TH08_DX8_HEADER}")
            file(READ "${TH08_DX8_HEADER}" TH08_DX8_HEADER_CONTENT)
            string(REPLACE "${TH08_DOS_EOF}" "" TH08_DX8_HEADER_CONTENT "${TH08_DX8_HEADER_CONTENT}")
            file(WRITE "${TH08_DX8_HEADER}" "${TH08_DX8_HEADER_CONTENT}")
        endif()
    endforeach()

    target_compile_options(th08-modern PRIVATE
        # The reconstructed source preserves the original VC7 /Od boundaries;
        # cross-TU inline bodies are not available for modern optimization.
        -O0
        -fpermissive
        -mms-bitfields
        -Wno-write-strings
        "-include${CMAKE_SOURCE_DIR}/src/modern/windows_compat.hpp"
        "-idirafter${TH08_DX8_SANITIZED_INCLUDE}"
    )
    target_link_options(th08-modern PRIVATE
        -static
        -static-libgcc
        -static-libstdc++
        # Some reconstructed data references still use target absolute VAs.
        # Keep the original image base until every such owner is symbolic.
        -Wl,--disable-dynamicbase
    )
    target_link_libraries(th08-modern PRIVATE
        # The retail D3DX8 library is a static MSVC C++ archive and cannot be
        # mixed with MinGW's C++ ABI. The SDK's debug DLL exposes the same C
        # API through an import library, which is suitable for bring-up builds.
        "${TH08_DX8_SDK_ROOT}/lib/d3dx8d.lib"
        dxguid dinput8 dsound d3d8 winmm
        kernel32 user32 gdi32 advapi32 shell32 ole32 oleaut32 uuid dbghelp
    )
    set(TH08_D3DX8_DEBUG_DLL
        "${TH08_DX8_SDK_ROOT}/samples/multimedia/support/d3dx8d.dll")
    if(EXISTS "${TH08_D3DX8_DEBUG_DLL}")
        add_custom_command(TARGET th08-modern POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                    "${TH08_D3DX8_DEBUG_DLL}" "$<TARGET_FILE_DIR:th08-modern>"
            VERBATIM
        )
    else()
        message(WARNING "d3dx8d.dll was not found; th08-modern.exe will build but needs that SDK DLL to start.")
    endif()
elseif(MSVC)
    target_include_directories(th08-modern SYSTEM PRIVATE "${TH08_DX8_SDK_ROOT}/include")
    target_link_directories(th08-modern PRIVATE "${TH08_DX8_SDK_ROOT}/lib")
    target_compile_options(th08-modern PRIVATE /W3)
    target_link_options(th08-modern PRIVATE /DYNAMICBASE:NO)
    target_link_libraries(th08-modern PRIVATE
        dxguid d3dx8 dinput8 dsound d3d8 winmm
        kernel32 user32 gdi32 advapi32 shell32 ole32 oleaut32 uuid dbghelp
    )
elseif(ANDROID)
    # SDL and the text stack are built from the vendored source trees that
    # scripts/build-android.sh fetches into 3rdparty/. Gradle points its
    # native build at this file for both ABIs.
    if(NOT TARGET SDL2::SDL2)
        set(SDL_SHARED ON CACHE BOOL "Build SDL2 as a shared library" FORCE)
        set(SDL_STATIC OFF CACHE BOOL "" FORCE)
        add_subdirectory("${CMAKE_SOURCE_DIR}/3rdparty/SDL2" "${CMAKE_BINARY_DIR}/SDL2")
    endif()
    if(NOT TARGET SDL2_image::SDL2_image)
        add_subdirectory("${CMAKE_SOURCE_DIR}/3rdparty/SDL2_image" "${CMAKE_BINARY_DIR}/SDL2_image")
    endif()
    if(NOT TARGET SDL2_ttf::SDL2_ttf)
        add_subdirectory("${CMAKE_SOURCE_DIR}/3rdparty/SDL2_ttf" "${CMAKE_BINARY_DIR}/SDL2_ttf")
    endif()
    if(NOT TARGET freetype)
        # linux_compat measures GDI font heights through FreeType directly.
        # Keep the build independent of any system library: Android has none.
        set(FT_DISABLE_ZLIB ON CACHE BOOL "" FORCE)
        set(FT_DISABLE_BZIP2 ON CACHE BOOL "" FORCE)
        set(FT_DISABLE_PNG ON CACHE BOOL "" FORCE)
        set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
        add_subdirectory("${CMAKE_SOURCE_DIR}/3rdparty/freetype" "${CMAKE_BINARY_DIR}/freetype")
    endif()

    target_compile_definitions(th08-modern PRIVATE
        TH08_MODERN_LINUX
        TH08_SOFTWARE_RENDERER
        # The original fixed-address layout needs a non-PIE executable, which
        # Android never provides. Both ABIs therefore run the symbolic layout
        # that the portable-64bit line already validates on 64-bit hosts.
        TH08_PORTABLE_NATIVE_LAYOUT
        _FILE_OFFSET_BITS=64
        _TIME_BITS=64
    )
    target_compile_options(th08-modern PRIVATE
        # The reconstructed game TUs keep the original /Od-style boundaries.
        -O0
        -fpermissive
        -fdata-sections
        -fno-omit-frame-pointer
        -Wno-write-strings
        # The reconstruction takes the address of D3DX temporaries where VC7
        # did; the pointed-to values are only read during the call. GCC has no
        # such diagnostic, and clang makes this one an error by default. The
        # same applies to printf(variable) call sites kept from VC7, which the
        # NDK turns into errors through -Werror=format-security.
        -Wno-address-of-temporary
        -Wno-format-security
        "-include${CMAKE_SOURCE_DIR}/src/modern/linux/linux_compat.hpp"
    )
    # Everything above the adapter boundary is new platform code, including the
    # CPU rasterizer, which must run at -O2 to hold the 60 Hz frame budget on a
    # phone.
    set_source_files_properties(${TH08_ANDROID_ADAPTER_SOURCES} PROPERTIES
        COMPILE_OPTIONS "-O2;-fno-strict-aliasing"
    )
    target_include_directories(th08-modern BEFORE PRIVATE
        "${CMAKE_SOURCE_DIR}/src/modern/linux/include"
    )
    target_link_libraries(th08-modern PRIVATE
        SDL2::SDL2
        SDL2_image::SDL2_image
        SDL2_ttf::SDL2_ttf
        freetype
        log
        android
    )
elseif(UNIX AND NOT APPLE)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
    pkg_check_modules(SDL2_IMAGE REQUIRED IMPORTED_TARGET SDL2_image)
    pkg_check_modules(SDL2_TTF REQUIRED IMPORTED_TARGET SDL2_ttf)
    find_package(Fontconfig REQUIRED)
    find_package(Freetype REQUIRED)
    find_package(Threads REQUIRED)
    if(NOT TH08_SOFTWARE_RENDERER)
        # Prefer the broadly deployed libGL.so.1 runtime ABI over GLVND's separate
        # libOpenGL.so.0 dispatch library for the 32-bit portable build.
        set(OpenGL_GL_PREFERENCE LEGACY)
        find_package(OpenGL REQUIRED)
    endif()

    target_compile_definitions(th08-modern PRIVATE
        TH08_MODERN_LINUX
        _FILE_OFFSET_BITS=64
        _TIME_BITS=64
    )
    if(TH08_SOFTWARE_RENDERER)
        target_compile_definitions(th08-modern PRIVATE TH08_SOFTWARE_RENDERER)
    endif()
    target_compile_options(th08-modern PRIVATE
        # Keep the original /Od-style TU boundaries in every CMake build type.
        -O0
        -fpermissive
        -fdata-sections
        -fno-omit-frame-pointer
        -Wno-write-strings
        "-include${CMAKE_SOURCE_DIR}/src/modern/linux/linux_compat.hpp"
    )
    # The sources above this adapter boundary are reconstructed VC7 game TUs
    # and deliberately retain -O0. These files are the new SDL/OpenGL platform
    # implementation, including the per-vertex renderer and pixel converters;
    # compiling those handwritten hot paths at -O0 needlessly consumes the
    # frame-time budget needed to sustain the port's 60 Hz target under load.
    if(TH08_PORTABLE_NATIVE_LAYOUT)
        set_source_files_properties(${TH08_LINUX_HOT_ADAPTER_SOURCES} PROPERTIES
            COMPILE_OPTIONS "-O2;-fno-strict-aliasing"
        )
    endif()
    target_link_options(th08-modern PRIVATE -rdynamic)
    if(NOT TH08_PORTABLE_NATIVE_LAYOUT)
        target_compile_options(th08-modern PRIVATE -fno-pie)
        target_link_options(th08-modern PRIVATE
            -no-pie
            "-Wl,-T,${CMAKE_SOURCE_DIR}/src/modern/linux/th08-layout.ld"
        )
        set_property(TARGET th08-modern APPEND PROPERTY LINK_DEPENDS
            "${CMAKE_SOURCE_DIR}/src/modern/linux/th08-layout.ld"
        )
    endif()
    target_include_directories(th08-modern BEFORE PRIVATE
        "${CMAKE_SOURCE_DIR}/src/modern/linux/include"
    )
    target_link_libraries(th08-modern PRIVATE
        PkgConfig::SDL2
        PkgConfig::SDL2_IMAGE
        PkgConfig::SDL2_TTF
        Fontconfig::Fontconfig
        Freetype::Freetype
        Threads::Threads
        dl
    )
    if(NOT TH08_SOFTWARE_RENDERER)
        target_link_libraries(th08-modern PRIVATE OpenGL::GL)
    endif()
    add_custom_command(TARGET th08-modern POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${CMAKE_SOURCE_DIR}/resources/modern-icon.png"
                "$<TARGET_FILE_DIR:th08-modern>/th08-modern.png"
        VERBATIM
    )
else()
    message(FATAL_ERROR "The modern Windows target currently supports MSVC and MinGW.")
endif()

set_target_properties(th08-modern PROPERTIES OUTPUT_NAME "th08-modern")
