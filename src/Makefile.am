## Process this file with automake to produce Makefile.in

bin_PROGRAMS = queuefs

noinst_HEADERS = debug.h misc.h
queuefs_SOURCES = queuefs.c misc.c

AM_CFLAGS = $(fuse_CFLAGS) $(glib_CFLAGS)
queuefs_LDADD = $(fuse_LIBS) $(glib_LIBS)

man_MANS = queuefs.1
