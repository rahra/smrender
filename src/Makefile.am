AM_LDFLAGS = $(EXP_DYN) $(GD_LDFLAGS) $(GD_LIBS) $(CAIRO_LIBS) $(FONTCONFIG_LIBS) $(RSVG_LIBS) $(LIBJPEG_LIBS) $(GLIB_LIBS)
AM_CFLAGS = $(GD_CFLAGS) $(CAIRO_CFLAGS) $(RSVG_CFLAGS) $(LIBJPEG_CFLAGS) $(GLIB_CFLAGS)
AM_CPPFLAGS = -I$(srcdir)/../libsmrender
bin_PROGRAMS = smrender
smrender_SOURCES = smath.c smfunc.c smloadosm.c smrparse.c libhpxml.c smcoast.c smgrid.c smrender.c smkap.c smqr.c smthread.c smtile.c smrules_cairo.c rdata.c median_cut.c smexec.c smcore.c smosmout.c bspline_ctrl.c cairo_jpg.c
smrender_LDADD = ../libsmrender/libsmrender.la
noinst_HEADERS = libhpxml.h smath.h smrender_dev.h smcoast.h colors.c rdata.h smcore.h smloadosm.h bspline.h cairo_jpg.h

