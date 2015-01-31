SUBDIRS = libsmrender src smrenderd libsmfilter libskel
ACLOCAL_AMFLAGS = -I m4
dist_smrender_data_DATA = rules/rules.osm rules/rulesbig.osm rules/rules_land.osm
available_rulesdir = $(smrender_datadir)/available
dist_available_rules_DATA = rules/available/basemap_col.osm \
								  	 rules/available/lightsectors_bw.osm \
								  	 rules/available/seamark_buoy.osm \
									 rules/available/basemap.osm \
							 		 rules/available/lightsectors_col.osm \
									 rules/available/seamark_compass.osm \
									 rules/available/landfeatures.osm \
									 rules/available/route.osm \
					 				 rules/available/seamark_topmark.osm \
									 rules/available/legend.osm \
									 rules/available/seamark_beacon.osm \
								 	 rules/available/translation.osm

rules_100000dir = $(smrender_datadir)/rules_100000
r100k_yedir = $(smrender_datadir)/r100k_ye
r100k_coldir = $(smrender_datadir)/r100k_col

install-data-hook:
	if test -e $(DESTDIR)$(r100k_yedir) ; then \
		rm -r $(DESTDIR)$(r100k_yedir) ; \
		fi
	$(MKDIR_P) $(DESTDIR)$(r100k_yedir) && \
		cd $(DESTDIR)$(r100k_yedir) && \
		$(LN_S) ../available/basemap.osm 000basemap.osm && \
		$(LN_S) ../available/translation.osm 001translation.osm && \
		$(LN_S) ../available/landfeatures.osm 010landfeatures.osm && \
		$(LN_S) ../available/route.osm 020route.osm && \
		$(LN_S) ../available/lightsectors_bw.osm 028sectors.osm && \
		$(LN_S) ../available/seamark_buoy.osm 029buoy.osm && \
		$(LN_S) ../available/seamark_beacon.osm 030beacon.osm && \
		$(LN_S) ../available/seamark_topmark.osm 031topmark.osm && \
		$(LN_S) ../available/legend.osm 900legend.osm
	if test -e $(DESTDIR)$(rules_100000dir) ; then \
		rm -r $(DESTDIR)$(rules_100000dir) ; \
		fi
	$(LN_S) $(DESTDIR)$(r100k_yedir) $(DESTDIR)$(rules_100000dir)
	if test -e $(DESTDIR)$(r100k_coldir) ; then \
		rm -r $(DESTDIR)$(r100k_coldir) ; \
		fi
	$(MKDIR_P) $(DESTDIR)$(r100k_coldir) && \
		cd $(DESTDIR)$(r100k_coldir) && \
		$(LN_S) ../available/basemap_col.osm 000basemap.osm && \
		$(LN_S) ../available/translation.osm 001translation.osm && \
		$(LN_S) ../available/landfeatures.osm 010landfeatures.osm && \
		$(LN_S) ../available/route.osm 020route.osm && \
		$(LN_S) ../available/lightsectors_col.osm 028sectors.osm && \
		$(LN_S) ../available/seamark_buoy.osm 029buoy.osm && \
		$(LN_S) ../available/seamark_beacon.osm 030beacon.osm && \
		$(LN_S) ../available/seamark_topmark.osm 031topmark.osm && \
		$(LN_S) ../available/legend.osm 900legend.osm
	
nodist_bin_SCRIPTS = tools/smfilter2
dist_noinst_SCRIPTS = tools/smfilter2.in

