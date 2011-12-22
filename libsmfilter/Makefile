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
CFLAGS = -g -fPIC -I..
LDFLAGS = -lm

all: libsmfilter.so

libsmfilter.o: libsmfilter.c

libsmfilter.so.1: libsmfilter.o
	gcc -g -shared -Wl,-soname,libsmfilter.so.1 -o libsmfilter.so.1.0.1 libsmfilter.o -lc -lm

libsmfilter.so: libsmfilter.so.1
	if test ! -e libsmfilter.so ; then \
		ln -s libsmfilter.so.1.0.1 libsmfilter.so ; \
	fi

clean:
	rm -f *.o *.so *.so.1

.PHONY: clean

