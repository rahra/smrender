#!/bin/sh

# run this script first if the source was checked out from SVN.

autoreconf -f
automake --add-missing --copy
autoreconf -f
libtoolize --copy

