@echo off
setlocal enabledelayedexpansion

:: Default values
set "BUILD_TYPE=Release"
set "PREFIX=C:\usr"

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
cmake -S . -B build -G "Visual Studio 18 2026" -A x64 -DCMAKE_INSTALL_PREFIX="%PREFIX%"

if %errorlevel% neq 0 exit /b %errorlevel%

:: Build the project
:: Note: We specify --config here because VS is a multi-config generator.
cmake --build build --config %BUILD_TYPE%

if %errorlevel% neq 0 exit /b %errorlevel%

endlocal
