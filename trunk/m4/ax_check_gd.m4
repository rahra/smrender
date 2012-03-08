AC_DEFUN([AX_CHECK_GD], [
   AC_ARG_WITH(gd,
      AC_HELP_STRING([--with-gd(=DIR)], [use the gd library (in DIR)]),,
      with_gd=yes)

   if test "$with_gd" != no; then
      AC_PATH_PROG(GDLIB_CONFIG, gdlib-config, , [$with_gd/bin:$PATH])
      if test -n "$GDLIB_CONFIG"; then
         GD_CFLAGS=`$GDLIB_CONFIG --cflags`
         GD_LDFLAGS=`$GDLIB_CONFIG --ldflags`
         GD_LIBS=`$GDLIB_CONFIG --libs`
      elif test -d "$with_gd"; then
         GD_CFLAGS="-I$with_gd/include"
         GD_LDFLAGS="-L$with_gd/lib"
         AC_CHECK_LIB(z, inflateReset, GD_LIBS="-lz")
         AC_CHECK_LIB(png, png_check_sig, GD_LIBS="-lpng $GD_LIBS", , $GD_LIBS)
      fi

      save_CFLAGS="$CFLAGS"
      CFLAGS="$GD_CFLAGS $CFLAGS"
      save_LDFLAGS="$LDFLAGS"
      LDFLAGS="$GD_LDFLAGS $LDFLAGS"

      AC_CHECK_LIB(gd, gdImageCreate, [
         AC_DEFINE(HAVE_GD, 1, [ Define if you have gd library. ])
         AC_CHECK_LIB(gd, gdImageGif, AC_DEFINE(HAVE_GD_GIF, 1, [ Define if GD supports gif. ]), , "$GD_LIBS")
         AC_CHECK_LIB(gd, gdImageJpeg, AC_DEFINE(HAVE_GD_JPEG, 1, [ Define if GD supports jpeg. ]), , "$GD_LIBS")
         AC_CHECK_LIB(gd, gdImagePng, AC_DEFINE(HAVE_GD_PNG, 1, [ Define if GD supports png. ]), , "$GD_LIBS")
         GD_LIBS="-lgd $GD_LIBS"
      ], with_gd=no, $GD_LIBS)

      CFLAGS="$save_CFLAGS"
      LDFLAGS="$save_LDFLAGS"
   fi

   if test "$with_gd" = "no"; then
      GD_CFLAGS="";
      GD_LDFLAGS="";
      GD_LIBS="";
   fi

   AC_SUBST(GD_CFLAGS)
   AC_SUBST(GD_LDFLAGS)
   AC_SUBST(GD_LIBS)
])

