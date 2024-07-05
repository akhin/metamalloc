@echo off

REM Change vars accordingly to your MSVC installation
set "VS_PATH=C:\Program Files\Microsoft Visual Studio"
set "VS_VERSION=2022"
set "VS_EDITION=Community"

if not exist "%VS_PATH%\%VS_VERSION%\%VS_EDITION%\VC\Auxiliary\Build\vcvarsall.bat" (
    echo Can't find VS%VS_VERSION% command prompt in %VS_PATH%.
    echo Please check your VS installation and update the script accordingly.
    pause
    exit /b 1
)

call "%VS_PATH%\%VS_VERSION%\%VS_EDITION%\VC\Auxiliary\Build\vcvarsall.bat" x64

set "TRANSLATION_UNIT_NAME=unit_test_scalable_allocator"

REM Set the console color to yellow
color 0E

REM Build the C++ file using MSVC, no O3 in MSVC
cl.exe /EHsc /I"../../" /std:c++17 /D NDEBUG /O2 %TRANSLATION_UNIT_NAME%.cpp /Fe:%TRANSLATION_UNIT_NAME%.exe /link /subsystem:console /DEFAULTLIB:Advapi32.lib


REM Delete the object file generated during compilation
del %TRANSLATION_UNIT_NAME%.obj

REM Pause the script so you can see the build output
pause