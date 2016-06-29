AM_CPPFLAGS = -I$(srcdir)/../libsmrender -I$(srcdir)/../src
bin_PROGRAMS = smrenderd smwsclient
smrenderd_SOURCES = smrenderd.c smhttp.c smdb.c smcache.c websocket.c
smrenderd_LDADD = ../libsmrender/libsmrender.la ../src/smcore.o ../src/libhpxml.o ../src/smloadosm.o ../src/smosmout.o ../src/rdata.o ../src/smrparse.o -lcrypto
noinst_HEADERS = smhttp.h smcache.h websocket.h
smwsclient_SOURCES = smwsclient.c websocket.c
smwsclient_LDADD = ../libsmrender/libsmrender.la -lcrypto
