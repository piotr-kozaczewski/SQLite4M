rem TCL -------------------------------------------
set TCLDIR=d:\tcl
set TCLINCDIR=d:\tcl\include
set TCLLIBDIR=d:\tcl\lib
rem ICU -------------------------------------------
set ICUDIR=d:\APT_LIBS_ROOT\msvc-14\icu\56.1
set ICUINCDIR=d:\APT_LIBS_ROOT\msvc-14\icu\56.1\include\
set ICULIBDIR=d:\APT_LIBS_ROOT\msvc-14\icu\56.1\lib64\
rem SQLite4 ---------------------------------------
set SQLITE4DIR=d:\user\piotr\imdb\SQLite\SQLite4\github\sqlite4-build\
rem WiredTiger ------------------------------------
set WTDIR=d:\wiredtiger\2015\
rem RPMalloc --------------------------------------
set RPMALLOCDIR=d:\user\piotr\imdb\SQLite\rpmalloc\
set RPMALLOCDEVELDIR=%RPMALLOCDIR%\rpmalloc-develop\
set RPMALLOCINCDIR=%RPMALLOCDEVELDIR%\rpmalloc\
set RPMALLOCLIBDIR=%RPMALLOCDEVELDIR%\lib\windows\
set RPMALLOCLIBDIR_DBG=%RPMALLOCLIBDIR%\debug\x86-64\
set RPMALLOCLIBDIR_REL=%RPMALLOCLIBDIR%\release\x86-64\ 
rem kvwt ------------------------------------------------
set KVWTDIR=d:\user\piotr\imdb\SQLite\SQLite4\github\kvwt-build\
set KVWTINCDIR=d:\user\piotr\imdb\SQLite\SQLite4\github\kvwt-build\kvwt\

"c:\Program Files (x86)\Microsoft Visual Studio 14.0\Common7\IDE\devenv.exe" d:\user\piotr\imdb\SQLite\SQLite4\github\kvwt-build\kvwt.sln
