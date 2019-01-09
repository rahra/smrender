AC_PREREQ(2.67)
define([svnversion], esyscmd([sh -c "echo -n `git log --oneline | wc -l`"]))dnl
AC_INIT([smrender], [[4.1.g]svnversion], [bf@abenteuerland.at])
AC_CONFIG_MACRO_DIR([m4])
AM_INIT_AUTOMAKE([foreign -Wall -Werror])
AM_PROG_AR
LT_INIT
AC_SUBST([CFLAGS], [["$CFLAGS -Wall -Wextra"]])
#AC_SUBST([CFLAGS], [["-g -Wall"]])
AC_PROG_CC_C99
AC_PROG_LN_S
AC_PROG_MKDIR_P
AC_CONFIG_HEADERS([config.h])

smrender_datadir=${datadir}/smrender
AC_SUBST(smrender_datadir)

# some #defines for smrender
AC_DEFINE([MEM_USAGE], [], [Generate memory usage statistics.])
AC_DEFINE([bx_hash_t], [int64_t], [Set hash type for bxtree.])
AC_DEFINE([BX_RES], [4], [Set bit resolution for bxtree.])
AC_DEFINE([WITH_MMAP], [], [Add mmap() support.])

AC_ARG_ENABLE([threads], [AS_HELP_STRING([--enable-threads],[compile with pthreads support])],
   AC_DEFINE([WITH_THREADS], [], [enable multi-threading]))

AC_ARG_WITH([cairo], [AS_HELP_STRING([--without-cairo], [disable support for libcairo])], [], [with_cairo=yes])
AC_ARG_WITH([fontconfig], [AS_HELP_STRING([--without-fontconfig], [disable support for fontconfig])], [], [with_fontconfig=yes])
AC_ARG_WITH([libjpeg], [AS_HELP_STRING([--without-libjpeg], [disable support for libjpeg])], [], [with_libjpeg=yes])
AC_ARG_WITH([librsvg], [AS_HELP_STRING([--without-librsvg], [disable support for librsvg])], [], [with_librsvg=yes])

AS_IF([test "x$with_fontconfig" != "xno"], [
PKG_CHECK_MODULES([FONTCONFIG], [fontconfig],
                  [AC_DEFINE([HAVE_FONTCONFIG], [1], [compile with support for fontconfig.])],
                  []
                  )
])

AS_IF([test "x$with_libjpeg" != "xno"], [
PKG_CHECK_MODULES([LIBJPEG], [libjpeg],
                  [AC_DEFINE([HAVE_LIBJPEG], [1], [compile with support for libjpeg.])],
                  [AC_MSG_NOTICE([img() is compiled without JPEG support])]
                  )
])

AS_IF([test "x$with_cairo" != "xno"], [
PKG_CHECK_MODULES([CAIRO], [cairo],
                  [AC_DEFINE([HAVE_CAIRO], [1], [compile with support for libcairo.])],
                  []
                  )
# this should be done just for the src directory
#AC_SUBST([CAIRO_CFLAGS])
#AC_SUBST([CAIRO_LIBS])
AC_DEFINE([image_t], [cairo_surface_t], [default type of image_t])
])

AS_IF([test "x$with_cairo" = "xno"], [
AC_DEFINE([image_t], [void], [default type of image_t])
])

AS_IF([test "x$with_librsvg" != "xno"], [
PKG_CHECK_MODULES([RSVG], [librsvg-2.0],
                  [AC_DEFINE([HAVE_RSVG], [1], [compile with support for librsvg.])],
                  [AC_MSG_NOTICE([img() is compiled without SVG support])]
                  )
])

AS_IF([test "x$with_libcrypto" != "xno"], [
PKG_CHECK_MODULES([CRYPTO], [libcrypto],
                  [AC_DEFINE([HAVE_LIBCRYPTO], [1], [compile with support for libcrypto.])],
                  [AC_MSG_NOTICE([Websockets are compiled without libcrypto support])]
                  )
])

AX_PTHREAD([])
AS_IF([test "${enable_threads+set}" = "set"], [
   LIBS="$PTHREAD_LIBS $LIBS"
   CFLAGS="$CFLAGS $PTHREAD_CFLAGS"
   CC="$PTHREAD_CC"
])

AC_SEARCH_LIBS([sin], [m])
AC_SEARCH_LIBS([dlopen], [dl])
#AC_SEARCH_LIBS([RAND_bytes], [crypto])

AC_CHECK_FUNCS([strptime dladdr mbtowc execvpe])

PKG_CHECK_MODULES([GLIB], [glib-2.0],
                  [AC_DEFINE([HAVE_GLIB], [1], [using glib version check])],
                  []
                  )
AC_SUBST(GLIB_LIBS)
AC_SUBST(GLIB_CFLAGS)

AS_IF([test -n "$export_dynamic_flag_spec"], [
   exp_dyn=`eval echo $export_dynamic_flag_spec`
   AC_SUBST([EXP_DYN], [["$exp_dyn"]])
])

AC_SUBST([SMFILTER_NAME], [["libsmfilter$shrext_cmds"]])

AC_CONFIG_FILES([Makefile libsmrender/Makefile src/Makefile smrenderd/Makefile libsmfilter/Makefile libskel/Makefile tools/smfilter2])
AC_OUTPUT

