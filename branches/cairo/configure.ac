AC_PREREQ(2.67)
define([svnversion], esyscmd([sh -c "echo -n `svnversion | tr -d 'M'`"]))dnl
AC_INIT([smrender], [[3.0.r]svnversion], [bf@abenteuerland.at])
AC_CONFIG_MACRO_DIR([m4])
AM_INIT_AUTOMAKE([foreign -Wall -Werror])
LT_INIT
AC_SUBST([CFLAGS], [["$CFLAGS -Wall -Wextra"]])
#AC_SUBST([CFLAGS], [["-g -Wall"]])
AC_PROG_CC
AC_PROG_LN_S
AC_PROG_MKDIR_P
AC_CONFIG_HEADERS([config.h])


# some #defines for smrender
AC_DEFINE([MEM_USAGE], [], [Generate memory usage statistics.])
AC_DEFINE([bx_hash_t], [int64_t], [Set hash type for bxtree.])
AC_DEFINE([BX_RES], [4], [Set bit resolution for bxtree.])
AC_DEFINE([WITH_MMAP], [], [Add mmap() support.])
AC_DEFINE([READ_FILTER], [], [Add code for read filter.])

AC_ARG_ENABLE([threads], [AS_HELP_STRING([--enable-threads],[compile with pthreads support])],
   AC_DEFINE([WITH_THREADS], [], [enable multi-threading]))

AC_ARG_WITH([cairo], [AS_HELP_STRING([--without-cairo], [disable support for libcairo])], [], [with_cairo=yes])

AS_IF([test "x$with_cairo" != xno], [
PKG_CHECK_MODULES([CAIRO], [cairo],
                  AC_DEFINE([HAVE_CAIRO], [], [compile with support for libcairo.])
                  )
# this should be done just for the src directory
#AC_SUBST([CAIRO_CFLAGS])
#AC_SUBST([CAIRO_LIBS])
AC_DEFINE([image_t], [cairo_surface_t], [default type of image_t])
])

AS_IF([test "x$with_cairo" = xno], [
AC_DEFINE([image_t], [void], [default type of image_t])
])

AS_IF([test "${enable_threads+set}" = set], [
AX_PTHREAD([
   LIBS="$PTHREAD_LIBS $LIBS"
   CFLAGS="$CFLAGS $PTHREAD_CFLAGS"
   CC="$PTHREAD_CC"
            ])
])

AC_SEARCH_LIBS([sin], [m])
AC_SEARCH_LIBS([dlopen], [dl])

AC_CHECK_FUNCS([strptime dladdr])

AS_IF([test -n "$export_dynamic_flag_spec"], [
   exp_dyn=`eval echo $export_dynamic_flag_spec`
   AC_SUBST([EXP_DYN], [["$exp_dyn"]])
])

AC_SUBST([SMFILTER_NAME], [["libsmfilter$shrext_cmds"]])

AC_CONFIG_FILES([Makefile libsmrender/Makefile src/Makefile libsmfilter/Makefile libskel/Makefile tools/smfilter2])
AC_OUTPUT

