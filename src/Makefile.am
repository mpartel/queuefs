bin_PROGRAMS = queuefs

noinst_HEADERS = debug.h misc.h jobqueue.h jobqueue_process.h
queuefs_SOURCES = queuefs.c misc.c jobqueue.c jobqueue_process.c

AM_CFLAGS = $(fuse_CFLAGS) $(glib_CFLAGS)
queuefs_LDADD = $(fuse_LIBS) $(glib_LIBS)

man_MANS = queuefs.1
