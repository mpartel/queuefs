AC_INIT([queuefs],[1.9],[martin.partel@gmail.com])

AM_INIT_AUTOMAKE
AM_CONFIG_HEADER(config.h)

AC_PROG_CC_C99
AC_LANG(C)
AC_PROG_LIBTOOL

# --enable and --with options
AC_ARG_ENABLE([debug],
    [AS_HELP_STRING([--enable-debug], [enable extra debug output])])


if test x"$enable_debug" == "xyes" ; then
    CFLAGS="${CFLAGS} -g -O0 -DMALLOC_CHECK_=2"
    AC_DEFINE([QUEUEFS_DEBUG], [1], [Define to 1 to enable debugging messages])
else
    CFLAGS="${CFLAGS} -O2"
fi

CFLAGS="${CFLAGS} -Wall -D_REENTRANT -D_FILE_OFFSET_BITS=64 -DFUSE_USE_VERSION=26"

# Check for xattrs
AC_CHECK_FUNCS([setxattr getxattr listxattr removexattr])
AC_CHECK_FUNCS([lsetxattr lgetxattr llistxattr lremovexattr])

# Check for dependencies
PKG_CHECK_MODULES([fuse], [fuse >= 2.8.0])
PKG_CHECK_MODULES([glib], [glib-2.0 >= 2.26.0])

AC_CONFIG_FILES([Makefile \
    src/Makefile \
    tests/Makefile])
AC_OUTPUT

