AM_CPPFLAGS = -I$(srcdir)/../libsmrender -I$(srcdir)/../src
bin_PROGRAMS = smrenderd
smrenderd_SOURCES = smrenderd.c smhttp.c
smrenderd_LDADD = ../libsmrender/libsmrender.la ../src/smcore.o ../src/libhpxml.o ../src/smloadosm.o
noinst_HEADERS = smhttp.h

