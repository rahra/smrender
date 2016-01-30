DEFS = @DEFS@ -DDATADIR=\"${smrender_datadir}\"
AM_CPPFLAGS = -I$(srcdir)/../libsmrender
lib_LTLIBRARIES = libsmfilter.la
libsmfilter_la_SOURCES = libsmfilter.c
libsmfilter_la_LDFLAGS = -no-undefined
noinst_HEADERS = seamark.h
libsmfilter_la_LIBADD = ../libsmrender/libsmrender.la

