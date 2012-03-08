AC_INIT([smrender], [1.1], [bf@abenteuerland.at])
AC_CONFIG_MACRO_DIR([m4])
AM_INIT_AUTOMAKE([foreign -Wall -Werror])
LT_INIT
AC_PROG_CC
AC_CONFIG_HEADERS([config.h])

AC_SEARCH_LIBS([sin], [m])
AC_SEARCH_LIBS([gdImageSetPixel], [gd])
AC_SEARCH_LIBS([dlopen], [dl])

AC_CONFIG_FILES([Makefile src/Makefile libsmfilter/Makefile libskel/Makefile])
AC_OUTPUT

