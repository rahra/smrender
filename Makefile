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
CFLAGS	= -g -Wall
LDFLAGS	= -lm -lgd -ldl -Wl,-export-dynamic
VER = smrender-r$(shell svnversion | tr -d M)

all: smrender libsmfilter.so

smloadosm.o: smloadosm.c smrender.h

smrender: smrender.o bstring.o osm_func.o libhpxml.o smlog.o bxtree.o smloadosm.o smath.o smcoast.o smutil.o

smutil.o: smutil.c

smrender.o: smrender.c smlog.h bstring.h

osm_func.o: osm_func.c osm_inplace.h

bstring.o: bstring.c bstring.h

libhpxml.o: libhpxml.c libhpxml.h bstring.h

smlog.o: smlog.c smlog.h

bxtree.o: bxtree.c bxtree.h

smath.o: smath.c smath.h

smcoast.o: smcoast.c smrender.h smath.h

libsmfilter.so:
	make -C libsmfilter
	ln -s libsmfilter/libsmfilter.so

clean:
	make -C libsmfilter clean
	rm -f *.o smrender libsmfilter.so

dist: smrender
	if test -e $(VER) ; then \
		rm -r $(VER) ; \
	fi
	mkdir $(VER) $(VER)/man
	cp *.c *.h smrender Makefile testlight.osm $(VER)
	cp man/smrender.1 $(VER)/man
	tar cvfj $(VER).tbz2 $(VER)

.PHONY: clean dist

