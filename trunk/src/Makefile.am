#AM_LDFLAGS = -Wl,-export-dynamic $(GD_LDFLAGS) $(GD_LIBS)
AM_LDFLAGS = $(EXP_DYN) $(GD_LDFLAGS) $(GD_LIBS)
AM_CFLAGS = $(GD_CFLAGS)
bin_PROGRAMS = smrender
smrender_SOURCES = bstring.c MurmurHash2_64.c smath.c smfunc.c smloadosm.c smreg.c smrparse.c smutil.c bxtree.c libhpxml.c osm_func.c smcoast.c smgrid.c smlog.c smrender.c smrules.c
noinst_HEADERS = bstring.h  libhpxml.h osm_inplace.h smconfig.h smreg.h smrparse.h bxtree.h MurmurHash2_64.h smath.h smlog.h smrender.h smrules.h

