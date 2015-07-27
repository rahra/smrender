#ifndef SMLOADOSM_H
#define SMLOADOSM_H

#include "libhpxml.h"
//#include "rdata.h"

struct filter
{
   // c1 = left upper corner, c2 = right lower corner of bounding box
   struct coord c1, c2;
   // set use_bbox to 1 if bbox should be honored
   int use_bbox;
   // pointer to rules tree (or NULL if it should be ignored)
   bx_node_t *rules;
};


void osm_read_exit(void);
int read_osm_obj(hpx_ctrl_t *, hpx_tree_t **, osm_obj_t **);
int read_osm_file(hpx_ctrl_t*, bx_node_t**, const struct filter*, struct dstats*);
hpx_ctrl_t *open_osm_source(const char*, int);

#endif

