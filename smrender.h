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

#ifndef SMRENDER_H
#define SMRENDER_H

#include <stdint.h>
#include <gd.h>
#include <regex.h>

#include "osm_inplace.h"
#include "bstring.h"
//#include "libhpxml.h"
//#include "smlog.h"
#include "bxtree.h"
//#include "smact.h"
//#include "smrules.h"
#include "smath.h"


#define SPECIAL_DIRECT 0x0000
#define SPECIAL_REGEX 0x0001
#define SPECIAL_INVERT 0x8000
#define SPECIAL_NOT 0x4000
#define SPECIAL_MASK 0x00ff

#define POS_M 0
#define POS_N 1
#define POS_S 2
#define POS_C 0
#define POS_E 4
#define POS_W 8

#define G_GRID (10.0 / 60.0)
#define G_TICKS (1.0 / 60.0)
#define G_STICKS (G_TICKS / 4.0)
// margin from paper edge to border of chart (mm)
#define G_MARGIN 15.0
// width of ticks border (mm)
#define G_TW 5.0
// width of subticks border (mm)
#define G_STW 2.5
// line width of chart border (mm)
#define G_BW 0.1

#define ANGLE_DIFF 10


typedef struct rdata rdata_t;
typedef struct onode onode_t;


struct specialTag
{
   short type;
   regex_t re;
};

struct otag
{
   bstring_t k;
   bstring_t v;
   struct specialTag stk;
   struct specialTag stv;
};

struct actImage
{
   //char *fn;
   gdImage *img;
};

struct actCaption
{
   short pos;
   int col;
   char *font;
   char *key;
   double size;
   double angle;
};

struct actFunction
{
   //char *lib;
   int (*func)(const rdata_t*, const onode_t*);
};

struct drawStyle
{
   int col;
   double width;
   short style;
   short used;
};

struct actDraw
{
   struct drawStyle fill;
   struct drawStyle border;
};

struct rule
{
   short type;
   union
   {
      struct actImage img;
      struct actCaption cap;
      struct actFunction func;
      struct actDraw draw;
   };
};

struct onode
{
   struct osm_node nd;
   struct rule rule;
   int ref_cnt;
   int64_t *ref;
   int tag_cnt;
   struct otag otag[];
};

struct grid
{
   double lat_ticks, lon_ticks;
   double lat_sticks, lon_sticks;
   double lat_g, lon_g;
};

struct dstats
{
   struct coord lu;  // left upper
   struct coord rb;  // right bottom
   int64_t min_nid;
   int64_t max_nid;
   int64_t min_wid;
   int64_t max_wid;
};

struct rdata
{
   bx_node_t *nodes, *ways;
   bx_node_t *nrules, *wrules;
   gdImage *img;
   double x1c, y1c, x2c, y2c, wc, hc;
   double mean_lat, mean_lat_len;
   int w, h;
   int dpi;
   double scale;
   struct grid grd;
   struct dstats ds;
   int col[6];
};

enum {WHITE, YELLOW, BLACK, BLUE, MAGENTA, BROWN};
enum {LAT, LON};
enum {ACT_NA, ACT_IMG, ACT_CAP, ACT_FUNC, ACT_DRAW};
enum {DRAW_SOLID, DRAW_DASHED, DRAW_DOTTED};

// select projection
// PRJ_DIRECT directly projects the bounding box onto the image.
// PRJ_MERC_PAGE chooses the largest possible bb withing the given bb to fit into the image.
// PRJ_MERC_BB chooses the image size being selected depending on the given bb.
enum {PRJ_DIRECT, PRJ_MERC_PAGE, PRJ_MERC_BB};


/* smrender.c */
void traverse(const bx_node_t *, int, void (*)(struct onode*, struct rdata*, void*), struct rdata *, void *);
int match_attr(const struct onode *, const char *, const char *);
int print_onode(FILE *, const struct onode *);
int col_freq(struct rdata *, int, int, int, int, double, int);
double rot_pos(int, int, double, int *, int *);

/* smloadosm.c */
int read_osm_file(hpx_ctrl_t *, bx_node_t **, bx_node_t **);
#ifdef MEM_USAGE
size_t onode_mem(void);
#endif

/* smcoast.c */
int cat_poly(struct rdata *);

extern int oline_;

#endif

