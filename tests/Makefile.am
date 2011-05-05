
check_PROGRAMS = jobqueuetest

jobqueuetest_SOURCES = jobqueuetest.c

AM_CFLAGS = $(fuse_CFLAGS) $(glib_CFLAGS) -I../src
jobqueuetest_LDADD = $(fuse_LIBS) $(glib_LIBS)

TESTS = test_queuefs.rb jobqueuetest
