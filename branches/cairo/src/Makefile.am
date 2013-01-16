#AM_LDFLAGS = -Wl,-export-dynamic $(GD_LDFLAGS) $(GD_LIBS)
AM_LDFLAGS = $(EXP_DYN) $(GD_LDFLAGS) $(GD_LIBS) $(CAIRO_LIBS)
#AM_LDFLAGS = $(GD_LDFLAGS) $(GD_LIBS)
AM_CFLAGS = $(GD_CFLAGS) $(CAIRO_CFLAGS)
AM_CPPFLAGS = -I$(srcdir)/../libsmrender
bin_PROGRAMS = smrender
smrender_SOURCES = smath.c smfunc.c smloadosm.c smrparse.c libhpxml.c smcoast.c smgrid.c smrender.c smrules.c smkap.c smqr.c smthread.c memimg.c smtile.c smrules_cairo.c rdata.c
smrender_LDADD = ../libsmrender/libsmrender.la
noinst_HEADERS = libhpxml.h smath.h smrender_dev.h smcoast.h memimg.h colors.c rdata.h

