@echo off
setlocal EnableExtensions EnableDelayedExpansion
set "PATH=C:\msys64\mingw64\bin;%PATH%"
set "sharedSources="
for /R "%~dp0..\src\Shered" %%F in (*.c) do set "sharedSources=!sharedSources! "%%F""
gcc.exe "%~dp0..\src\Play\rpg_main.c" !sharedSources! -Wall -Wextra -Wpedantic -I src\Shered -I src\Shered\Rpg -I src\Shered\Explorer -I src\Play -I C:\msys64\mingw64\include -L C:\msys64\mingw64\lib -mwindows -Wl,--stack,16777216 -o build\rpg_version.exe -lraylib -lopengl32 -lgdi32 -lwinmm -lshell32 -ldwmapi > build\rpg_version_build.log 2>&1
if errorlevel 1 exit /b 1
set "editorSources="
for %%F in (src\Editor\file_dialog.c src\Editor\rpg_editor_drag.c src\Editor\rpg_editor_main.c src\Editor\rpg_editor_navigation.c src\Editor\rpg_editor_play.c src\Editor\rpg_editor_text.c) do set "editorSources=!editorSources! "%%F""
gcc.exe !editorSources! !sharedSources! -Wall -Wextra -Wpedantic -I src\Shered -I src\Shered\Rpg -I src\Shered\Explorer -I src\Editor -I C:\msys64\mingw64\include -L C:\msys64\mingw64\lib -mwindows -Wl,--stack,16777216 -o build\rpg_editor.exe -lraylib -lopengl32 -lgdi32 -lwinmm -lshell32 -ldwmapi -lcomdlg32 -limm32 -lshlwapi > build\rpg_editor_build.log 2>&1
exit /b %ERRORLEVEL%
