AC_PREREQ(2.67)
define([svnversion], esyscmd([sh -c "echo -n $(( `svnversion | tr -d 'M'` + 1 ))"]))dnl
AC_INIT([smrender], [[1.2.r]svnversion], [bf@abenteuerland.at])
AC_CONFIG_MACRO_DIR([m4])
AM_INIT_AUTOMAKE([foreign -Wall -Werror])
LT_INIT
AC_SUBST([CFLAGS], [["$CFLAGS -Wall"]])
#AC_SUBST([CFLAGS], [["-g -Wall"]])
AC_PROG_CC
AC_CONFIG_HEADERS([config.h])


# some #defines for smrender
AC_DEFINE([MEM_USAGE], [], [Generate memory usage statistics.])
AC_DEFINE([bx_hash_t], [int64_t], [Set hash type for bxtree.])
AC_DEFINE([BX_RES], [4], [Set bit resolution for bxtree.])
AC_DEFINE([WITH_MMAP], [], [Add mmap() support.])
AC_DEFINE([READ_FILTER], [], [Add code for read filter.])


AX_CHECK_GD

#AC_SEARCH_LIBS([gdImageSetPixel], [gd])
AC_SEARCH_LIBS([sin], [m])
AC_SEARCH_LIBS([dlopen], [dl])

AC_CHECK_FUNCS([strptime])

if test -n "$export_dynamic_flag_spec" ; then
   exp_dyn=`eval echo $export_dynamic_flag_spec`
   AC_SUBST([EXP_DYN], [["$exp_dyn"]])
fi

AC_SUBST([SMFILTER_NAME], [["libsmfilter$shrext_cmds"]])

AC_CONFIG_FILES([Makefile src/Makefile libsmfilter/Makefile libskel/Makefile tools/smfilter2])
AC_OUTPUT

