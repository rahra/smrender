RULES = $(wildcard rules/*.osm)
DEST = images
SMRENDER = smrender
LOG = log

all:
	if test ! -d $(DEST) ; then mkdir $(DEST) ; fi
	for i in $(RULES) ; do \
		echo "making $$i" ; \
		$(SMRENDER) -D -P 50x50 -i testdata.osm -r $$i -G \
		-o $(DEST)/$$(basename $${i%%.osm}.png) 15E34.35:43N44.06:10000 2>> $(LOG) ; \
	done

clean:
	rm -rf $(DEST) $(LOG)

