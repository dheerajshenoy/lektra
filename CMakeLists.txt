cmake_minimum_required(VERSION 3.10)
project(lektra VERSION 0.7.0 LANGUAGES CXX)

option(ENABLE_LLM_SUPPORT "Enable LLM support for advanced features" OFF)

include(GNUInstallDirs)

if (NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type")
endif()

message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
add_compile_definitions(
    APP_BUILD_TYPE="${CMAKE_BUILD_TYPE}"
    APP_INSTALL_PREFIX="${CMAKE_INSTALL_PREFIX}"
    APP_VERSION="${PROJECT_VERSION}"
    APP_NAME="${PROJECT_NAME}"
)

message(STATUS "Installation prefix: ${CMAKE_INSTALL_PREFIX}")
set(PREFIX "${CMAKE_INSTALL_PREFIX}")
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_COLOR_DIAGNOSTICS ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

set(MUPDF_INSTALL_DIR "${CMAKE_BINARY_DIR}/mupdf-install")

# Get the version from the mupdf source if possible
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/mupdf/include/mupdf/fitz/version.h")
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/mupdf/include/mupdf/fitz/version.h" MUPDF_VERSION_CONTENTS)
    string(REGEX MATCH "#define FZ_VERSION \"([^\"]+)\"" _ ${MUPDF_VERSION_CONTENTS})
    if (CMAKE_MATCH_1)
        set(MUPDF_VERSION "${CMAKE_MATCH_1}")
    else()
        message(FATAL_ERROR "Could not find MuPDF version in source. Please ensure the mupdf submodule is initialized and try again.")
    endif()
    set(MUPDF_FOUND TRUE)
    set(MUPDF_INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/mupdf/include")
    set(MUPDF_LIBRARIES
        "${MUPDF_INSTALL_DIR}/lib/libmupdf.a"
        "${MUPDF_INSTALL_DIR}/lib/libmupdf-third.a"
    )
endif()

set(DEBUG_FLAGS -fno-omit-frame-pointer -ggdb3 -O0 -fsanitize=address -Wall -Wextra)
# set(DEBUG_FLAGS -fno-omit-frame-pointer -ggdb3 -O0 -Wall -Wextra)
set(RELEASE_FLAGS -O3 -DNDEBUG -fno-rtti)
set(REL_WITH_DEB_INFO -O2 -g -fno-omit-frame-pointer -DNDEBUG)

set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-O2 -g -fno-omit-frame-pointer -DNDEBUG")

include(ExternalProject)
ExternalProject_Add(mupdf_external
    SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/mupdf"
    CONFIGURE_COMMAND ""
    BUILD_COMMAND
    make -C <SOURCE_DIR>
    build=release
    shared=no
    HAVE_TESSERACT=no
    HAVE_CURL=no
    HAVE_X11=no
    HAVE_GLUT=no
    HAVE_LIBCRYPTO=no
    LIB_CFLAGS=-fPIC
    libs
    INSTALL_COMMAND
    make -C <SOURCE_DIR>
    build=release
    shared=no
    HAVE_TESSERACT=no
    HAVE_CURL=no
    HAVE_X11=no
    HAVE_GLUT=no
    HAVE_LIBCRYPTO=no
    prefix="${MUPDF_INSTALL_DIR}"
    install-libs
    BUILD_BYPRODUCTS
    "${MUPDF_INSTALL_DIR}/lib/libmupdf.a"
    "${MUPDF_INSTALL_DIR}/lib/libmupdf-third.a"
    UPDATE_COMMAND ""
)
add_library(mupdf_lib STATIC IMPORTED GLOBAL)
set_target_properties(mupdf_lib PROPERTIES
    IMPORTED_LOCATION "${MUPDF_INSTALL_DIR}/lib/libmupdf.a"
    INTERFACE_INCLUDE_DIRECTORIES "${MUPDF_INCLUDE_DIRS}"
)
add_dependencies(mupdf_lib mupdf_external)

find_package(Qt6 REQUIRED COMPONENTS Gui Widgets Core Concurrent LinguistTools OpenGLWidgets)
find_package(PkgConfig REQUIRED)

pkg_check_modules(DJVULIBRE ddjvuapi)
pkg_check_modules(SYNCTEX synctex)

if(NOT DJVULIBRE_FOUND)
    message(STATUS "DjVuLibre not found, disabling DjVu support")
endif()

if (NOT SYNCTEX_FOUND)
    message(STATUS "SyncTeX not found, disabling SyncTeX support.")
endif()


file(GLOB LEKTRA_CORE_HEADERS
    CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp
)

file(GLOB LEKTRA_ANNOTATION_HEADERS
    CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/include/Annotations/*.hpp
)

file(GLOB LEKTRA_COMMAND_HEADERS
    CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/include/Commands/*.hpp
)

add_executable(${PROJECT_NAME}
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
    include # Include project headers
    ${CMAKE_CURRENT_BINARY_DIR}/include # Include generated headers like TranslationsDir.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty # Include third-party headers (MuPDF)
)


if(APPLE)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "Lektra"
        MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
        MACOSX_BUNDLE_COPYRIGHT "Copyright 2026 Lektra contributors"
        MACOSX_BUNDLE_GUI_IDENTIFIER "org.lektra.lektra"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Info.plist.in"
        MACOSX_BUNDLE_LONG_VERSION_STRING "${PROJECT_NAME} ${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
    )
endif()

if (DJVULIBRE_FOUND)
    target_compile_definitions(${PROJECT_NAME} PUBLIC HAS_DJVU=1)
endif()

if (ENABLE_LLM_SUPPORT)
    message(STATUS "LLM support enabled.")
    target_compile_definitions(${PROJECT_NAME} PRIVATE ENABLE_LLM_SUPPORT=1)

    file(GLOB_RECURSE LEKTRA_LLM_HEADERS
        CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/include/LLM/*.hpp
    )

    file(GLOB_RECURSE LEKTRA_LLM_SOURCES
        CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/src/LLM/*.cpp
    )
    target_sources(${PROJECT_NAME} PRIVATE ${LEKTRA_LLM_SOURCES} ${LEKTRA_LLM_HEADERS}
    )

    find_package(CURL REQUIRED)
    target_link_libraries(${PROJECT_NAME} PRIVATE CURL::libcurl)

else()
    message(STATUS "LLM support disabled.")
endif()


qt_add_resources(RESOURCES resources.qrc)


target_sources(${PROJECT_NAME} PRIVATE ${RESOURCES})

# ------------------------------
# Link Libraries
# ------------------------------

if (MUPDF_FOUND)
    target_include_directories(${PROJECT_NAME} PRIVATE ${MUPDF_INCLUDE_DIRS})
    target_link_libraries(${PROJECT_NAME} PRIVATE
        mupdf_lib
        "${MUPDF_INSTALL_DIR}/lib/libmupdf-third.a"
        z      # zlib
        m      # libm (math, also needed by mupdf)
        dl     # libdl
    )
    target_compile_definitions(${PROJECT_NAME} PUBLIC HAS_MUPDF=1)
    add_dependencies(${PROJECT_NAME} mupdf_external)
endif()

if(SYNCTEX_FOUND)
    target_include_directories(${PROJECT_NAME} PRIVATE "${SYNCTEX_INCLUDE_DIR}")
    target_compile_definitions(${PROJECT_NAME} PUBLIC HAS_SYNCTEX=1)
    target_link_libraries(${PROJECT_NAME} PRIVATE synctex)
endif()

if (DJVULIBRE_FOUND)
    target_include_directories(${PROJECT_NAME} PRIVATE ${DJVU_INCLUDE_DIRS})
    target_link_libraries(${PROJECT_NAME} PRIVATE djvulibre)
    target_compile_definitions(${PROJECT_NAME} PUBLIC HAS_DJVU=1)
endif()

# ------------------------------
# Translations
# ------------------------------
set(TRANSLATION_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Translations")
file(GLOB TRANSLATION_FILES "${TRANSLATION_SRC_DIR}/*.ts")

# Configure translationsdir.hpp
set(TRANSLATIONS_DIR "${CMAKE_INSTALL_FULL_DATADIR}/locale")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/TranslationsDir.hpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/include/TranslationsDir.hpp"
    @ONLY
)

# Generate .qm files
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

# Helper macro to extract lang from qm filename, sets lang to "" for base file
macro(get_qm_lang qm_file out_lang)
    get_filename_component(_qm_name "${qm_file}" NAME)
    string(REGEX REPLACE "\\.qm$" "" _qm_name "${_qm_name}")
    string(REPLACE "lektra." "" ${out_lang} "${_qm_name}")
    if(${out_lang} STREQUAL "${_qm_name}" OR ${out_lang} STREQUAL "")
        set(${out_lang} "")
    endif()
endmacro()

# Apple Stuff
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

    if(ENABLE_LLM_SUPPORT)
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/src/llm/role.txt"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/../Resources/role.txt"
            VERBATIM
        )
    endif()

endif()

# Install each .qm into translations/
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


target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt6::Gui Qt6::Widgets Qt6::Core Qt6::Concurrent Qt6::OpenGLWidgets
)

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

# set_property(TARGET ${PROJECT_NAME} PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)

if(APPLE)
    install(TARGETS ${PROJECT_NAME} BUNDLE DESTINATION .)
else()
    install(TARGETS ${PROJECT_NAME} DESTINATION bin)
endif()
#Install icons to correct freedesktop - compliant locations

if(LINUX)
    qt_import_plugins(${CMAKE_PROJECT_NAME}
        INCLUDE
        Qt6::QWaylandIntegrationPlugin      # Wayland support (libqwayland.so)
        Qt6::QXcbIntegrationPlugin          # X11 support
        Qt6::QEglFSIntegrationPlugin        # Embedded GL support
    )
endif()


configure_file(${CMAKE_CURRENT_SOURCE_DIR}/lektra-template.desktop.in
    ${CMAKE_CURRENT_BINARY_DIR}/lektra.desktop @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/lektra.desktop DESTINATION share/applications)

install(DIRECTORY resources/hicolor DESTINATION share/icons FILES_MATCHING PATTERN "*.png")

# Install docs
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/tutorial.pdf
    DESTINATION share/doc/${PROJECT_NAME})

# Install man page
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/docs/man/lektra.1 DESTINATION share/man/man1)
# install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/docs/man/lektra-config.5 DESTINATION share/man/man5)
# install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/docs/man/lektra-commands.7 DESTINATION share/man/man7)

if(ENABLE_LLM_SUPPORT)
    install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/src/llm/role.txt DESTINATION share/${PROJECT_NAME}/)
endif()
