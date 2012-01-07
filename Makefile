#/* Copyright 2011 Bernhard R. Fischer, 2048R/5C5FFD47 <bf@abenteuerland.at>
# *
# * This file is part of smrender.
# *
# * Smrender is free software: you can redistribute it and/or modify
# * it under the terms of the GNU General Public License as published by
# * the Free Software Foundation, version 3 of the License.
# *
# * Smrender is distributed in the hope that it will be useful,
# * but WITHOUT ANY WARRANTY; without even the implied warranty of
# * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# * GNU General Public License for more details.
# *
# * You should have received a copy of the GNU General Public License
# * along with smrender. If not, see <http://www.gnu.org/licenses/>.
# */

CC	= gcc
CFLAGS	= -O2 -g -Wall -D_GNU_SOURCE -I/usr/local/include
#LDFLAGS	= -L/usr/local/lib -lm -lgd -ldl -Wl,-export-dynamic
# FreeBSD provides dl-functions in libc
LDFLAGS	= -L/usr/local/lib -lm -lgd -Wl,-export-dynamic
VER = smrender-r$(shell svnversion | tr -d M)

all: smrender libsmfilter.so

smloadosm.o: smloadosm.c smrender.h

smrender: smrender.o bstring.o osm_func.o libhpxml.o smlog.o bxtree.o smloadosm.o smath.o smcoast.o smutil.o smgrid.o smrules.o smrparse.o
	gcc $(LDFLAGS) -o smrender smrender.o bstring.o osm_func.o libhpxml.o smlog.o bxtree.o smloadosm.o smath.o smcoast.o smutil.o smgrid.o smrules.o smrparse.o

smgrid.o: smgrid.c

smutil.o: smutil.c

smrender.o: smrender.c smlog.h bstring.h

osm_func.o: osm_func.c osm_inplace.h

bstring.o: bstring.c bstring.h

libhpxml.o: libhpxml.c libhpxml.h bstring.h

smlog.o: smlog.c smlog.h

bxtree.o: bxtree.c bxtree.h

smath.o: smath.c smath.h

smcoast.o: smcoast.c smrender.h smath.h

smrules.o: smrules.c smrender.h

smrparse.o: smrparse.c smrparse.h

libsmfilter.so:
	make -C libsmfilter
	ln -s libsmfilter/libsmfilter.so

clean:
	make -C libsmfilter clean
	rm -f *.o smrender libsmfilter.so

dist: smrender libsmfilter.so
	if test -e $(VER) ; then \
		rm -r $(VER) ; \
	fi
	mkdir $(VER) $(VER)/libsmfilter
	cp *.c *.h smrender Makefile $(VER)
	cp libsmfilter/libsmfilter.so* libsmfilter/*.c libsmfilter/*.h libsmfilter/Makefile $(VER)/libsmfilter
	tar cvfj $(VER).tbz2 $(VER)

.PHONY: clean dist

