RULES0 = $(wildcard rules/0*.osm)
RULES1 = $(wildcard rules/1*.osm)
DEST = images
SMRENDER = smrender
LOG = log

all: rules0 rules1

$(DEST):
	if test ! -d $(DEST) ; then mkdir $(DEST) ; fi

rules0: $(DEST)
	for i in $(RULES0) ; do \
		echo "making $$i" ; \
		$(SMRENDER) -D -P 50x50 -i testdata.osm -r $$i -G \
		-o $(DEST)/$$(basename $${i%%.osm}.png) 15E34.35:43N44.06:10000 2>> $(LOG) ; \
	done

rules1: $(DEST)
	for i in $(RULES1) ; do \
		echo "making $$i" ; \
		$(SMRENDER) -D -P 50x50 -i testdata.osm -r $$i -G \
		-o $(DEST)/$$(basename $${i%%.osm}.png) 15E34.59:43N44.06:40000 2>> $(LOG) ; \
	done

clean:
	rm -rf $(DEST) $(LOG)

.PHONY: clean rules0 rules1

