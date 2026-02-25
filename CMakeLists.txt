cmake_minimum_required(VERSION 3.10)
project(lektra VERSION 0.6.5 LANGUAGES CXX)

option(ENABLE_LLM_SUPPORT "Enable LLM support for advanced features" OFF)

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
set(CMAKE_CXX_FLAGS_DEBUG "-fsanitize=address -fno-omit-frame-pointer -ggdb3 -O0 -Wall -Wextra")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-O2 -g -fno-omit-frame-pointer -DNDEBUG")

find_package(Qt6 REQUIRED COMPONENTS Gui Widgets Core Concurrent) #OpenGLWidgets )
find_package(PkgConfig REQUIRED)

# SyncTeX
find_path(SYNCTEX_INCLUDE_DIR
    NAMES synctex_parser.h synctex/synctex_parser.h
    PATH_SUFFIXES synctex
)
find_library(SYNCTEX_LIBRARY
    NAMES synctex synctex_parser
)

add_executable(${PROJECT_NAME}
    src/main.cpp
    src/lektra.cpp
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
    src/Annotations/TextAnnotation.hpp
    src/Annotations/HighlightAnnotation.hpp

    # Commands
    src/Commands/RectAnnotationCommand.hpp
    src/Commands/TextAnnotationCommand.hpp
    src/Commands/TextHighlightAnnotationCommand.hpp
    src/Commands/DeleteAnnotationsCommand.hpp
)


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

# Add MuPDF headers
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/external/mupdf/include
)

target_sources(${PROJECT_NAME} PRIVATE ${RESOURCES})

if(NOT SYNCTEX_INCLUDE_DIR OR NOT SYNCTEX_LIBRARY)
    message(STATUS "SyncTeX not found.")
else()
    message(STATUS "Found SyncTex: ${synctex_VERSION}. Compiling with SyncTeX support.")
    target_include_directories(${PROJECT_NAME} PRIVATE "${SYNCTEX_INCLUDE_DIR}")
    target_compile_definitions(${PROJECT_NAME} PUBLIC HAS_SYNCTEX=1)
    target_link_libraries(${PROJECT_NAME} PRIVATE synctex)
endif()



target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt6::Gui Qt6::Widgets Qt6::Core Qt6::Concurrent
    ${CMAKE_SOURCE_DIR}/external/mupdf/build/release/libmupdf.a
    ${CMAKE_SOURCE_DIR}/external/mupdf/build/release/libmupdf-third.a
)


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
