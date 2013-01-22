/* Copyright 2011 Bernhard R. Fischer, 2048R/5C5FFD47 <bf@abenteuerland.at>
 *
 * This file is part of smrender.
 *
 * Smfilter is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3 of the License.
 *
 * Smfilter is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with smrender. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef RDATA_H
#define RDATA_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdint.h>

#include "smrender.h"
#include "bxtree.h"


// maximum number if different rule versions (processing iterations)
#define MAX_ITER 8


typedef enum
{
   U_MM, U_PX, U_PT, U_IN
} unit_t;


struct bbox
{
   struct coord ll, ru;
};

struct dstats
{
   struct bbox bb;
   long ncnt, wcnt, rcnt;
   int64_t min_nid;
   int64_t max_nid;
   int64_t min_wid;
   int64_t max_wid;
   const void *lo_addr, *hi_addr;   // lowest and highest memory address
   int ver_cnt;
   int ver[MAX_ITER];
};

struct rdata
{
   // root node of objects (nodes and ways)
//   bx_node_t *obj;
   // root nodes of node rules and way rules
   bx_node_t *rules;
   // bounding box (left lower and right upper coordinates)
   struct bbox bb;
   // coordinate with/height (wc=bb.ru.lon-bb.ll.lon, hc=bb.ru.lat-bb.ll.lat)
   double wc, hc;
   // mean latitude and its length in degrees corresponding to the real nautical miles
   double mean_lat, mean_lat_len;
   double mean_lon;
   // hyperbolic values for transversial Mercator (latitude stretching)
   double lath, lath_len;
   // (pixel) image width and height of rendered image
   int w, h;
   // (pixel) image width and height of final image
   int fw, fh;
   // pixel resolution
   int dpi;
   // oversampling factor
   int ovs;
   // scale
   double scale;
   // node/way stats
   struct dstats ds;
   // pointer to cmd line string
   char *cmdline;
   // chart title
   char *title;
};


double mm2ptf(double);
double mm2pxf(double);
int mm2pxi(double);
double px2mm(double);
void geo2pxf(double, double, double*, double*);
void geo2pxi(double, double, int*, int*);
#define mk_paper_coords(p0, p1, p2, p3, p4) geo2pxi(p1, p0, p3, p4)


struct rdata *rdata_get(void);
#define get_rdata rdata_get
void rdata_log(void);
double rdata_px_unit(double, unit_t);
double rdata_width(unit_t);
double rdata_height(unit_t);
int rdata_dpi(void);
double rdata_square_nm(void);

#endif

