#!/bin/sh
#
# This script demonstrates how to do a full-featured build of the sqlite4
# command-line shell on Linux.
#
# SQLite source code should be in a sibling directory named "sqlite".  For
# example, put SQLite sources in ~/sqlite/sqlite and run this script from
# ~/sqlite/bld.  There should be an appropriate Makefile in the current
# directory as well.
#
make sqlite4.c
gcc -o sqlite4 -g -Os -I. \
   -DSQLITE_THREADSAFE=0 \
   -DSQLITE_ENABLE_VFSTRACE \
   -DSQLITE_ENABLE_STAT2 \
   -DSQLITE_ENABLE_FTS3 \
   -DSQLITE_ENABLE_RTREE \
   -DHAVE_READLINE \
   -DHAVE_USLEEP=1 \
   ../sqlite/src/shell.c ../sqlite/src/test_vfstrace.c \
   sqlite4.c -ldl -lreadline -lncurses