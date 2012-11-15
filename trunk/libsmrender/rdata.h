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

// maximum number if different rule versions (processing iterations)
#define MAX_ITER 8
//#define MAX_COLOR 32


/*#ifdef HAVE_GD
typedef gdImage image_t;
#else
typedef void image_t;
#endif*/
#ifndef image_t
#define image_t void
#endif

struct bbox
{
   struct coord ll, ru;
};

struct dstats
{
   //struct coord lu;  // left upper
   //struct coord rb;  // right bottom
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

   // ***** this is libgd2 specific ***** 
   // pointer to image data
   //image_t *img;
   // image colors
   //int col[MAX_COLOR];
};


#endif

