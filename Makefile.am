SUBDIRS = libsmrender src smrenderd libsmfilter libskel
ACLOCAL_AMFLAGS = -I m4
dist_smrender_data_DATA = rules/rules.osm rules/rulesbig.osm rules/rules_land.osm
available_rulesdir = $(smrender_datadir)/available
dist_available_rules_DATA = \
	rules/available/basemap_col.osm \
	rules/available/basemap.osm \
	rules/available/landfeatures.osm \
	rules/available/legend.osm \
	rules/available/lightsectors_bw.osm \
	rules/available/lightsectors_col.osm \
	rules/available/route.osm \
	rules/available/seamark_area.osm \
	rules/available/seamark_beacon.osm \
	rules/available/seamark_buoy.osm \
	rules/available/seamark_compass.osm \
	rules/available/seamark_rocks.osm \
	rules/available/seamark_topmark.osm \
	rules/available/soundings.osm \
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
		$(LN_S) ../available/lightsectors_col.osm 028sectors.osm && \
		$(LN_S) ../available/seamark_area.osm 027area.osm && \
		$(LN_S) ../available/seamark_rocks.osm 026rocks.osm && \
		$(LN_S) ../available/seamark_buoy.osm 029buoy.osm && \
		$(LN_S) ../available/seamark_beacon.osm 030beacon.osm && \
		$(LN_S) ../available/seamark_topmark.osm 031topmark.osm && \
		$(LN_S) ../available/soundings.osm 032soundings.osm && \
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
		$(LN_S) ../available/seamark_area.osm 027area.osm && \
		$(LN_S) ../available/seamark_rocks.osm 026rocks.osm && \
		$(LN_S) ../available/seamark_buoy.osm 029buoy.osm && \
		$(LN_S) ../available/seamark_beacon.osm 030beacon.osm && \
		$(LN_S) ../available/seamark_topmark.osm 031topmark.osm && \
		$(LN_S) ../available/soundings.osm 032soundings.osm && \
		$(LN_S) ../available/legend.osm 900legend.osm
	
nodist_bin_SCRIPTS = tools/smfilter2
dist_noinst_SCRIPTS = tools/smfilter2.in

seamapsymbolsdir = $(smrender_datadir)/SeaMapSymbols
dist_seamapsymbols_DATA = \
SeaMapSymbols/AnchorageER.svg \
SeaMapSymbols/Anchorage.svg \
SeaMapSymbols/Anchor_Berth.svg \
SeaMapSymbols/Barrel.svg \
SeaMapSymbols/Barrel_Yellow.svg \
SeaMapSymbols/Beacon_Black_Red_Black.svg \
SeaMapSymbols/Beacon_Black_Yellow_Black.svg \
SeaMapSymbols/Beacon_Black_Yellow.svg \
SeaMapSymbols/Beacon_Green_Red_Green.svg \
SeaMapSymbols/Beacon_Green.svg \
SeaMapSymbols/Beacon_Green_White_Green_White.svg \
SeaMapSymbols/Beacon_Red_Green_Red_Green.svg \
SeaMapSymbols/Beacon_Red_Green_Red.svg \
SeaMapSymbols/Beacon_Red.svg \
SeaMapSymbols/Beacon_Red_White_Red_White.svg \
SeaMapSymbols/Beacon_Red_White.svg \
SeaMapSymbols/Beacon.svg \
SeaMapSymbols/Beacon_Yellow_Black.svg \
SeaMapSymbols/Beacon_Yellow_Black_Yellow.svg \
SeaMapSymbols/Beacon_Yellow.svg \
SeaMapSymbols/Board_Bottom.svg \
SeaMapSymbols/Board_Top.svg \
SeaMapSymbols/Boatyard.svg \
SeaMapSymbols/Bollard.svg \
SeaMapSymbols/Cable.svg \
SeaMapSymbols/Cairn.svg \
SeaMapSymbols/Can_Black_Red_Black.svg \
SeaMapSymbols/Can_Black_Yellow_Black.svg \
SeaMapSymbols/Can_Black_Yellow.svg \
SeaMapSymbols/Can_Green_Red_Green.svg \
SeaMapSymbols/Can_Green.svg \
SeaMapSymbols/Can_Red_Green_Red_Green.svg \
SeaMapSymbols/Can_Red_Green_Red.svg \
SeaMapSymbols/Can_Red.svg \
SeaMapSymbols/Can_Red_White_Red_White.svg \
SeaMapSymbols/Can.svg \
SeaMapSymbols/Can_Yellow_Black.svg \
SeaMapSymbols/Can_Yellow_Black_Yellow.svg \
SeaMapSymbols/Can_Yellow.svg \
SeaMapSymbols/Chandler.svg \
SeaMapSymbols/Chimney.svg \
SeaMapSymbols/ChurchA.svg \
SeaMapSymbols/ChurchDome.svg \
SeaMapSymbols/ChurchSpire.svg \
SeaMapSymbols/Church.svg \
SeaMapSymbols/ChurchTower.svg \
SeaMapSymbols/Clear_H.svg \
SeaMapSymbols/Clear_V.svg \
SeaMapSymbols/Cone_Black_Red_Black.svg \
SeaMapSymbols/Cone_Black_Yellow_Black.svg \
SeaMapSymbols/Cone_Black_Yellow.svg \
SeaMapSymbols/Cone_Green_Red_Green.svg \
SeaMapSymbols/Cone_Green.svg \
SeaMapSymbols/Cone_Green_White_Green_White.svg \
SeaMapSymbols/Cone_Red_Green_Red_Green.svg \
SeaMapSymbols/Cone_Red_Green_Red.svg \
SeaMapSymbols/Cone_Red.svg \
SeaMapSymbols/Cone.svg \
SeaMapSymbols/Cone_Yellow_Black.svg \
SeaMapSymbols/Cone_Yellow_Black_Yellow.svg \
SeaMapSymbols/Cone_Yellow.svg \
SeaMapSymbols/ContainerCrane.svg \
SeaMapSymbols/Crane.svg \
SeaMapSymbols/Cross.svg \
SeaMapSymbols/Customs.svg \
SeaMapSymbols/DeviationDolphin.svg \
SeaMapSymbols/DishAerial.svg \
SeaMapSymbols/DistanceI.svg \
SeaMapSymbols/DistanceU.svg \
SeaMapSymbols/Dolphin.svg \
SeaMapSymbols/Dome.svg \
SeaMapSymbols/Dot.svg \
SeaMapSymbols/Fishing_Harbour.svg \
SeaMapSymbols/Flagstaff.svg \
SeaMapSymbols/FlareStack.svg \
SeaMapSymbols/Float_Black_Red_Black.svg \
SeaMapSymbols/Float_Black_Yellow_Black.svg \
SeaMapSymbols/Float_Black_Yellow.svg \
SeaMapSymbols/Float_Green_Red_Green.svg \
SeaMapSymbols/Float_Green.svg \
SeaMapSymbols/Float_Green_White_Green_White.svg \
SeaMapSymbols/Float_Red_Green_Red_Green.svg \
SeaMapSymbols/Float_Red_Green_Red.svg \
SeaMapSymbols/Float_Red.svg \
SeaMapSymbols/Float_Red_White_Red_White.svg \
SeaMapSymbols/Float_Red_White.svg \
SeaMapSymbols/Float.svg \
SeaMapSymbols/Float_Yellow_Black.svg \
SeaMapSymbols/Float_Yellow_Black_Yellow.svg \
SeaMapSymbols/Float_Yellow.svg \
SeaMapSymbols/Fog_Signal.svg \
SeaMapSymbols/Fuel.svg \
SeaMapSymbols/harbour_master.svg \
SeaMapSymbols/HarbourMaster.svg \
SeaMapSymbols/Harbour.svg \
SeaMapSymbols/Hospital.svg \
SeaMapSymbols/Ice.svg \
SeaMapSymbols/International_tidyman_magenta.svg \
SeaMapSymbols/LandingSteps.svg \
SeaMapSymbols/LandTower.svg \
SeaMapSymbols/LaneArrow.svg \
SeaMapSymbols/Laundrette.svg \
SeaMapSymbols/Light_Green_120.svg \
SeaMapSymbols/Light_Green_240.svg \
SeaMapSymbols/Light_House.svg \
SeaMapSymbols/Light_Magenta_120.svg \
SeaMapSymbols/Light_Major.svg \
SeaMapSymbols/Light_Minor.svg \
SeaMapSymbols/Light_Red_120.svg \
SeaMapSymbols/Light_Red_240.svg \
SeaMapSymbols/Light.svg \
SeaMapSymbols/Light_White_120.svg \
SeaMapSymbols/Light_White_240.svg \
SeaMapSymbols/LineAnchor.svg \
SeaMapSymbols/LinePlane.svg \
SeaMapSymbols/LLight.svg \
SeaMapSymbols/Lock_Gate_Single.svg \
SeaMapSymbols/Lock_Gate.svg \
SeaMapSymbols/Lock_Single.svg \
SeaMapSymbols/Lock.svg \
SeaMapSymbols/LoLcaption.svg \
SeaMapSymbols/MarinaNF.svg \
SeaMapSymbols/Marina.svg \
SeaMapSymbols/MarineFarm.svg \
SeaMapSymbols/Mast.svg \
SeaMapSymbols/Minaret.svg \
SeaMapSymbols/Monastary.svg \
SeaMapSymbols/Monument.svg \
SeaMapSymbols/Notice_A10a.svg \
SeaMapSymbols/Notice_A10b.svg \
SeaMapSymbols/Notice_A12.svg \
SeaMapSymbols/Notice_A13.svg \
SeaMapSymbols/Notice_A14.svg \
SeaMapSymbols/Notice_A15.svg \
SeaMapSymbols/Notice_A16.svg \
SeaMapSymbols/Notice_A17.svg \
SeaMapSymbols/Notice_A18.svg \
SeaMapSymbols/Notice_A19.svg \
SeaMapSymbols/Notice_A1a.svg \
SeaMapSymbols/Notice_A1.svg \
SeaMapSymbols/Notice_A20.svg \
SeaMapSymbols/Notice_A2.svg \
SeaMapSymbols/Notice_A3.svg \
SeaMapSymbols/Notice_A4_1.svg \
SeaMapSymbols/Notice_A4.svg \
SeaMapSymbols/Notice_A5_1.svg \
SeaMapSymbols/Notice_A5.svg \
SeaMapSymbols/Notice_A6.svg \
SeaMapSymbols/Notice_A7.svg \
SeaMapSymbols/Notice_A8.svg \
SeaMapSymbols/Notice_A9.svg \
SeaMapSymbols/Notice_Anchor.svg \
SeaMapSymbols/Notice_A.svg \
SeaMapSymbols/Notice_B11.svg \
SeaMapSymbols/Notice_B1a.svg \
SeaMapSymbols/Notice_B1b.svg \
SeaMapSymbols/Notice_B2a.svg \
SeaMapSymbols/Notice_B2b.svg \
SeaMapSymbols/Notice_B3a.svg \
SeaMapSymbols/Notice_B3b.svg \
SeaMapSymbols/Notice_B4a.svg \
SeaMapSymbols/Notice_B4b.svg \
SeaMapSymbols/Notice_B5.svg \
SeaMapSymbols/Notice_B6.svg \
SeaMapSymbols/Notice_B7.svg \
SeaMapSymbols/Notice_B8.svg \
SeaMapSymbols/Notice_B9a.svg \
SeaMapSymbols/Notice_B9b.svg \
SeaMapSymbols/Notice_Bbm.svg \
SeaMapSymbols/Notice_Bcp.svg \
SeaMapSymbols/Notice_Bcs.svg \
SeaMapSymbols/Notice_Blb.svg \
SeaMapSymbols/Notice_Blmhr.svg \
SeaMapSymbols/Notice_Bmr.svg \
SeaMapSymbols/Notice_Bnank.svg \
SeaMapSymbols/Notice_Bollard.svg \
SeaMapSymbols/Notice_Bol.svg \
SeaMapSymbols/Notice_Bor.svg \
SeaMapSymbols/Notice_Bpm.svg \
SeaMapSymbols/Notice_Brb.svg \
SeaMapSymbols/Notice_Brs.svg \
SeaMapSymbols/Notice_Bsi.svg \
SeaMapSymbols/Notice_Bsm.svg \
SeaMapSymbols/Notice_B.svg \
SeaMapSymbols/Notice_Bwlar.svg \
SeaMapSymbols/Notice_Bwral.svg \
SeaMapSymbols/Notice_C1.svg \
SeaMapSymbols/Notice_C2.svg \
SeaMapSymbols/Notice_C3.svg \
SeaMapSymbols/Notice_C4.svg \
SeaMapSymbols/Notice_C5a.svg \
SeaMapSymbols/Notice_C5b.svg \
SeaMapSymbols/Notice_Crossing_L.svg \
SeaMapSymbols/Notice_Crossing_R.svg \
SeaMapSymbols/Notice_Crossing.svg \
SeaMapSymbols/Notice_D1a.svg \
SeaMapSymbols/Notice_D1b.svg \
SeaMapSymbols/Notice_D2a.svg \
SeaMapSymbols/Notice_D2b.svg \
SeaMapSymbols/Notice_D3a.svg \
SeaMapSymbols/Notice_D3b.svg \
SeaMapSymbols/Notice_E10a.svg \
SeaMapSymbols/Notice_E10b.svg \
SeaMapSymbols/Notice_E10c.svg \
SeaMapSymbols/Notice_E10d.svg \
SeaMapSymbols/Notice_E10e.svg \
SeaMapSymbols/Notice_E10f.svg \
SeaMapSymbols/Notice_E11.svg \
SeaMapSymbols/Notice_E13.svg \
SeaMapSymbols/Notice_E14.svg \
SeaMapSymbols/Notice_E15.svg \
SeaMapSymbols/Notice_E16.svg \
SeaMapSymbols/Notice_E17.svg \
SeaMapSymbols/Notice_E18.svg \
SeaMapSymbols/Notice_E19.svg \
SeaMapSymbols/Notice_E1.svg \
SeaMapSymbols/Notice_E20.svg \
SeaMapSymbols/Notice_E21.svg \
SeaMapSymbols/Notice_E22.svg \
SeaMapSymbols/Notice_E23.svg \
SeaMapSymbols/Notice_E24.svg \
SeaMapSymbols/Notice_E2.svg \
SeaMapSymbols/Notice_E3.svg \
SeaMapSymbols/Notice_E4a.svg \
SeaMapSymbols/Notice_E4b.svg \
SeaMapSymbols/Notice_E5_10.svg \
SeaMapSymbols/Notice_E5_11.svg \
SeaMapSymbols/Notice_E5_12.svg \
SeaMapSymbols/Notice_E5_13.svg \
SeaMapSymbols/Notice_E5_14.svg \
SeaMapSymbols/Notice_E5_15.svg \
SeaMapSymbols/Notice_E5_1.svg \
SeaMapSymbols/Notice_E5_2.svg \
SeaMapSymbols/Notice_E5_3.svg \
SeaMapSymbols/Notice_E5_4.svg \
SeaMapSymbols/Notice_E5_5.svg \
SeaMapSymbols/Notice_E5_6.svg \
SeaMapSymbols/Notice_E5_7.svg \
SeaMapSymbols/Notice_E5_8.svg \
SeaMapSymbols/Notice_E5_9.svg \
SeaMapSymbols/Notice_E5.svg \
SeaMapSymbols/Notice_E6.svg \
SeaMapSymbols/Notice_E7_1.svg \
SeaMapSymbols/Notice_E7.svg \
SeaMapSymbols/Notice_E8.svg \
SeaMapSymbols/Notice_E9a.svg \
SeaMapSymbols/Notice_E9b.svg \
SeaMapSymbols/Notice_E9c.svg \
SeaMapSymbols/Notice_E9d.svg \
SeaMapSymbols/Notice_E9e.svg \
SeaMapSymbols/Notice_E9f.svg \
SeaMapSymbols/Notice_E9g.svg \
SeaMapSymbols/Notice_E9h.svg \
SeaMapSymbols/Notice_E9i.svg \
SeaMapSymbols/Notice_E.svg \
SeaMapSymbols/Notice_Junction_L.svg \
SeaMapSymbols/Notice_Junction_R.svg \
SeaMapSymbols/Notice_Junction.svg \
SeaMapSymbols/Notice_Motor.svg \
SeaMapSymbols/Notice_Pcpl.svg \
SeaMapSymbols/Notice_Pcpr.svg \
SeaMapSymbols/Notice_Pcsl.svg \
SeaMapSymbols/Notice_Pcsr.svg \
SeaMapSymbols/Notice_Pmrl.svg \
SeaMapSymbols/Notice_Pmrr.svg \
SeaMapSymbols/Notice_Ppml.svg \
SeaMapSymbols/Notice_Ppmr.svg \
SeaMapSymbols/Notice_Proceed.svg \
SeaMapSymbols/Notice_Psml.svg \
SeaMapSymbols/Notice_Psmr.svg \
SeaMapSymbols/Notice_P.svg \
SeaMapSymbols/Notice_Pwlarl.svg \
SeaMapSymbols/Notice_Pwlarr.svg \
SeaMapSymbols/Notice_Pwrall.svg \
SeaMapSymbols/Notice_Pwralr.svg \
SeaMapSymbols/Notice_Rowboat.svg \
SeaMapSymbols/Notice_Sailboard.svg \
SeaMapSymbols/Notice_Sailboat.svg \
SeaMapSymbols/Notice_Slipway.svg \
SeaMapSymbols/Notice_Speedboat.svg \
SeaMapSymbols/Notice_Sport.svg \
SeaMapSymbols/Notice.svg \
SeaMapSymbols/Notice_Turn.svg \
SeaMapSymbols/Notice_VHF.svg \
SeaMapSymbols/Notice_Waterbike.svg \
SeaMapSymbols/Notice_Waterski.svg \
SeaMapSymbols/No_Wake.svg \
SeaMapSymbols/Perch_Port.svg \
SeaMapSymbols/Perch_Starboard.svg \
SeaMapSymbols/Pillar_Black_Red_Black.svg \
SeaMapSymbols/Pillar_Black_Yellow_Black.svg \
SeaMapSymbols/Pillar_Black_Yellow.svg \
SeaMapSymbols/Pillar_Green_Red_Green.svg \
SeaMapSymbols/Pillar_Green.svg \
SeaMapSymbols/Pillar_Green_White_Green_White.svg \
SeaMapSymbols/Pillar_Red_Green_Red_Green.svg \
SeaMapSymbols/Pillar_Red_Green_Red.svg \
SeaMapSymbols/Pillar_Red.svg \
SeaMapSymbols/Pillar_Red_White_Red_White.svg \
SeaMapSymbols/Pillar_Red_White.svg \
SeaMapSymbols/Pillar.svg \
SeaMapSymbols/Pillar_Yellow_Black.svg \
SeaMapSymbols/Pillar_Yellow_Black_Yellow.svg \
SeaMapSymbols/Pillar_Yellow.svg \
SeaMapSymbols/Pilot.svg \
SeaMapSymbols/Pipeline.svg \
SeaMapSymbols/Platform.svg \
SeaMapSymbols/PortCrane.svg \
SeaMapSymbols/Post.svg \
SeaMapSymbols/Pump_Out.svg \
SeaMapSymbols/Radar_Reflector_250.svg \
SeaMapSymbols/Radar_Reflector_280.svg \
SeaMapSymbols/Radar_Reflector_305.svg \
SeaMapSymbols/Radar_Reflector_320.svg \
SeaMapSymbols/Radar_Reflector_355.svg \
SeaMapSymbols/Radar_Reflector.svg \
SeaMapSymbols/Radar_Station.svg \
SeaMapSymbols/RadioMast.svg \
SeaMapSymbols/Rescue.svg \
SeaMapSymbols/Restricted.svg \
SeaMapSymbols/RockA.svg \
SeaMapSymbols/RockC.svg \
SeaMapSymbols/Rock.svg \
SeaMapSymbols/SailingClub.svg \
SeaMapSymbols/Sandwaves.svg \
SeaMapSymbols/Seaplane.svg \
SeaMapSymbols/Shower.svg \
SeaMapSymbols/Signal_Station.svg \
SeaMapSymbols/Slipway.svg \
SeaMapSymbols/Spar_Black_Red_Black.svg \
SeaMapSymbols/Spar_Black_Yellow_Black.svg \
SeaMapSymbols/Spar_Black_Yellow.svg \
SeaMapSymbols/Spar_Green_Red_Green.svg \
SeaMapSymbols/Spar_Green.svg \
SeaMapSymbols/Spar_Green_White_Green_White.svg \
SeaMapSymbols/Spar_Red_Green_Red_Green.svg \
SeaMapSymbols/Spar_Red_Green_Red.svg \
SeaMapSymbols/Spar_Red.svg \
SeaMapSymbols/Spar_Red_White_Red_White.svg \
SeaMapSymbols/Spar_Red_White.svg \
SeaMapSymbols/Spar.svg \
SeaMapSymbols/Spar_Yellow_Black.svg \
SeaMapSymbols/Spar_Yellow_Black_Yellow.svg \
SeaMapSymbols/Spar_Yellow.svg \
SeaMapSymbols/Sphere_Black_Red_Black.svg \
SeaMapSymbols/Sphere_Black_Yellow_Black.svg \
SeaMapSymbols/Sphere_Black_Yellow.svg \
SeaMapSymbols/Sphere_Green_Red_Green.svg \
SeaMapSymbols/Sphere_Green.svg \
SeaMapSymbols/Sphere_Green_White_Green_White.svg \
SeaMapSymbols/Sphere_Red_Green_Red_Green.svg \
SeaMapSymbols/Sphere_Red_Green_Red.svg \
SeaMapSymbols/Sphere_Red.svg \
SeaMapSymbols/Sphere_Red_White_Red_White.svg \
SeaMapSymbols/Sphere_Red_White.svg \
SeaMapSymbols/Sphere.svg \
SeaMapSymbols/Sphere_White_Yellow.svg \
SeaMapSymbols/Sphere_Yellow_Black.svg \
SeaMapSymbols/Sphere_Yellow_Black_Yellow.svg \
SeaMapSymbols/Sphere_Yellow_Red.svg \
SeaMapSymbols/Sphere_Yellow.svg \
SeaMapSymbols/Spire.svg \
SeaMapSymbols/Stake_Green.svg \
SeaMapSymbols/Stake_Red.svg \
SeaMapSymbols/Stake.svg \
SeaMapSymbols/Stake_Yellow.svg \
SeaMapSymbols/Storage.svg \
SeaMapSymbols/Super_Black.svg \
SeaMapSymbols/Super.svg \
SeaMapSymbols/Super_Yellow.svg \
SeaMapSymbols/Temple.svg \
SeaMapSymbols/Tidal_Scale.svg \
SeaMapSymbols/Tide_Gauge.svg \
SeaMapSymbols/Toilet.svg \
SeaMapSymbols/Top_Board.svg \
SeaMapSymbols/Top_Can_Beacon.svg \
SeaMapSymbols/Top_Can_Buoy_Small.svg \
SeaMapSymbols/Top_Can_Buoy.svg \
SeaMapSymbols/Top_Can_Float.svg \
SeaMapSymbols/Top_Can_Green_Beacon.svg \
SeaMapSymbols/Top_Can_Green_Buoy_Small.svg \
SeaMapSymbols/Top_Can_Green_Buoy.svg \
SeaMapSymbols/Top_Can_Green_Float.svg \
SeaMapSymbols/Top_Can_Red_Beacon.svg \
SeaMapSymbols/Top_Can_Red_Buoy_Small.svg \
SeaMapSymbols/Top_Can_Red_Buoy.svg \
SeaMapSymbols/Top_Can_Red_Float.svg \
SeaMapSymbols/Top_Can_Red_White_Red_Barrel.svg \
SeaMapSymbols/Top_Can_Red_White_Red_Beacon.svg \
SeaMapSymbols/Top_Can_Red_White_Red_Buoy_Small.svg \
SeaMapSymbols/Top_Can_Red_White_Red_Buoy.svg \
SeaMapSymbols/Top_Can.svg \
SeaMapSymbols/Top_Can_Yellow_Beacon.svg \
SeaMapSymbols/Top_Can_Yellow_Buoy_Small.svg \
SeaMapSymbols/Top_Can_Yellow_Buoy.svg \
SeaMapSymbols/Top_Can_Yellow_Float.svg \
SeaMapSymbols/Top_Cone_Beacon.svg \
SeaMapSymbols/Top_Cone_Buoy_Small.svg \
SeaMapSymbols/Top_Cone_Buoy.svg \
SeaMapSymbols/Top_ConeD.svg \
SeaMapSymbols/Top_Cone_Float.svg \
SeaMapSymbols/Top_Cone_Green_Beacon.svg \
SeaMapSymbols/Top_Cone_Green_Buoy_Small.svg \
SeaMapSymbols/Top_Cone_Green_Buoy.svg \
SeaMapSymbols/Top_Cone_Green_Float.svg \
SeaMapSymbols/Top_Cone_Inv_Beacon.svg \
SeaMapSymbols/Top_Cone_Inv_Red_Beacon.svg \
SeaMapSymbols/Top_Cone_Inv.svg \
SeaMapSymbols/Top_Cone_Red_Beacon.svg \
SeaMapSymbols/Top_Cone_Red_Buoy_Small.svg \
SeaMapSymbols/Top_Cone_Red_Buoy.svg \
SeaMapSymbols/Top_Cone_Red_Float.svg \
SeaMapSymbols/Top_ConesDU_Beacon.svg \
SeaMapSymbols/Top_ConesDU_Red_Green_Beacon.svg \
SeaMapSymbols/Top_Cone.svg \
SeaMapSymbols/Top_Cone_Yellow_Beacon.svg \
SeaMapSymbols/Top_Cone_Yellow_Buoy_Small.svg \
SeaMapSymbols/Top_Cone_Yellow_Buoy.svg \
SeaMapSymbols/Top_Cone_Yellow_Float.svg \
SeaMapSymbols/Top_Cross_Beacon.svg \
SeaMapSymbols/Top_Cross.svg \
SeaMapSymbols/Top_Cross_Yellow_Beacon.svg \
SeaMapSymbols/Top_Diamond_Beacon.svg \
SeaMapSymbols/Top_Diamond_Green_White_Beacon.svg \
SeaMapSymbols/Top_Diamond.svg \
SeaMapSymbols/Top_Diamond_White_Green_Beacon.svg \
SeaMapSymbols/Top_Diamond_Yellow_Black_Yellow_Beacon.svg \
SeaMapSymbols/Top_East_Beacon.svg \
SeaMapSymbols/Top_East_Buoy_Small.svg \
SeaMapSymbols/Top_East_Buoy.svg \
SeaMapSymbols/Top_East_Float.svg \
SeaMapSymbols/Top_East.svg \
SeaMapSymbols/Top_Isol_Beacon.svg \
SeaMapSymbols/Top_Isol_Buoy_Small.svg \
SeaMapSymbols/Top_Isol_Buoy.svg \
SeaMapSymbols/Top_Isol_Float.svg \
SeaMapSymbols/Top_Isol.svg \
SeaMapSymbols/Top_Mooring_Buoy_Small.svg \
SeaMapSymbols/Top_Mooring_Float.svg \
SeaMapSymbols/Top_Mooring.svg \
SeaMapSymbols/Top_North_Beacon.svg \
SeaMapSymbols/Top_North_Buoy_Small.svg \
SeaMapSymbols/Top_North_Buoy.svg \
SeaMapSymbols/Top_North_Float.svg \
SeaMapSymbols/Top_North.svg \
SeaMapSymbols/Top_South_Beacon.svg \
SeaMapSymbols/Top_South_Buoy_Small.svg \
SeaMapSymbols/Top_South_Buoy.svg \
SeaMapSymbols/Top_South_Float.svg \
SeaMapSymbols/Top_South.svg \
SeaMapSymbols/Top_Sphere_Beacon.svg \
SeaMapSymbols/Top_Sphere_Buoy_Small.svg \
SeaMapSymbols/Top_Sphere_Buoy.svg \
SeaMapSymbols/Top_Sphere_Float.svg \
SeaMapSymbols/Top_Sphere_Red_Beacon.svg \
SeaMapSymbols/Top_Sphere_Red_Buoy_Small.svg \
SeaMapSymbols/Top_Sphere_Red_Buoy.svg \
SeaMapSymbols/Top_Sphere_Red_Float.svg \
SeaMapSymbols/Top_Sphere_Red_Green_Beacon.svg \
SeaMapSymbols/Top_Sphere_Red_Green_Buoy_Small.svg \
SeaMapSymbols/Top_Sphere_Red_Green_Buoy.svg \
SeaMapSymbols/Top_Sphere_Red_Green_Float.svg \
SeaMapSymbols/Top_Sphere.svg \
SeaMapSymbols/Top_Square_Beacon.svg \
SeaMapSymbols/Top_Square.svg \
SeaMapSymbols/Top_Square_White_Red_Beacon.svg \
SeaMapSymbols/Top_Square_White_Red_White_Beacon.svg \
SeaMapSymbols/Top_Square_Yellow_Black_Yellow_Beacon.svg \
SeaMapSymbols/Top_Triangle_Beacon.svg \
SeaMapSymbols/Top_TriangleD.svg \
SeaMapSymbols/Top_Triangle_Green_Beacon.svg \
SeaMapSymbols/Top_Triangle_Inv_Beacon.svg \
SeaMapSymbols/Top_Triangle_Inv_Red_Beacon.svg \
SeaMapSymbols/Top_Triangle_Inv.svg \
SeaMapSymbols/Top_Triangle_Inv_White_Red_Beacon.svg \
SeaMapSymbols/Top_Triangle.svg \
SeaMapSymbols/Top_Triangle_White_Green_Beacon.svg \
SeaMapSymbols/Top_West_Beacon.svg \
SeaMapSymbols/Top_West_Buoy_Small.svg \
SeaMapSymbols/Top_West_Buoy.svg \
SeaMapSymbols/Top_West_Float.svg \
SeaMapSymbols/Top_West.svg \
SeaMapSymbols/Top_X_Beacon.svg \
SeaMapSymbols/Top_X_Buoy_Small.svg \
SeaMapSymbols/Top_X_Buoy.svg \
SeaMapSymbols/Top_X_Float.svg \
SeaMapSymbols/Top_X_Red_Beacon.svg \
SeaMapSymbols/Top_X_Red_Buoy_Small.svg \
SeaMapSymbols/Top_X_Red_Buoy.svg \
SeaMapSymbols/Top_X_Red_Float.svg \
SeaMapSymbols/Top_X.svg \
SeaMapSymbols/Top_X_Yellow_Beacon.svg \
SeaMapSymbols/Top_X_Yellow_Buoy_Small.svg \
SeaMapSymbols/Top_X_Yellow_Buoy.svg \
SeaMapSymbols/Top_X_Yellow_Float.svg \
SeaMapSymbols/Tower_Black_Red_Black.svg \
SeaMapSymbols/Tower_Black_Yellow_Black.svg \
SeaMapSymbols/Tower_Black_Yellow.svg \
SeaMapSymbols/Tower_Green_Red_Green.svg \
SeaMapSymbols/Tower_Green.svg \
SeaMapSymbols/Tower_Green_White_Green_White.svg \
SeaMapSymbols/Tower_Red_Green_Red_Green.svg \
SeaMapSymbols/Tower_Red_Green_Red.svg \
SeaMapSymbols/Tower_Red.svg \
SeaMapSymbols/Tower_Red_White_Red_White.svg \
SeaMapSymbols/Tower.svg \
SeaMapSymbols/Tower_Yellow_Black.svg \
SeaMapSymbols/Tower_Yellow_Black_Yellow.svg \
SeaMapSymbols/Tower_Yellow.svg \
SeaMapSymbols/Triangle_Bottom.svg \
SeaMapSymbols/Triangle_Left.svg \
SeaMapSymbols/Triangle_Right.svg \
SeaMapSymbols/Triangle.svg \
SeaMapSymbols/Turning_Point.svg \
SeaMapSymbols/Visitor_Berth.svg \
SeaMapSymbols/Visitor_Mooring.svg \
SeaMapSymbols/Waste_Disposal.svg \
SeaMapSymbols/WaterTower.svg \
SeaMapSymbols/Wind_Farm.svg \
SeaMapSymbols/Windmill.svg \
SeaMapSymbols/Wind_Motor.svg \
SeaMapSymbols/Windsock.svg \
SeaMapSymbols/Withy_Port.svg \
SeaMapSymbols/Withy_Starboard.svg \
SeaMapSymbols/WreckD.svg \
SeaMapSymbols/WreckND.svg \
SeaMapSymbols/WreckS.svg

dist_noinst_DATA = test/Makefile test/testdata.osm test/rules

