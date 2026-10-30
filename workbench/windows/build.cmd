@echo off
setlocal
cd /d "%~dp0"
if not defined VSCMD_VER (
 for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "ORBIT_VS=%%i"
)
if not defined VSCMD_VER call "%ORBIT_VS%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
if /I "%~1"=="--app-only" goto build_app
if not exist vendor\scintilla\bin mkdir vendor\scintilla\bin
if not exist vendor\lexilla\bin mkdir vendor\lexilla\bin
if not exist vendor\scintilla\win32\obj mkdir vendor\scintilla\win32\obj
if not exist vendor\lexilla\src\obj mkdir vendor\lexilla\src\obj
pushd vendor\scintilla\win32
nmake /nologo -f scintilla.mak QUIET=1 ..\bin\libscintilla.lib
if errorlevel 1 exit /b 1
popd
pushd vendor\lexilla\src
nmake /nologo -f lexilla.mak QUIET=1 ..\bin\liblexilla.lib
if errorlevel 1 exit /b 1
popd
:build_app
if not exist vendor\scintilla\bin\libscintilla.lib exit /b 1
if not exist vendor\lexilla\bin\liblexilla.lib exit /b 1
if not exist build mkdir build
if not exist app mkdir app
pushd src
rc /nologo /fo..\build\orbit.res orbit.rc
if errorlevel 1 exit /b 1
popd
cl /nologo /MP /D_SILENCE_CXX20_U8PATH_DEPRECATION_WARNING /std:c++20 /utf-8 /EHsc /O2 /GL /MT /DNDEBUG /DUNICODE /D_UNICODE /W4 /wd4100 /wd4244 /wd4267 src\main.cpp src\paint.cpp src\view.cpp src\workspace.cpp src\interaction.cpp src\runtime.cpp src\integration.cpp src\navigation\history.cpp src\navigation\workbench_navigation.cpp src\editor\outline.cpp src\editor\commands.cpp src\ui\quick_pick.cpp src\ui\icons.cpp src\tests\icon_tests.cpp src\tests\workbench_tests.cpp src\runtime\output_buffer.cpp src\runtime\console_view.cpp src\workspace\project_tree.cpp src\tests\performance_tests.cpp /Fo"build\\" /Fe"app\OrbitNative.exe" /link /LTCG /OPT:REF /OPT:ICF /SUBSYSTEM:WINDOWS /DYNAMICBASE /NXCOMPAT /CETCOMPAT /MANIFEST:NO build\orbit.res vendor\scintilla\bin\libscintilla.lib vendor\lexilla\bin\liblexilla.lib user32.lib gdi32.lib
if errorlevel 1 exit /b 1
xcopy assets app\assets\ /E /I /Y >nul
xcopy sample-project app\sample-project\ /E /I /Y >nul
copy /Y vendor\scintilla\License.txt app\Scintilla-LICENSE.txt >nul
copy /Y vendor\lexilla\License.txt app\Lexilla-LICENSE.txt >nul
copy /Y vendor\json-LICENSE.txt app\json-LICENSE.txt >nul
echo Built app\OrbitNative.exe
endlocal
