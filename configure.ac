AC_INIT([smrender], [1.1], [bf@abenteuerland.at])
AC_CONFIG_MACRO_DIR([m4])
AM_INIT_AUTOMAKE([foreign -Wall -Werror])
LT_INIT
AC_PROG_CC
AC_CONFIG_HEADERS([config.h])

AX_CHECK_GD

#AC_SEARCH_LIBS([gdImageSetPixel], [gd])
AC_SEARCH_LIBS([sin], [m])
AC_SEARCH_LIBS([dlopen], [dl])

AC_CHECK_FUNCS([strptime])

if test -n "$export_dynamic_flag_spec" ; then
   exp_dyn=`eval echo $export_dynamic_flag_spec`
   AC_SUBST([EXP_DYN], [["$exp_dyn"]])
fi

AC_CONFIG_FILES([Makefile src/Makefile libsmfilter/Makefile libskel/Makefile rules/Makefile])
AC_OUTPUT

