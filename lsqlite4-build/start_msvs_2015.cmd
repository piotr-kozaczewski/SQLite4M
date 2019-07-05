rem TCL -------------------------------------------
set TCLDIR=c:\tcl
set TCLINCDIR=c:\tcl\include
set TCLLIBDIR=c:\tcl\lib
rem ICU -------------------------------------------
set ICUDIR=d:\EXT_LIBS_ROOT\msvc-14\icu\56.1
set ICUINCDIR=d:\EXT_LIBS_ROOT\msvc-14\icu\56.1\include\
set ICULIBDIR=d:\EXT_LIBS_ROOT\msvc-14\icu\56.1\lib64\
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

devenv.lnk d:\user\piotr\imdb\SQLite\SQLite4\github\lsqlite4-build\lsqlite4.sln
