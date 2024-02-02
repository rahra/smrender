/* Copyright 2011-2023 Bernhard R. Fischer, 4096R/8E24F29D <bf@abenteuerland.at>
 *
 * This file is part of smrender.
 *
 * Smrender is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3 of the License.
 *
 * Smrender is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with smrender. If not, see <http://www.gnu.org/licenses/>.
 */

/*! \file smloadosm.h
 * This file contains the definitions for loading the OSM files.
 *
 *  \author Bernhard R. Fischer, <bf@abenteuerland.at>
 */
#ifndef SMLOADOSM_H
#define SMLOADOSM_H

#define READ_STATS_TIME 15

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

void init_stats(struct dstats *);
int update_stats(const osm_obj_t *, struct dstats *);
void fin_stats(struct dstats *);

#endif

