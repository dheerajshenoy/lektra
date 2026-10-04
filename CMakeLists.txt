cmake_minimum_required(VERSION 3.22)
project(lektra VERSION 0.7.9 LANGUAGES C CXX)

include(GNUInstallDirs)

# Single source of truth for the project version, exposed to the Inno Setup
# installer script (lektra.iss) so it doesn't need its own hardcoded copy.
file(WRITE "${CMAKE_BINARY_DIR}/version.txt" "${PROJECT_VERSION}")

option(WITH_PCH "Use a precompiled header to speed up compilation" ON)
option(WITH_SYNCTEX "Enable synctex support" ON)
option(WITH_FONTCONFIG "Look up fonts via fontconfig when available (otherwise scan the system font folders)" ON)
option(WITH_LIBARCHIVE "Open RAR/7z comic books (cbr, cb7) via libarchive when available" ON)
# Lua scripting (LuaJIT, built from thirdparty/luajit and linked statically).
option(WITH_LUA     "Enable lua scripting support (bundled LuaJIT)" ON)
option(WITH_LLM_SUPPORT "Enable LLM support" OFF)

# Internal option to build as AppImage (Linux only)
option(IS_APPIMAGE "Build as AppImage (Linux only)" OFF)


# ---------------------------------------------------------------------------
# Build type default
# ---------------------------------------------------------------------------
if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type")
endif()

message(STATUS "Build type:          ${CMAKE_BUILD_TYPE}")
message(STATUS "Installation prefix: ${CMAKE_INSTALL_PREFIX}")

# ---------------------------------------------------------------------------
# Compiler / language settings
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_COLOR_DIAGNOSTICS ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

if(MSVC)
    add_compile_options(/Zc:__cplusplus /Zc:preprocessor)
    set(DEBUG_FLAGS          /Od;/Zi;/RTC1;/W4;/fsanitize=address)
    set(RELEASE_FLAGS        /O2;/DNDEBUG;/GR-;/W4)
    set(REL_WITH_DEB_INFO_FLAGS /O2;/Zi;/DNDEBUG;/W4)
else()
    set(DEBUG_FLAGS          -fno-omit-frame-pointer;-ggdb3;-O0;-fsanitize=address;-Wall;-Wextra)
    # -g/-fno-omit-frame-pointer are kept in Release too (not just
    # RelWithDebInfo): without them, CrashHandler's backtrace() has nothing
    # reliable to unwind and no line info to resolve against, so a crash from
    # a Release build produces a useless report. The extra debug info can be
    # split out with `objcopy --only-keep-debug` + `--add-gnu-debuglink` at
    # packaging time if a slimmer shipped binary is wanted later.
    set(RELEASE_FLAGS        -O3;-g;-fno-omit-frame-pointer;-DNDEBUG;-fno-rtti)
    set(REL_WITH_DEB_INFO_FLAGS -O2;-g;-fno-omit-frame-pointer;-DNDEBUG)
endif()

# ---------------------------------------------------------------------------
# Platform helpers  (CMake does not define LINUX; derive it)
# ---------------------------------------------------------------------------
if(UNIX AND NOT APPLE)
    set(PLATFORM_LINUX TRUE)
endif()

# ---------------------------------------------------------------------------
# Compile-time definitions
# ---------------------------------------------------------------------------
add_compile_definitions(
    APP_BUILD_TYPE="${CMAKE_BUILD_TYPE}"
    APP_INSTALL_PREFIX="${CMAKE_INSTALL_PREFIX}"
    APP_VERSION="${PROJECT_VERSION}"
    APP_NAME="${PROJECT_NAME}"
    APP_VERSION_MAJOR="${PROJECT_VERSION_MAJOR}"
    APP_VERSION_MINOR="${PROJECT_VERSION_MINOR}"
    APP_VERSION_PATCH="${PROJECT_VERSION_PATCH}"
)

# ---------------------------------------------------------------------------
# MuPDF (ExternalProject)
# ---------------------------------------------------------------------------
include(ExternalProject)
set(MUPDF_INSTALL_DIR "${CMAKE_BINARY_DIR}/mupdf-install")
set(MUPDF_SOURCE_DIR  "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/mupdf")

# Detect MuPDF version from source header
set(MUPDF_VERSION_HEADER "${MUPDF_SOURCE_DIR}/include/mupdf/fitz/version.h")
if(EXISTS "${MUPDF_VERSION_HEADER}")
    file(READ "${MUPDF_VERSION_HEADER}" _mupdf_ver_contents)
    string(REGEX MATCH "#define FZ_VERSION \"([^\"]+)\"" _ "${_mupdf_ver_contents}")
    if(CMAKE_MATCH_1)
        set(MUPDF_VERSION "${CMAKE_MATCH_1}")
        message(STATUS "MuPDF version: ${MUPDF_VERSION}")
    else()
        message(FATAL_ERROR
            "Could not parse MuPDF version from ${MUPDF_VERSION_HEADER}. "
            "Ensure the mupdf submodule is initialised (git submodule update --init).")
    endif()
    set(MUPDF_FOUND TRUE)
    set(MUPDF_INCLUDE_DIRS "${MUPDF_SOURCE_DIR}/include")
else()
    message(FATAL_ERROR
        "MuPDF source not found at ${MUPDF_SOURCE_DIR}. "
        "Initialise the submodule: git submodule update --init thirdparty/mupdf")
endif()


if(MSVC)
    set(MUPDF_SLN "${MUPDF_SOURCE_DIR}/platform/win32/mupdf.sln")
    set(MUPDF_LIB_FILE "${MUPDF_INSTALL_DIR}/lib/libmupdf.lib")

    ExternalProject_Add(mupdf_external
        SOURCE_DIR "${MUPDF_SOURCE_DIR}"
        CONFIGURE_COMMAND ""

        BUILD_COMMAND
        MSBuild "${MUPDF_SLN}"
        /p:Configuration=$<CONFIG>
        /p:Platform=x64
        /p:PlatformToolset=${CMAKE_VS_PLATFORM_TOOLSET}
        /m

        INSTALL_COMMAND
        ${CMAKE_COMMAND} -E make_directory "${MUPDF_INSTALL_DIR}/lib"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${MUPDF_SOURCE_DIR}/platform/win32/x64/Release/libmupdf.lib"
        MUPDF_LIB_FILE

        BUILD_BYPRODUCTS "${MUPDF_LIB_FILE}"
        UPDATE_COMMAND ""
    )
else()
    set(MUPDF_LIB_FILE "${MUPDF_INSTALL_DIR}/lib/libmupdf.a")
    set(MUPDF_LIB_THIRD_FILE "${MUPDF_INSTALL_DIR}/lib/libmupdf-third.a")

    ExternalProject_Add(mupdf_external
        SOURCE_DIR "${MUPDF_SOURCE_DIR}"
        CONFIGURE_COMMAND ""
        BUILD_COMMAND
        make -C <SOURCE_DIR> build=release shared=no
        HAVE_TESSERACT=no HAVE_CURL=no HAVE_X11=no HAVE_GLUT=no HAVE_LIBCRYPTO=no USE_SYSTEM_LIBS=no
        LIB_CFLAGS=-fPIC libs
        INSTALL_COMMAND
        make -C <SOURCE_DIR> build=release shared=no
        HAVE_TESSERACT=no HAVE_CURL=no HAVE_X11=no HAVE_GLUT=no HAVE_LIBCRYPTO=no USE_SYSTEM_LIBS=no
        prefix="${MUPDF_INSTALL_DIR}" install-libs
        BUILD_BYPRODUCTS "${MUPDF_LIB_FILE}" "${MUPDF_LIB_THIRD_FILE}"
        UPDATE_COMMAND ""
    )
endif()

# ---------------------------------------------------------------------------
# Qt6 + optional system libraries
# ---------------------------------------------------------------------------
find_package(Qt6 REQUIRED COMPONENTS
    Core Gui Widgets Concurrent LinguistTools OpenGLWidgets Network Svg)


# ---------------------------------------------------------------------------
# LuaJIT (ExternalProject, linked statically)
# ---------------------------------------------------------------------------
if (WITH_LUA)
    set(LUAJIT_SOURCE_DIR  "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/luajit")
    set(LUAJIT_BUILD_DIR   "${CMAKE_BINARY_DIR}/luajit-src")
    set(LUAJIT_INSTALL_DIR "${CMAKE_BINARY_DIR}/luajit-install")

    if (NOT EXISTS "${LUAJIT_SOURCE_DIR}/src/lua.h")
        message(FATAL_ERROR
            "LuaJIT source not found at ${LUAJIT_SOURCE_DIR}. "
            "Initialise the submodule: git submodule update --init thirdparty/luajit "
            "(or configure with -DWITH_LUA=OFF).")
    endif()

    # LuaJIT builds in its own source tree, so build from a copy in the build
    # directory and leave the submodule checkout clean.
    #
    # Its version number comes from git, which does not work in a copy (the
    # submodule's .git is a pointer relative to its original location). Record
    # the commit time now and hand it over as .relver, which both the Makefile
    # and msvcbuild.bat fall back to when there is no .git.
    execute_process(
        COMMAND git -C "${LUAJIT_SOURCE_DIR}" show -s --format=%ct
        OUTPUT_VARIABLE _luajit_relver
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
    if (NOT _luajit_relver)
        set(_luajit_relver 0)
    endif()
    file(WRITE "${CMAKE_BINARY_DIR}/luajit.relver" "${_luajit_relver}\n")
    set(_luajit_copy
        ${CMAKE_COMMAND} -E copy_directory "${LUAJIT_SOURCE_DIR}" "${LUAJIT_BUILD_DIR}"
        COMMAND ${CMAKE_COMMAND} -E remove -f "${LUAJIT_BUILD_DIR}/.git"
        COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_BINARY_DIR}/luajit.relver"
                "${LUAJIT_BUILD_DIR}/.relver")

    if (MSVC)
        set(LUAJIT_LIB_FILE    "${LUAJIT_BUILD_DIR}/src/lua51.lib")
        set(LUAJIT_INCLUDE_DIR "${LUAJIT_BUILD_DIR}/src")

        # msvcbuild.bat must run inside a Visual Studio developer environment
        # (it stops if INCLUDE is not set). A build started from the IDE, or
        # from a plain prompt, does not have one, so the wrapper sets it up
        # from the vcvarsall.bat that belongs to the compiler CMake picked:
        #   <VS>/VC/Tools/MSVC/<ver>/bin/Host<arch>/<arch>/cl.exe
        #   <VS>/VC/Auxiliary/Build/vcvarsall.bat
        set(_luajit_vcvars "")
        get_filename_component(_cl_dir "${CMAKE_C_COMPILER}" DIRECTORY)
        foreach (_i RANGE 5)
            get_filename_component(_cl_dir "${_cl_dir}" DIRECTORY)
        endforeach ()
        if (EXISTS "${_cl_dir}/Auxiliary/Build/vcvarsall.bat")
            set(_luajit_vcvars "${_cl_dir}/Auxiliary/Build/vcvarsall.bat")
        endif ()
        if (CMAKE_SYSTEM_PROCESSOR MATCHES "ARM64|arm64|aarch64")
            set(_luajit_vcarch "x64_arm64")
        else ()
            set(_luajit_vcarch "x64")
        endif ()
        file(TO_NATIVE_PATH "${_luajit_vcvars}" _luajit_vcvars_native)
        file(TO_NATIVE_PATH "${LUAJIT_BUILD_DIR}/src" _luajit_src_native)

        # The static library is built with the compiler's default runtime
        # (/MT) unless told otherwise; match what Lektra links (/MD, or /MDd
        # in Debug) through the CL environment variable.
        file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/luajit_build_$<CONFIG>.bat"
            CONTENT "@echo off
setlocal
if not defined INCLUDE (
    if exist \"${_luajit_vcvars_native}\" call \"${_luajit_vcvars_native}\" ${_luajit_vcarch} >nul
)
if not defined INCLUDE (
    echo LuaJIT: no Visual Studio environment found. Run from a Developer Command Prompt, or configure with -DWITH_LUA=OFF.
    exit /b 1
)
set CL=$<IF:$<CONFIG:Debug>,/MDd,/MD>
cd /d \"${_luajit_src_native}\"
call msvcbuild.bat lua52compat static
exit /b %errorlevel%
")

        ExternalProject_Add(luajit_external
            SOURCE_DIR "${LUAJIT_BUILD_DIR}"
            DOWNLOAD_COMMAND ${_luajit_copy}
            CONFIGURE_COMMAND ""
            BUILD_COMMAND cmd /c "${CMAKE_BINARY_DIR}/luajit_build_$<CONFIG>.bat"
            INSTALL_COMMAND ""
            BUILD_BYPRODUCTS "${LUAJIT_LIB_FILE}"
            UPDATE_COMMAND ""
        )
    else()
        set(LUAJIT_LIB_FILE    "${LUAJIT_INSTALL_DIR}/lib/libluajit-5.1.a")
        set(LUAJIT_INCLUDE_DIR "${LUAJIT_INSTALL_DIR}/include/luajit-2.1")

        # -fPIC: the static library is linked into a position-independent
        # executable. LUA52COMPAT adds the Lua 5.2 features that are cheap
        # and widely used in scripts (goto, __len on tables, ...).
        set(_luajit_make_args
            BUILDMODE=static
            "XCFLAGS=-DLUAJIT_ENABLE_LUA52COMPAT -fPIC")

        if (APPLE)
            # LuaJIT's Makefile refuses to build on macOS without this.
            if (CMAKE_OSX_DEPLOYMENT_TARGET)
                set(_luajit_macos_target "${CMAKE_OSX_DEPLOYMENT_TARGET}")
            else()
                set(_luajit_macos_target "11.0")
            endif()
            list(APPEND _luajit_make_args
                "MACOSX_DEPLOYMENT_TARGET=${_luajit_macos_target}")
        endif()

        ExternalProject_Add(luajit_external
            SOURCE_DIR "${LUAJIT_BUILD_DIR}"
            DOWNLOAD_COMMAND ${_luajit_copy}
            CONFIGURE_COMMAND ""
            BUILD_COMMAND make -C <SOURCE_DIR> ${_luajit_make_args}
            INSTALL_COMMAND make -C <SOURCE_DIR> ${_luajit_make_args}
                "PREFIX=${LUAJIT_INSTALL_DIR}" install
            BUILD_BYPRODUCTS "${LUAJIT_LIB_FILE}"
            UPDATE_COMMAND ""
        )
    endif()
    message(STATUS "Lua support: enabled (bundled LuaJIT, static)")
endif()

# Optional: lets EPUB/FB2 documents use any installed font via fontconfig.
find_package(PkgConfig QUIET)
if (WITH_FONTCONFIG AND PkgConfig_FOUND)
    pkg_check_modules(FONTCONFIG QUIET IMPORTED_TARGET fontconfig)
endif()
if (FONTCONFIG_FOUND)
    message(STATUS "fontconfig support: enabled (EPUB font_family)")
else()
    message(STATUS "fontconfig not used - EPUB font_family scans the system font folders")
endif()

# Optional: RAR/7z comic books (cbr, cb7). The installed MuPDF is usually built
# without libarchive, so Lektra unpacks these itself.
if (WITH_LIBARCHIVE AND PkgConfig_FOUND)
    pkg_check_modules(LIBARCHIVE QUIET IMPORTED_TARGET libarchive)
endif()
if (LIBARCHIVE_FOUND)
    message(STATUS "libarchive support: enabled (cbr, cb7)")
else()
    message(STATUS "libarchive not used - cbr/cb7 comic books unsupported")
endif()

message(STATUS "librsvg support: runtime probe (uses librsvg-2.so.2 + libcairo.so.2 if installed)")
message(STATUS "libexif support: runtime probe (uses libexif.so.12 if installed)")


# ---------------------------------------------------------------------------
# Source files
# ---------------------------------------------------------------------------
file(GLOB LEKTRA_CORE_HEADERS       CONFIGURE_DEPENDS include/*.hpp)
file(GLOB LEKTRA_ANNOTATION_HEADERS CONFIGURE_DEPENDS include/Annotations/*.hpp)
file(GLOB LEKTRA_COMMAND_HEADERS    CONFIGURE_DEPENDS include/Commands/*.hpp)

if (WITH_LUA)
    file(GLOB LEKTRA_LUA_HEADERS        CONFIGURE_DEPENDS include/lua/*.hpp)
endif()

if (WITH_LLM_SUPPORT)
    file(GLOB LEKTRA_LLM_HEADERS        CONFIGURE_DEPENDS include/llm/*.hpp)
endif()

set(app_icon_resource_windows "")
if(WIN32)
    set(app_icon_resource_windows "${CMAKE_CURRENT_SOURCE_DIR}/resources/lektra.rc")
endif()

qt_add_executable(${PROJECT_NAME}
    ${app_icon_resource_windows}
    src/main.cpp
    src/CrashHandler.cpp
    src/CrashReporterDialog.cpp
    src/Lektra.cpp
    src/lektra/Config.cpp
    src/lektra/Commands.cpp
    src/lektra/Menus.cpp
    src/lektra/Tabs.cpp
    src/lektra/Sessions.cpp
    src/lektra/Files.cpp
    src/lektra/Actions.cpp
    src/Statusbar.cpp
    src/Model.cpp
    src/model/Lifecycle.cpp
    src/model/PageCache.cpp
    src/model/Text.cpp
    src/model/Render.cpp
    src/model/Annotations.cpp
    src/model/Reflow.cpp
    src/PropertiesWidget.cpp
    src/AboutDialog.cpp
    src/DonateDialog.cpp
    src/GraphicsView.cpp
    src/GraphicsScene.cpp
    src/DocumentView.cpp
    src/documentview/Rendering.cpp
    src/documentview/Layout.cpp
    src/documentview/Selection.cpp
    src/documentview/Caret.cpp
    src/documentview/Links.cpp
    src/documentview/Connections.cpp
    src/StartupWidget.cpp
    src/MessageBar.cpp
    src/Annotation.cpp
    src/EditLastPagesWidget.cpp
    src/RecentFilesStore.cpp
    src/RecentFilesModel.cpp
    src/SearchBar.cpp
    src/SaveSessionDialog.cpp
    src/FloatingOverlayWidget.cpp
    src/HighlightSearchPicker.cpp
    src/utils.cpp
    src/WaitingSpinnerWidget.cpp
    src/CommandPicker.cpp
    src/TabWidget.cpp
    src/DocumentContainer.cpp
    src/ThumbnailView.cpp
    src/TabBar.cpp
    src/Picker.cpp
    src/RecentFilesPicker.cpp
    src/FilePicker.cpp
    src/OutlinePicker.cpp
    src/CommentSearchPicker.cpp
    src/ScrollBar.cpp
    src/ColorDialog.cpp
    src/Bookmark.cpp
    src/BookmarkManager.cpp
    src/BookmarkPicker.cpp

    ${LEKTRA_CORE_HEADERS}
    ${LEKTRA_ANNOTATION_HEADERS}
    ${LEKTRA_COMMAND_HEADERS}
)

if (WITH_LUA)
    # Glob for lua source files in the src/lua directory
    file(GLOB LEKTRA_LUA_SOURCES CONFIGURE_DEPENDS src/lua/*.cpp)
    target_sources(${PROJECT_NAME} PRIVATE
        ${LEKTRA_LUA_SOURCES}
        ${LEKTRA_LUA_HEADERS}
    )
    target_include_directories(${PROJECT_NAME} PRIVATE include/lua)
endif()

target_include_directories(${PROJECT_NAME} PRIVATE
    include
    ${CMAKE_CURRENT_BINARY_DIR}/include   # generated headers (TranslationsDir.hpp etc.)
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty
)

if (IS_APPIMAGE)
    message(STATUS "Building as AppImage")
    target_compile_definitions(${PROJECT_NAME} PRIVATE IS_APPIMAGE=1)
endif()


# ---------------------------------------------------------------------------
# ---------------------------------------------------------------------------
# Windows executable properties
# ---------------------------------------------------------------------------
if(WIN32)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        WIN32_EXECUTABLE TRUE   # use Windows subsystem (no console window)
    )
    # Qt6::EntryPoint provides the WinMain -> main shim required by the
    # Windows subsystem. Without it the linker can't find `main`.
endif()

# ---------------------------------------------------------------------------
# macOS bundle properties
# ---------------------------------------------------------------------------
if(APPLE)
    set(LEKTRA_BUNDLE_NAME "Lektra")
    set(LEKTRA_BUNDLE_IDENTIFIER "org.lektra.lektra")
    set(LEKTRA_BUNDLE_ICON_FILE "lektra.icns")
    set(LEKTRA_BUNDLE_INFO_STRING "${PROJECT_NAME} ${PROJECT_VERSION}")
    set(LEKTRA_BUNDLE_COPYRIGHT "Copyright 2026 Lektra contributors")
    set(LEKTRA_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_BINARY_DIR}/Info.plist")

    set(MACOSX_BUNDLE_EXECUTABLE_NAME "${PROJECT_NAME}")
    set(MACOSX_BUNDLE_BUNDLE_NAME "${LEKTRA_BUNDLE_NAME}")
    set(MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}")
    set(MACOSX_BUNDLE_COPYRIGHT "${LEKTRA_BUNDLE_COPYRIGHT}")
    set(MACOSX_BUNDLE_GUI_IDENTIFIER "${LEKTRA_BUNDLE_IDENTIFIER}")
    set(MACOSX_BUNDLE_ICON_FILE "${LEKTRA_BUNDLE_ICON_FILE}")
    set(MACOSX_BUNDLE_INFO_STRING "${LEKTRA_BUNDLE_INFO_STRING}")
    set(MACOSX_BUNDLE_LONG_VERSION_STRING "${LEKTRA_BUNDLE_INFO_STRING}")
    set(MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}")
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Info.plist.in"
        "${LEKTRA_BUNDLE_INFO_PLIST}"
    )

    set_target_properties(${PROJECT_NAME} PROPERTIES
        MACOSX_BUNDLE                    TRUE
        MACOSX_BUNDLE_BUNDLE_NAME        "${LEKTRA_BUNDLE_NAME}"
        MACOSX_BUNDLE_BUNDLE_VERSION     "${PROJECT_VERSION}"
        MACOSX_BUNDLE_COPYRIGHT          "${LEKTRA_BUNDLE_COPYRIGHT}"
        MACOSX_BUNDLE_GUI_IDENTIFIER     "${LEKTRA_BUNDLE_IDENTIFIER}"
        MACOSX_BUNDLE_ICON_FILE          "${LEKTRA_BUNDLE_ICON_FILE}"
        MACOSX_BUNDLE_INFO_PLIST         "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Info.plist.in"
        MACOSX_BUNDLE_INFO_STRING        "${LEKTRA_BUNDLE_INFO_STRING}"
        MACOSX_BUNDLE_LONG_VERSION_STRING "${LEKTRA_BUNDLE_INFO_STRING}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    )

    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_BUNDLE_DIR:${PROJECT_NAME}>/Contents"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${LEKTRA_BUNDLE_INFO_PLIST}"
        "$<TARGET_BUNDLE_DIR:${PROJECT_NAME}>/Contents/Info.plist"
        VERBATIM
    )
endif()

# ---------------------------------------------------------------------------
# Qt resources
# ---------------------------------------------------------------------------
qt_add_resources(RESOURCES resources.qrc)
target_sources(${PROJECT_NAME} PRIVATE ${RESOURCES})

# ---------------------------------------------------------------------------
# Link libraries
# ---------------------------------------------------------------------------

add_dependencies(${PROJECT_NAME} mupdf_external)

# --- Qt ---
target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets
    Qt6::Concurrent Qt6::OpenGLWidgets Qt6::Network Qt6::Svg
)

if(NOT MSVC)
    # dladdr() — used by CrashHandler to resolve each stack frame's module
    # and load base for symbolization. Folded into libc on glibc >= 2.34,
    # but CMAKE_DL_LIBS is a no-op there and still correct on older glibc.
    target_link_libraries(${PROJECT_NAME} PRIVATE ${CMAKE_DL_LIBS})
endif()

# Qt6Network on Arch/Qt 6.11+ has an undeclared dep on libbrotlidec.
# Link it explicitly on Linux to avoid "DSO missing from command line".
if(PLATFORM_LINUX)
    find_library(BROTLIDEC_LIB  brotlidec)
    find_library(BROTLICOMMON_LIB brotlicommon)
    if(BROTLIDEC_LIB AND BROTLICOMMON_LIB)
        target_link_libraries(${PROJECT_NAME} PRIVATE
            ${BROTLIDEC_LIB} ${BROTLICOMMON_LIB})
    endif()
endif()

# --- MuPDF ---
target_include_directories(${PROJECT_NAME} PRIVATE ${MUPDF_INCLUDE_DIRS})
target_compile_definitions(${PROJECT_NAME} PRIVATE HAS_MUPDF=1)

# --- Precompiled header (Qt, std and MuPDF headers shared by most sources) ---
if (WITH_PCH)
    target_precompile_headers(${PROJECT_NAME} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/pch.hpp")
endif()

if (LIBARCHIVE_FOUND)
    target_link_libraries(${PROJECT_NAME} PRIVATE PkgConfig::LIBARCHIVE)
    target_compile_definitions(${PROJECT_NAME} PRIVATE HAVE_LIBARCHIVE=1)
endif()

if (FONTCONFIG_FOUND)
    target_link_libraries(${PROJECT_NAME} PRIVATE PkgConfig::FONTCONFIG)
    target_compile_definitions(${PROJECT_NAME} PRIVATE HAVE_FONTCONFIG=1)
endif()

if(MSVC)
    target_link_libraries(${PROJECT_NAME} PRIVATE
        "${MUPDF_SOURCE_DIR}/platform/win32/x64/Release/libmupdf.lib"
        dbghelp
    )
else()
    target_link_libraries(${PROJECT_NAME} PRIVATE
        "${MUPDF_INSTALL_DIR}/lib/libmupdf.a"
        "${MUPDF_INSTALL_DIR}/lib/libmupdf-third.a"
        z       # zlib
        m       # libm
        dl      # libdl
    )
endif()

# ----------------------------------------------------------------------------
# Synctex
# ----------------------------------------------------------------------------

if (WITH_SYNCTEX)
    # Define the source files
    set(SYNCTEX_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/synctex/synctex_parser.c
        ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/synctex/synctex_parser_utils.c
    )
    add_library(synctex STATIC ${SYNCTEX_SOURCES})

    target_include_directories(synctex
        PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/synctex)

    target_link_libraries(${PROJECT_NAME} PRIVATE synctex)
    target_compile_definitions(${PROJECT_NAME} PRIVATE WITH_SYNCTEX=1)
endif()

if (WITH_LUA)
    add_dependencies(${PROJECT_NAME} luajit_external)
    target_include_directories(${PROJECT_NAME} PRIVATE ${LUAJIT_INCLUDE_DIR})
    target_compile_definitions(${PROJECT_NAME} PRIVATE WITH_LUA=1)
    target_link_libraries(${PROJECT_NAME} PRIVATE ${LUAJIT_LIB_FILE})
    if (NOT MSVC)
        # LuaJIT needs libm and dlopen (for require() of C modules)
        target_link_libraries(${PROJECT_NAME} PRIVATE m ${CMAKE_DL_LIBS})
    endif()
    if (APPLE AND CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64")
        # 64-bit LuaJIT on x86_64 macOS needs the low 4 GB of address space.
        target_link_options(${PROJECT_NAME} PRIVATE
            -Wl,-pagezero_size,10000 -Wl,-image_base,100000000)
    endif()
endif()


if (WITH_LLM_SUPPORT)
    message(STATUS "LLM support: enabled")
    target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include/llm)
    target_sources(${PROJECT_NAME} PRIVATE
        src/llm/LLMView.cpp
        src/llm/HTTPClient.cpp
        src/llm/ChatBubble.cpp
        ${LEKTRA_LLM_HEADERS})
    target_compile_definitions(${PROJECT_NAME} PRIVATE WITH_LLM_SUPPORT=1)
endif()



# ---------------------------------------------------------------------------
# Per-config compile/link options
# NOTE: both use the same variable names so Debug -fsanitize=address is
# applied consistently to both compile and link steps.
# ---------------------------------------------------------------------------
target_compile_options(${PROJECT_NAME} PRIVATE
    $<$<CONFIG:Debug>:${DEBUG_FLAGS}>
    $<$<CONFIG:Release>:${RELEASE_FLAGS}>
    $<$<CONFIG:RelWithDebInfo>:${REL_WITH_DEB_INFO_FLAGS}>
)

# Portable -rdynamic: keeps Lektra's own function symbols in the dynamic
# symbol table so CrashHandler's backtrace_symbols_fd() can name our frames
# (Qt/libc frames already resolve without this via their own exported
# symbols; ours wouldn't without it).
set_target_properties(${PROJECT_NAME} PROPERTIES ENABLE_EXPORTS ON)

# ...but never export the symbols of the static libraries linked in (MuPDF and
# its bundled libjpeg, zlib, freetype, ...). Exported, they take precedence over
# the system libraries that Qt's plugins use, and mismatched versions crash
# (e.g. opening any JPEG: "Wrong JPEG library version").
if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_options(${PROJECT_NAME} PRIVATE -Wl,--exclude-libs,ALL)
endif()

if (MSVC)
    target_link_options(${PROJECT_NAME} PRIVATE
        $<$<CONFIG:Debug>:$<IF:$<CXX_COMPILER_ID:MSVC>,/DEBUG,--fsanitize=address>>
        # nothing needed for Release/RelWithDebInfo
    )
else()
    target_link_options(${PROJECT_NAME} PRIVATE
        $<$<CONFIG:Debug>:$<IF:$<CXX_COMPILER_ID:MSVC>,/DEBUG,-fsanitize=address>>
        # nothing needed for Release/RelWithDebInfo
    )
endif()


# ---------------------------------------------------------------------------
# Qt platform plugins
# ---------------------------------------------------------------------------
if(PLATFORM_LINUX)
    qt_import_plugins(${PROJECT_NAME}
        INCLUDE
        Qt6::QWaylandIntegrationPlugin
        Qt6::QXcbIntegrationPlugin
        Qt6::QEglFSIntegrationPlugin
    )
elseif(APPLE)
    qt_import_plugins(${PROJECT_NAME}
        INCLUDE Qt6::QCocoaIntegrationPlugin
    )
elseif(WIN32)
    qt_import_plugins(${PROJECT_NAME}
        INCLUDE Qt6::QWindowsIntegrationPlugin
    )
endif()

# ---------------------------------------------------------------------------
# Translations
# ---------------------------------------------------------------------------
set(TRANSLATION_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Translations")
file(GLOB TRANSLATION_FILES "${TRANSLATION_SRC_DIR}/*.ts")

set(TRANSLATIONS_DIR "${CMAKE_INSTALL_FULL_DATADIR}/locale")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/TranslationsDir.hpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/include/TranslationsDir.hpp"
    @ONLY
)

if(Qt6LinguistTools_VERSION VERSION_GREATER_EQUAL "6.7")
    qt_add_translations(
        TARGETS ${PROJECT_NAME}
        TS_FILES ${TRANSLATION_FILES}
        QM_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/translations"
        QM_FILES_OUTPUT_VARIABLE QM_FILES
    )
else()
    foreach(ts_file ${TRANSLATION_FILES})
        set_source_files_properties(${ts_file} PROPERTIES
            OUTPUT_LOCATION "${CMAKE_CURRENT_BINARY_DIR}/translations"
        )
    endforeach()
    qt_add_translations(${PROJECT_NAME}
        TS_FILES ${TRANSLATION_FILES}
        QM_FILES_OUTPUT_VARIABLE QM_FILES
    )
endif()

if(TARGET lektra_lrelease)
    add_dependencies(${PROJECT_NAME} lektra_lrelease)
endif()

# Helper macro: extracts the language code from a .qm filename.
# e.g. lektra.es.qm -> "es",  lektra.qm -> "" (base/untranslated, skip)
macro(get_qm_lang qm_file out_lang)
    get_filename_component(_qm_name "${qm_file}" NAME)
    string(REGEX REPLACE "\\.qm$"       "" _qm_name  "${_qm_name}")
    string(REPLACE       "lektra."      "" ${out_lang} "${_qm_name}")
    if(${out_lang} STREQUAL "${_qm_name}" OR ${out_lang} STREQUAL "")
        set(${out_lang} "")
    endif()
endmacro()

# macOS: copy .qm files into the bundle
if(APPLE)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/../Resources"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/tutorial.pdf"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/../Resources/tutorial.pdf"
        VERBATIM
    )
    foreach(qm_file ${QM_FILES})
        get_qm_lang("${qm_file}" lang)
        if(lang STREQUAL "")
            continue()
        endif()
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/../Resources/translations"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${qm_file}"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/../Resources/translations/"
            VERBATIM
        )
    endforeach()
endif()

# Install .qm files
foreach(qm_file ${QM_FILES})
    get_qm_lang("${qm_file}" lang)
    if(lang STREQUAL "")
        continue()
    endif()
    install(
        FILES "${qm_file}"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/lektra/translations"
    )
endforeach()

# ---------------------------------------------------------------------------
# Install rules
# ---------------------------------------------------------------------------
if(APPLE)
    install(TARGETS ${PROJECT_NAME} BUNDLE DESTINATION .)
else()
    install(TARGETS ${PROJECT_NAME} DESTINATION bin)
endif()

# .desktop entry (Linux / freedesktop)
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/lektra-template.desktop.in
    ${CMAKE_CURRENT_BINARY_DIR}/lektra.desktop
    @ONLY
)
if(PLATFORM_LINUX)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/lektra.desktop
        DESTINATION share/applications)
    install(DIRECTORY resources/hicolor
        DESTINATION share/icons
        FILES_MATCHING PATTERN "*.png")
endif()

# Docs
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/tutorial.pdf
    DESTINATION share/doc/${PROJECT_NAME})

# JSON Schema for config.toml (editor completion/validation through Taplo);
# generated by scripts/gen_config_schema.py
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/schema/lektra-config.schema.json
    DESTINATION share/lektra)

# Lua stubs (Linux and macOS only)
if(NOT WIN32)
    install(DIRECTORY stubs/lua/
        DESTINATION share/lektra/lua)
endif()

# Man pages (Unix only — not applicable on Windows)
if(NOT WIN32)
    install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/docs/man/lektra.1
        DESTINATION share/man/man1)
    # install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/docs/man/lektra-config.5  DESTINATION share/man/man5)
    # install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/docs/man/lektra-commands.7 DESTINATION share/man/man7)
endif()

if(WIN32)
    # Find windeployqt executable
    find_program(WINDEPLOYQT_EXECUTABLE windeployqt
        HINTS "${Qt${QT_VERSION_MAJOR}_DIR}/../../../bin"
    )

    if(WINDEPLOYQT_EXECUTABLE)
        message(STATUS "Found windeployqt: ${WINDEPLOYQT_EXECUTABLE}")

        # Run windeployqt after build (for development/debugging)
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${WINDEPLOYQT_EXECUTABLE}
            --release
            --no-translations
            --compiler-runtime
            $<TARGET_FILE:${PROJECT_NAME}>
            COMMENT "Running windeployqt for development build..."
        )
    else()
        message(WARNING "windeployqt not found - Qt dependencies won't be deployed automatically")
    endif()
endif()

# Install Qt dependencies during cmake --install
if(WIN32 AND WINDEPLOYQT_EXECUTABLE)
    install(CODE "
    message(STATUS \"Deploying Qt dependencies with windeployqt...\")
    execute_process(
        COMMAND \"${WINDEPLOYQT_EXECUTABLE}\"
        --release
        --no-translations
        --compiler-runtime
        \"\${CMAKE_INSTALL_PREFIX}/bin/${PROJECT_NAME}.exe\"
        RESULT_VARIABLE WINDEPLOYQT_RESULT
    )
    if(NOT WINDEPLOYQT_RESULT EQUAL 0)
        message(WARNING \"windeployqt failed with code \${WINDEPLOYQT_RESULT}\")
    endif()
    " COMPONENT Runtime)
endif()


if (NOT WIN32)
    find_package(ZLIB REQUIRED)
    target_link_libraries(${PROJECT_NAME} PRIVATE ZLIB::ZLIB)
    target_include_directories(${PROJECT_NAME} PRIVATE ${ZLIB_INCLUDE_DIRS})
endif()
