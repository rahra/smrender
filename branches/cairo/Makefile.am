SUBDIRS = libsmrender src libsmfilter libskel
ACLOCAL_AMFLAGS = -I m4
rules_datadir = $(datadir)/smrender
dist_rules_data_DATA = rules/rules.osm rules/rulesbig.osm rules/rules_land.osm
available_rulesdir = $(rules_datadir)/available
dist_available_rules_DATA = rules/available/basemap.osm \
									 rules/available/landfeatures.osm \
									 rules/available/legend.osm \
									 rules/available/route.osm \
									 rules/available/seamark.osm \
									 rules/available/seamark_all.osm \
									 rules/available/seamark_buoy.osm

rules_100000dir = $(rules_datadir)/rules_100000

install-data-hook:
	if test -e $(DESTDIR)$(rules_100000dir) ; then \
		rm -r $(DESTDIR)$(rules_100000dir) ; \
		fi
	$(MKDIR_P) $(DESTDIR)$(rules_100000dir) && \
		cd $(DESTDIR)$(rules_100000dir) && \
		$(LN_S) ../available/basemap.osm 000basemap.osm && \
		$(LN_S) ../available/landfeatures.osm 010landfeatures.osm && \
		$(LN_S) ../available/route.osm 020route.osm && \
		$(LN_S) ../available/seamark_buoy.osm 029seamark.osm && \
		$(LN_S) ../available/seamark_all.osm 030seamark.osm && \
		$(LN_S) ../available/seamark.osm 031seamark.osm && \
		$(LN_S) ../available/legend.osm 900legend.osm

nodist_bin_SCRIPTS = tools/smfilter2
dist_noinst_SCRIPTS = tools/smfilter2.in

