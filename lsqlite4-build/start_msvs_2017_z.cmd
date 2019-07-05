rem TCL -------------------------------------------
set TCLDIR=d:\tcl
set TCLINCDIR=d:\tcl\include
set TCLLIBDIR=d:\tcl\lib
rem ICU -------------------------------------------
set ICUDIR=d:\APT_LIBS_ROOT\msvc-14.1\icu\4.4.2
set ICUINCDIR=d:\APT_LIBS_ROOT\msvc-14.1\icu\4.4.2\include\
set ICULIBDIR=d:\APT_LIBS_ROOT\msvc-14.1\icu\4.4.2\lib64\
rem SQLite4 ---------------------------------------
set SQLITE4DIR=d:\user\piotr\imdb\SQLite\SQLite4\github\sqlite4-build
REM === Lua ===========================
REM  The lua install directory
set LUA_DIR=C:\Lua\5.3.5
REM Where to look for .dll files (C libraries)
set LUA_CPATH=?.dll;%LUA_DIR%\?.dll
REM Where to look for .lua files (Lua libraries)
set LUA_PATH=?.lua;%LUA_DIR%\lua\?.lua
set PATH=%PATH%;%LUA_DIR%

"C:\Program Files (x86)\Microsoft Visual Studio\2017\Professional\Common7\IDE\devenv.exe" d:\user\piotr\imdb\SQLite\SQLite4\github\lsqlite4-build\lsqlite4.sln
