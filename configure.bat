@echo off
setlocal enabledelayedexpansion

:: Default values
set "BUILD_TYPE=Release"
set "PREFIX=C:\usr"
set "WITH_IMAGE=on"
set "WITH_SYNCTEX=off"
set "WITH_LUA=off"

:parse_args

if "%~1"=="" goto end_parse

if /i "%~1"=="--build-type" (
    set "BUILD_TYPE=%~2"
    shift
    shift
    goto parse_args
)

if /i "%~1"=="--prefix" (
    set "PREFIX=%~2"
    shift
    shift
    goto parse_args
)

if /i "%~1"=="--with-image" (
    set "WITH_IMAGE=on"
    shift
    shift
    goto parse_args
)

if /i "%~1"=="--without-image" (
    set "WITH_IMAGE=off"
    shift
    shift
    goto parse_args
)

if /i "%~1"=="--with-synctex" (
    set "WITH_SYNCTEX=on"
    shift
    shift
    goto parse_args
)

if /i "%~1"=="--without-synctex" (
    set "WITH_SYNCTEX=off"
    shift
    shift
    goto parse_args
)

if /i "%~1"=="--with-lua" (
    set "WITH_LUA=on"
    shift
    shift
    goto parse_args
)

if /i "%~1"=="--without-lua" (
    set "WITH_LUA=off"
    shift
    shift
    goto parse_args
)

if /i "%~1"=="-h" goto print_help

if /i "%~1"=="--help" goto print_help

echo Unknown option: %1
echo Run 'configure.bat --help' for usage.
exit /b 1

:print_help
echo Usage: configure.bat [OPTIONS]
echo.
echo Options:
echo     --prefix PATH           Set the installation prefix [default: C:\usr]
echo     --with-image            Enable image files support (requires ImageMagick and it's C++ development library) (default: true)
echo     --without-image         Disable image files support
echo     --with-synctex          Enable SyncTeX support (requires SyncTeX library) (default: false)
echo     --without-synctex       Disable SyncTeX support
echo     --with-lua              Enable Lua scripting support (requires Lua library) (default: false)
echo     --without-lua           Disable Lua scripting support
echo     --build-type TYPE       Set the CMake build type [default: Release]
echo                             Valid values: Debug, Release, RelWithDebInfo
echo     -h, --help              Show this help message and exit
echo.
echo Examples:
echo     configure.bat
echo     configure.bat --prefix C:\local --build-type Release
exit /b 0

:end_parse

:: Check for mupdf directory (checks if a file or folder exists)
dir /a "thirdparty\mupdf" >nul 2>&1
if %errorlevel% neq 0 (
    echo Error: The directory 'thirdparty\mupdf' does not exist or is empty.
    git submodule update --init --recursive
)

:: Create build directory
if not exist build mkdir build

:: Note: For MSVC, CMAKE_BUILD_TYPE is ignored at this stage.
cmake -S . -B build -G "Visual Studio 18 2026" -A x64
    -DCMAKE_INSTALL_PREFIX="%PREFIX%"
    -DWITH_IMAGE="%WITH_IMAGE%"
    -DWITH_SYNCTEX="%WITH_SYNCTEX%"
    -DWITH_LUA="%WITH_LUA%"

if %errorlevel% neq 0 exit /b %errorlevel%

:: Build the project
:: Note: We specify --config here because VS is a multi-config generator.
cmake --build build --config %BUILD_TYPE%

if %errorlevel% neq 0 exit /b %errorlevel%

endlocal
