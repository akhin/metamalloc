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



REM Set the console color to yellow
color 0E

REM ----------------------------------------------------------------------------------------------------------
REM MAIN DLL , /DENABLE_TRACER for traces
set "DLL_TU_NAME=metamalloc_simple_heap_pow2"
del %DLL_TU_NAME%.dll
del %DLL_TU_NAME%.lib

cl /c /EHsc /I"../../" /I"../../examples/" /DDLL_EXPORTS /MD  /std:c++17 /O2 %DLL_TU_NAME%.cpp
link /DEFAULTLIB:Advapi32.lib /DLL /OUT:%DLL_TU_NAME%.dll %DLL_TU_NAME%.obj
del %DLL_TU_NAME%.exp
del %DLL_TU_NAME%.obj
REM ----------------------------------------------------------------------------------------------------------
REM DEBUG DLL
del %DLL_TU_NAME%_debug.exp
del %DLL_TU_NAME%_debug.ilk
del %DLL_TU_NAME%_debug.dll
del %DLL_TU_NAME%_debug.lib

cl /c /EHsc /I"../../" /I"../../examples/" /DDLL_EXPORTS /MD /std:c++17 /Od /Zi /DEBUG %DLL_TU_NAME%.cpp
link /DEFAULTLIB:Advapi32.lib /DEBUG /PDB:"%DLL_TU_NAME%_debug.pdb" /DLL /OUT:"%DLL_TU_NAME%_debug.dll" %DLL_TU_NAME%.obj

del %DLL_TU_NAME%_debug.exp
del %DLL_TU_NAME%_debug.ilk
del %DLL_TU_NAME%.obj
REM ----------------------------------------------------------------------------------------------------------
REM SAMPLE APP
set "EXECUTABLE_TU_NAME=sample_app"
del %EXECUTABLE_TU_NAME%.exe
cl.exe /EHsc /I"../../" /I"../../examples" /std:c++17 /O2 %EXECUTABLE_TU_NAME%.cpp /Fe:%EXECUTABLE_TU_NAME%.exe /link /subsystem:console /DEFAULTLIB:Advapi32.lib

del %EXECUTABLE_TU_NAME%.obj
del vc140.pdb

REM Pause the script so you can see the build output
pause