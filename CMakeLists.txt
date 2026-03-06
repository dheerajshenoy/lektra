cmake_minimum_required(VERSION 3.10)
project(lektra VERSION 0.6.6 LANGUAGES CXX)

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

set(PREFIX "${CMAKE_INSTALL_PREFIX}")
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_COLOR_DIAGNOSTICS ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# set(DEBUG_FLAGS -fno-omit-frame-pointer -ggdb3 -O0 -fsanitize=address -Wall -Wextra)
set(DEBUG_FLAGS -fno-omit-frame-pointer -ggdb3 -O0 -Wall -Wextra)
set(RELEASE_FLAGS -O3 -DNDEBUG -march=native -flto -fno-rtti)
set(REL_WITH_DEB_INFO -O2 -g -fno-omit-frame-pointer -DNDEBUG)

# set(CMAKE_CXX_FLAGS_DEBUG "-fno-omit-frame-pointer -ggdb3 -O0 -Wall -Wextra -fsanitize=address -Werror")
# set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-O2 -g -fno-omit-frame-pointer -DNDEBUG")

find_package(Qt6 REQUIRED COMPONENTS Gui Widgets Core Concurrent LinguistTools OpenGLWidgets)
find_package(PkgConfig REQUIRED)

pkg_check_modules(MUPDF REQUIRED mupdf>=1.25.0)
pkg_check_modules(DJVULIBRE QUIET ddjvuapi)
pkg_check_modules(SYNCTEX QUIET synctex)

if (MUPDF_FOUND)
    message(STATUS "Found MuPDF: ${MUPDF_VERSION}. Compiling with MuPDF support.")
else()
    message(FATAL_ERROR "MuPDF not found. Please install MuPDF (>=1.25.0) development files to enable PDF support.")
endif()

if(DJVULIBRE_FOUND)
    message(STATUS "Found DjVuLibre: ${DJVULIBRE_VERSION}. Compiling with DJVU support.")
else()
    message(STATUS "DjVuLibre not found, disabling DjVu support")
endif()

if (SYNCTEX_FOUND)
    message(STATUS "Found SyncTeX: ${SYNCTEX_VERSION}. Compiling with SyncTeX support.")
else()
    message(STATUS "SyncTeX not found, disabling SyncTeX support.")
endif()

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
    src/Annotations/Annotation.cpp
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

    src/MouseBindKey.hpp
    src/Config.hpp
    src/MarkManager.hpp
    src/GraphicsPixmapItem.hpp
    src/BrowseLinkItem.hpp
    src/OutlineWidget.hpp
    src/OutlineModel.hpp
    src/ElidableLabel.hpp
    src/FloatingOverlayWidget.hpp
    src/HighlightSearchPicker.hpp
    src/EditLastPagesWidget.hpp
    src/RecentFilesStore.hpp
    src/RecentFilesModel.hpp
    src/LinkHint.hpp
    src/JumpMarker.hpp
    src/CircleLabel.hpp
    src/PlaceholderWidget.hpp
    src/ScrollBar.hpp
    src/CommandPicker.hpp
    src/JumpMarker.hpp
    src/OutlinePicker.hpp

    # Annotations
    src/Annotations/RectAnnotation.hpp
    src/Annotations/PopupAnnotation.hpp
    src/Annotations/HighlightAnnotation.hpp
    src/Annotations/CommentPopupButton.hpp

    # Commands
    src/Commands/RectAnnotationCommand.hpp
    src/Commands/TextAnnotationCommand.hpp
    src/Commands/TextHighlightAnnotationCommand.hpp
    src/Commands/DeleteAnnotationsCommand.hpp
    src/Commands/AnnotCommentCommand.hpp
)

if (DJVULIBRE_FOUND)
    target_compile_definitions(${PROJECT_NAME} PUBLIC HAS_DJVU=1)
endif()

if (ENABLE_LLM_SUPPORT)
    message(STATUS "LLM support enabled.")
    target_compile_definitions(${PROJECT_NAME} PRIVATE ENABLE_LLM_SUPPORT=1)

    target_sources(${PROJECT_NAME} PRIVATE
        src/LLM/http/HttpStreamClient.cpp
        src/LLM/LLMWidget.cpp
        src/LLM/providers/LLMProvider.cpp
        src/LLM/providers/ollama/OllamaProvider.cpp
    )

    find_package(CURL REQUIRED)
    target_link_libraries(${PROJECT_NAME} PRIVATE CURL::libcurl)

    target_include_directories(${PROJECT_NAME} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/llm
    )
else()
    message(STATUS "LLM support disabled.")
endif()


qt_add_resources(RESOURCES resources.qrc)


target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}/include # Include generated headers like TranslationsDir.hpp
)

target_sources(${PROJECT_NAME} PRIVATE ${RESOURCES})

# ------------------------------
# Link Libraries
# ------------------------------

if (MUPDF_FOUND)
    target_include_directories(${PROJECT_NAME} PRIVATE ${MUPDF_INCLUDE_DIRS})
    target_link_libraries(${PROJECT_NAME} PRIVATE ${MUPDF_LIBRARIES})
    target_compile_definitions(${PROJECT_NAME} PUBLIC HAS_MUPDF=1)
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
set(TRANSLATION_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/Translations")
file(GLOB TRANSLATION_FILES "${TRANSLATION_SRC_DIR}/*.ts")

# Configure translationsdir.hpp
set(TRANSLATIONS_DIR "${CMAKE_INSTALL_FULL_DATADIR}/locale")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/src/TranslationsDir.hpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/include/TranslationsDir.hpp"
    @ONLY
)

# Generate .qm files immediately without creating target dependencies
qt_add_translations(
    TARGETS ${PROJECT_NAME}         # Target that will use the .qm files
    TS_FILES ${TRANSLATION_FILES}   # Explicit .ts files
    QM_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/translations"
    QM_FILES_OUTPUT_VARIABLE QM_FILES
)

# Install each .qm into locale/<lang>/LC_MESSAGES/
foreach(qm_file ${QM_FILES})
    get_filename_component(qm_name "${qm_file}" NAME)
    string(REGEX REPLACE "\\.qm$" "" qm_name "${qm_name}")

    string(REPLACE "lektra." "" lang "${qm_name}")

    if(lang STREQUAL "${qm_name}")
        set(lang "en")
    endif()

    install(
        FILES "${qm_file}"
        DESTINATION "${TRANSLATIONS_DIR}/${lang}/LC_MESSAGES"
        RENAME "lektra.qm"
    )
endforeach()

# -----


target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt6::Gui Qt6::Widgets Qt6::Core Qt6::Concurrent Qt6::OpenGLWidgets
)

target_compile_options(${PROJECT_NAME} PRIVATE
    $<$<CONFIG:Debug>:${DEBUG_FLAGS}>
    $<$<CONFIG:Release>:${RELEASE_FLAGS}>
    $<$<CONFIG:RelWithDebInfo>:${REL_WITH_DEBINFO_FLAGS}>
)

target_link_options(${PROJECT_NAME} PRIVATE
    $<$<CONFIG:Debug>:${DEBUG_FLAGS}>
    $<$<CONFIG:Release>:${RELEASE_FLAGS}>
    $<$<CONFIG:RelWithDebInfo>:${REL_WITH_DEBINFO_FLAGS}>
)

set_property(TARGET ${PROJECT_NAME} PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)

install(TARGETS ${PROJECT_NAME} DESTINATION bin)
#Install icons to correct freedesktop - compliant locations

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/lektra-template.desktop.in
    ${CMAKE_CURRENT_BINARY_DIR}/lektra.desktop @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/lektra.desktop DESTINATION share/applications)

install(DIRECTORY resources/hicolor DESTINATION share/icons FILES_MATCHING PATTERN "*.png")

# Install docs
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/tutorial.pdf
    DESTINATION share/doc/${PROJECT_NAME})

if(ENABLE_LLM_SUPPORT)
    install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/src/llm/role.txt DESTINATION share/${PROJECT_NAME}/)
endif()
