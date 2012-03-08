#ifndef SMRULES_H
#define SMRULES_H


int act_image(osm_node_t*, struct rdata*, struct orule*);
int act_caption(osm_node_t*, struct rdata*, struct orule*);
int act_wcaption(osm_way_t*, struct rdata*, struct orule*);
int act_open_poly(osm_way_t*, struct rdata*, struct orule*);
int act_fill_poly(osm_way_t*, struct rdata*, struct orule*);

#endif

