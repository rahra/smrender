# Smrender

A rule-based renderer for OSM data.

## Description

Smrender is a powerful, flexible, and modular rule-based data processing and rendering engine for
OSM data. It is mainly intended to create paper charts for print-out but it can
be used for a lot of other tasks as well. Because of its very generic
and modular software design it is perfectly suitable for complex OSM data
processing and manipulation tasks such as filtering, modification, and
statistical analysis.

You can find some charts here: [prerendered
charts](https://www.cypherpunk.at/download/smrender/charts/).  The most
accurate charts are the yellow map style charts of Croatia (updated on 29th of
April 2015), which are based on the official sheet lines: [Croation sea
charts](https://www.cypherpunk.at/download/smrender/charts/croatia/yellow_map/PDF/).
You can find some other examples here: [chart
samples](https://www.cypherpunk.at/download/smrender/samples/).

## Download

The latest snapshots of version 4.1 of Smrender are found in the
[current/](https://www.cypherpunk.at/download/smrender/current/) directory.

With February 2016, the primary project page of Smrender was moved to
[Github](https://github.com/rahra/smrender). The old page is found at
[Abenteueland](http://www.abenteuerland.at/smrender/) which I will keep up for
a while.

Older versions of Smrender have been moved to the [archive/](https://www.cypherpunk.at/download/smrender/archive/) directory.

## Documentation

The latest documentation for version 4.0 is found (here as HTML page)[http://www.abenteuerland.at/smrender/descr.html] or (here as PDF document)[http://www.abenteuerland.at/smrender/descr.pdf].

## Compile and Install


To compile and install from the tarball:
```Shell
./configure
make
make install
```

If it was checked out from SVN or GIT: run `/autoconf.sh` first (you have to
have the *GNU Autotools* installed in this case.).

See documentation for more information.

## See Also

* Postings on Smrender at the [Bernhard's blog](https://www.cypherpunk.at/tag/smrender/)

## Bugs

Smrender seems not to work in non-memory-mapped mode. As a workaround, simply just always use the option -M.

## Author

Smrender is developed and maintained by Bernhard R. Fischer, 4096R/8E24F29D <bf@abenteuerland.at>.
You may also [follow me on Twitter](http://twitter.com/_Rahra_).

## License

Smrender is released under GNU GPLv3.


