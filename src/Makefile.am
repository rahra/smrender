#AM_LDFLAGS = -Wl,-export-dynamic $(GD_LDFLAGS) $(GD_LIBS)
AM_LDFLAGS = $(EXP_DYN) $(GD_LDFLAGS) $(GD_LIBS)
AM_CFLAGS = $(GD_CFLAGS)
bin_PROGRAMS = smrender
smrender_SOURCES = bstring.c smath.c smfunc.c smloadosm.c smrparse.c smutil.c bxtree.c libhpxml.c osm_func.c smcoast.c smgrid.c smlog.c smrender.c smrules.c smkap.c smqr.c smthread.c
noinst_HEADERS = bstring.h libhpxml.h osm_inplace.h bxtree.h smath.h smrender.h smrender_dev.h smcoast.h

