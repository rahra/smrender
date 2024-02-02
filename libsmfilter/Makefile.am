DEFS = @DEFS@ -DDATADIR=\"${smrender_datadir}\"
AM_CPPFLAGS = -I$(srcdir)/../libsmrender
nobase_lib_LTLIBRARIES = smrender/libsmfilter.la
smrender_libsmfilter_la_SOURCES = libsmfilter.c
smrender_libsmfilter_la_LDFLAGS = -no-undefined -version-info 0:1:0
noinst_HEADERS = seamark.h
smrender_libsmfilter_la_LIBADD = ../libsmrender/smrender/libsmrender.la

