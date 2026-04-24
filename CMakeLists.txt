cmake_minimum_required(VERSION 3.10)
project(lektra VERSION 0.7.0 LANGUAGES CXX)

include(GNUInstallDirs)

option(WITH_IMAGE "Enable image support" ON)

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
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_COLOR_DIAGNOSTICS ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# ---------------------------------------------------------------------------
# Per-config compile/link flags
# NOTE: define the variable name that the generator expressions actually use.
# ---------------------------------------------------------------------------
if(MSVC)
    set(DEBUG_FLAGS          /Od /Zi /RTC1 /W4 /fsanitize=address)
    set(RELEASE_FLAGS        /O2 /DNDEBUG /GR- /W4)
    set(REL_WITH_DEB_INFO_FLAGS /O2 /Zi /DNDEBUG /W4)
    # MSVC standard-conforming preprocessor (needed for some C++23 headers)
    add_compile_options(/Zc:__cplusplus /Zc:preprocessor)
else()
    set(DEBUG_FLAGS          -fno-omit-frame-pointer -ggdb3 -O0 -fsanitize=address -Wall -Wextra)
    # set(DEBUG_FLAGS        -fno-omit-frame-pointer -ggdb3 -O0 -Wall -Wextra)
    set(RELEASE_FLAGS        -O3 -DNDEBUG -fno-rtti)
    set(REL_WITH_DEB_INFO_FLAGS -O2 -g -fno-omit-frame-pointer -DNDEBUG)
endif()

# Override CMake's built-in RelWithDebInfo flags so they match ours exactly.
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "" CACHE STRING "" FORCE)

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
)

# ---------------------------------------------------------------------------
# MuPDF (ExternalProject)
# ---------------------------------------------------------------------------
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

include(ExternalProject)

if(MSVC)
    set(MUPDF_SLN "${MUPDF_SOURCE_DIR}/platform/win32/mupdf.sln")
    set(MUPDF_LIB_FILE "${MUPDF_INSTALL_DIR}/lib/libmupdf.lib")

    ExternalProject_Add(mupdf_external
        SOURCE_DIR "${MUPDF_SOURCE_DIR}"
        CONFIGURE_COMMAND ""
        BUILD_COMMAND
        MSBuild "${MUPDF_SLN}"
        /p:Configuration=Release
        /p:Platform=x64
        /t:libmupdf
        /m
        INSTALL_COMMAND
        ${CMAKE_COMMAND} -E make_directory "${MUPDF_INSTALL_DIR}/lib"
        COMMAND ${CMAKE_COMMAND} -E copy
        "${MUPDF_SOURCE_DIR}/platform/win32/x64/Release/libmupdf.lib"
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
        make -C <SOURCE_DIR> build=release shared=yes
        HAVE_TESSERACT=no HAVE_CURL=no HAVE_X11=no HAVE_GLUT=no HAVE_LIBCRYPTO=no USE_SYSTEM_LIBS=yes
        LIB_CFLAGS="-fPIC -fvisibility=hidden" libs
        INSTALL_COMMAND
        make -C <SOURCE_DIR> build=release shared=yes
        HAVE_TESSERACT=no HAVE_CURL=no HAVE_X11=no HAVE_GLUT=no HAVE_LIBCRYPTO=no USE_SYSTEM_LIBS=yes
        prefix="${MUPDF_INSTALL_DIR}" install-libs
        BUILD_BYPRODUCTS "${MUPDF_LIB_FILE}" "${MUPDF_LIB_THIRD_FILE}"
        UPDATE_COMMAND ""
    )
endif()

add_library(mupdf_lib SHARED IMPORTED)
set_target_properties(mupdf_lib PROPERTIES
    IMPORTED_LOCATION             "${MUPDF_LIB_FILE}"
    INTERFACE_INCLUDE_DIRECTORIES "${MUPDF_INCLUDE_DIRS}"
)

add_library(mupdf_lib_third SHARED IMPORTED)
set_target_properties(mupdf_lib_third PROPERTIES
    IMPORTED_LOCATION             "${MUPDF_LIB_THIRD_FILE}"
    INTERFACE_INCLUDE_DIRECTORIES "${MUPDF_INCLUDE_DIRS}"
)
add_dependencies(mupdf_lib_third mupdf_external)

# ---------------------------------------------------------------------------
# Qt6 + optional system libraries
# ---------------------------------------------------------------------------
find_package(Qt6 REQUIRED COMPONENTS
    Core Gui Widgets Concurrent LinguistTools OpenGLWidgets Network)

find_package(PkgConfig REQUIRED)

pkg_check_modules(DJVULIBRE QUIET ddjvuapi)
pkg_check_modules(SYNCTEX QUIET synctex)

if(NOT DJVULIBRE_FOUND)
    message(STATUS "DjVuLibre not found – DjVu support disabled")
endif()
if(NOT SYNCTEX_FOUND)
    message(STATUS "SyncTeX not found – SyncTeX support disabled")
endif()

# ---------------------------------------------------------------------------
# Source files
# ---------------------------------------------------------------------------
file(GLOB LEKTRA_CORE_HEADERS       CONFIGURE_DEPENDS include/*.hpp)
file(GLOB LEKTRA_ANNOTATION_HEADERS CONFIGURE_DEPENDS include/Annotations/*.hpp)
file(GLOB LEKTRA_COMMAND_HEADERS    CONFIGURE_DEPENDS include/Commands/*.hpp)

set(app_icon_resource_windows "")
if(WIN32)
    set(app_icon_resource_windows "${CMAKE_CURRENT_SOURCE_DIR}/resources/lektra.rc")
endif()

add_executable(${PROJECT_NAME}
    ${app_icon_resource_windows}
    src/main.cpp
    src/Lektra.cpp
    src/Statusbar.cpp
    src/Model.cpp
    src/PropertiesWidget.cpp
    src/AboutDialog.cpp
    src/GraphicsView.cpp
    src/GraphicsScene.cpp
    src/DocumentView.cpp
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
    src/TabBar.cpp
    src/Picker.cpp
    src/RecentFilesPicker.cpp
    src/OutlinePicker.cpp
    src/CommentSearchPicker.cpp
    src/ScrollBar.cpp
    src/ColorDialog.cpp

    ${LEKTRA_CORE_HEADERS}
    ${LEKTRA_ANNOTATION_HEADERS}
    ${LEKTRA_COMMAND_HEADERS}
)

target_include_directories(${PROJECT_NAME} PRIVATE
    include
    ${CMAKE_CURRENT_BINARY_DIR}/include   # generated headers (TranslationsDir.hpp etc.)
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty
)

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
    set_target_properties(${PROJECT_NAME} PROPERTIES
        MACOSX_BUNDLE                    TRUE
        MACOSX_BUNDLE_BUNDLE_NAME        "Lektra"
        MACOSX_BUNDLE_BUNDLE_VERSION     ${PROJECT_VERSION}
        MACOSX_BUNDLE_COPYRIGHT          "Copyright 2026 Lektra contributors"
        MACOSX_BUNDLE_GUI_IDENTIFIER     "org.lektra.lektra"
        MACOSX_BUNDLE_INFO_PLIST         "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Info.plist.in"
        MACOSX_BUNDLE_LONG_VERSION_STRING "${PROJECT_NAME} ${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
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

if(WITH_IMAGE)
    PKG_CHECK_MODULES(MAGICK Magick++ REQUIRED)
    if(MAGICK_FOUND)
        target_include_directories(lektra PRIVATE ${MAGICK_INCLUDE_DIRS})
        target_link_libraries(lektra PRIVATE ${MAGICK_LIBRARIES})
        target_compile_options(lektra PRIVATE ${MAGICK_CFLAGS_OTHER})
        target_compile_definitions(${PROJECT_NAME} PUBLIC WITH_IMAGE=1)
    endif()
endif()

# --- MuPDF ---
target_include_directories(${PROJECT_NAME} PRIVATE ${MUPDF_INCLUDE_DIRS})
target_compile_definitions(${PROJECT_NAME} PUBLIC HAS_MUPDF=1)
add_dependencies(${PROJECT_NAME} mupdf_external)

# --- Qt ---
target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets
    Qt6::Concurrent Qt6::OpenGLWidgets Qt6::Network
)

if(MSVC)
    target_link_libraries(${PROJECT_NAME} PRIVATE mupdf_lib mupdf_lib_third)
else()
    target_link_libraries(${PROJECT_NAME} PRIVATE
        mupdf_lib
        mupdf_lib_third
        z       # zlib
        m       # libm
        dl      # libdl
    )
endif()

# --- SyncTeX ---
if(SYNCTEX_FOUND)
    target_include_directories(${PROJECT_NAME} PRIVATE "${SYNCTEX_INCLUDE_DIRS}")
    target_compile_definitions(${PROJECT_NAME} PUBLIC HAS_SYNCTEX=1)
    target_link_libraries(${PROJECT_NAME} PRIVATE synctex)
endif()

# --- DjVuLibre ---
if(DJVULIBRE_FOUND)
    target_include_directories(${PROJECT_NAME} PRIVATE ${DJVULIBRE_INCLUDE_DIRS})
    target_compile_definitions(${PROJECT_NAME} PUBLIC HAS_DJVU=1)
    target_link_libraries(${PROJECT_NAME} PRIVATE ${DJVULIBRE_LIBRARIES})
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

target_link_options(${PROJECT_NAME} PRIVATE
    $<$<CONFIG:Debug>:${DEBUG_FLAGS}>
    $<$<CONFIG:Release>:${RELEASE_FLAGS}>
    $<$<CONFIG:RelWithDebInfo>:${REL_WITH_DEB_INFO_FLAGS}>
)

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

# Man pages (Unix only — not applicable on Windows)
if(NOT WIN32)
    install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/docs/man/lektra.1
        DESTINATION share/man/man1)
    # install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/docs/man/lektra-config.5  DESTINATION share/man/man5)
    # install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/docs/man/lektra-commands.7 DESTINATION share/man/man7)
endif()
