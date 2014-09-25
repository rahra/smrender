/* Copyright 2011 Bernhard R. Fischer, 2048R/5C5FFD47 <bf@abenteuerland.at>
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

#ifndef SMCOAST_H
#define SMCOAST_H

#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

#include "smrender_dev.h"


// initial number of ref array
#define INIT_MAX_REF 20
#define MAX_OPEN_POLY 32


struct corner_point
{
   struct pcoord pc;
   osm_node_t *n;
};

struct poly
{
   struct poly *next, *prev;  //!< pointer to next/prev directly connected segment
   osm_way_t *w;              //!< pointer to way segment
   short del;                 //!< cat_poly: 1 if element should be removed from list
   short open;                //!< cat_poly: 1 if element is connected but still open way
   double area;               //!< gen_layer: area of polygon
   short cw;                  //!< 1 if polygon is clockwise, otherwise 0
   osm_way_t *nw;             //!< pointer to new way
};

struct wlist
{
   int ref_cnt, max_ref;
   struct poly ref[];
};

struct catpoly
{
   short ign_incomplete;      // 1 if incomplete "closed" polys should be ignored
   short no_corner;           // 1 if no cornerpoints should be inserted
   struct wlist *wl;          // pointer to wlist
   osm_obj_t obj;             // list of tags to copy
};

struct pdef
{
   int wl_index;        // index of way within wlist
   int pn;              // index number of destined point within way
   union
   {
      struct pcoord pc; // bearing to pointer
      int64_t nid;      // node id
   };
};


//static void node_brg(struct pcoord*, struct coord*, int64_t);
//static void init_corner_brg(const struct rdata*, const struct coord*, struct corner_point*);
int compare_poly_area(const struct poly*, const struct poly*);
//int cat_poly_ini(smrule_t*);
int gather_poly0(osm_way_t *, struct wlist **);
struct wlist *init_wlist(void);
const osm_way_t *page_way(void);


#endif

