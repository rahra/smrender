/* Copyright 2011-2015 Bernhard R. Fischer, 2048R/5C5FFD47 <bf@abenteuerland.at>
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


//! maximum number if different rule versions (processing iterations)
#define MAX_ITER 64
//! define OSM version number which contain sub routines
#define SUBROUTINE_VERSION 0x10000
//! if set in rdata.flags, a page border way is generated
#define RD_CORNER_POINTS 1
#define RD_LANDSCAPE 2
#define RD_UIDS 4          //<! output IDs unsigned

// convert mm to pixels
#define MM2PX(x) mm2pxf(x)
// convert pixels to mm
#define px2mm(x) rdata_px_unit(x, U_MM)
#define PX2MM(x) px2mm(x)
// convert mm to degrees
#define MM2LAT(x) ((x) * (rd->bb.ru.lat - rd->bb.ll.lat) / PX2MM(rd->h))
#define MM2LON(x) ((x) * (rd->bb.ru.lon - rd->bb.ll.lon) / PX2MM(rd->w))


typedef enum
{
   // unit "1"
   U_1,
   // units in respect to the page
   U_MM, U_CM, U_PX, U_PT, U_IN,
   // units in respect to reality
   U_NM, U_KM, U_M, U_KBL, U_FT,
   // degrees/minutes on a great circle
   U_DEG, U_MIN
} unit_t;

typedef struct value
{
   unit_t u;
   double val;
} value_t;

struct bbox
{
   struct coord ll, ru;
};

struct dstats
{
   struct bbox bb;
   long cnt[4];
   int64_t min_id[4];
   int64_t max_id[4];
   int id_bits[4];
   int64_t id_mask[4];
   const void *lo_addr, *hi_addr;   // lowest and highest memory address
   int ver_cnt;
   int ver[MAX_ITER];
};

/*! This structure contains all core parameters and settings which are
 * necessary for Smrender to operate properly.
 */
struct rdata
{
   //! root node of node rules and way rules
   bx_node_t *rules;
   //! If need_index is set to 1, Smrender will create the reverse pointers
   //(index). Otherwise no index is created which is less memory consuming.
   int need_index;
   //! root node of reverse pointers for OSM objects
   bx_node_t *index;
   //! bounding box (left lower and right upper coordinates)
   struct bbox bb;
   //! polygon window instead of bbox
   int polygon_window;
   //! coordinates of polygon (ll, rl, ru, lu)
   struct coord pw[4];
   //! rotation of page
   double rot;
   //! coordinate width in degrees (wc=bb.ru.lon-bb.ll.lon)
   double wc, 
   //! coordinate height in degrees (hc=bb.ru.lat-bb.ll.lat)
          hc;
   //! mean latitude and its length in degrees corresponding to the real nautical miles
   double mean_lat, mean_lat_len;
   //! mean longitude in degrees
   double mean_lon;
   //! hyperbolic value of mean latitude, necessary for Mercator projection (latitude stretching)
   double lath,
   //! difference between hyperbolic max. and min. latitudes
          lath_len;
   //! (pixel) image width of rendered image
   double w,
   //! (pixel) image height of rendered image
          h;
   //! page (pixel) width, this == rd->w if no rotation takes place (i.e. rd->rot = 0)
   double pgw;
   //! page (pixel) height
   double pgh;
   //! pixel resolution
   int dpi;
   //! scale
   double scale;
   //! node/way stats
   struct dstats ds;
   //! pointer to cmd line string
   char *cmdline;
   //! chart title
   char *title;
   //! general control flags (RD_xxx)
   int flags;
   //! offset of output ids
   int64_t id_off;
   //! default image scale
   double img_scale;
};


double mm2ptf(double);
double mm2pxf(double);
int mm2pxi(double);
//double px2mm(double);
void pxf2geo(double, double, double*, double*);
void geo2pt(double, double, double*, double*);
void geo2pxf(double, double, double*, double*);
void geo2pxi(double, double, int*, int*);
#define mk_paper_coords(p0, p1, p2, p3, p4) geo2pxi(p1, p0, p3, p4)


struct rdata *rdata_get(void);
#define get_rdata rdata_get
void rdata_log(void);
double rdata_px_unit(double, unit_t);
double rdata_unit_px(double, unit_t);
double rdata_unit(const value_t *, unit_t );
double rdata_page_width(unit_t);
double rdata_page_height(unit_t);
double rdata_width(unit_t);
double rdata_height(unit_t);
int rdata_dpi(void);
double rdata_square_nm(void);
double rdata_square_mm(void);
int is_on_page(const struct coord *);
double rdata_scale(void);

#endif

