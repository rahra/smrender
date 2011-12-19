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
#include "bxtree.h"
#include "smath.h"

#define SW_AUTHOR "Bernhard R. Fischer"
#define SW_AEMAIL "bf@abenteuerland.at"
#define SW_COPY "© 2011"

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
#define G_FONT "/usr/share/fonts/truetype/ttf-liberation/LiberationSans-Regular.ttf"
#define G_FTSIZE 3
#define G_SFTSIZE 2

#define ANGLE_DIFF 10

// convert mm to pixels
#define MM2PX(x) round((double) (x) * rd->dpi / 25.4)
// convert mm to points (pt)
#define MM2PT(x) round((double) (x) * 72.72 / 25.4)
// convert pixels to mm
#define PX2MM(x) ((double) (x) * 25.4 / rd->dpi)

typedef struct rdata rdata_t;
typedef struct onode onode_t;
typedef void (*tree_func_t)(struct onode*, struct rdata*, void*);
typedef int (*ext_func_t)(struct onode*, struct rdata*);


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
   double angle;
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
   union
   {
      ext_func_t func;
      void *sym;
   };
   void *libhandle;
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
   long ncnt, wcnt;
   int64_t min_nid;
   int64_t max_nid;
   int64_t min_wid;
   int64_t max_wid;
};

struct cb_func
{ 
   void (*log_msg)(int, const char*, ...);
   struct onode *(*get_object)(bx_node_t*, int64_t);
};

struct rdata
{
   // root nodes of node tree and way tree
   bx_node_t *nodes, *ways;
   // root nodes of node rules and way rules
   bx_node_t *nrules, *wrules;
   // pointer to image data
   gdImage *img;
   // left upper and right bottom coordinates
   double x1c, y1c, x2c, y2c;
   // coordinate with/height (wc=x2c-x1c, hc=y1c-y2c)
   double wc, hc;
   // mean latitude and its length in degrees
   double mean_lat, mean_lat_len;
   // (pixel) image width and height
   int w, h;
   // pixel resolution
   int dpi;
   // scale
   double scale;
   // grid drawing data
   struct grid grd;
   // node/way stats
   struct dstats ds;
   // image colors
   int col[6];
   // callbacks for external function
   struct cb_func cb;
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
int cf_dist(struct rdata *, int, int, int, int, double, int, int);
double rot_pos(int, int, double, int *, int *);
double color_frequency(struct rdata *, int, int, int, int, int);
void mk_chart_coords(int, int, struct rdata*, double*, double*);
struct onode *get_object(bx_node_t*, int64_t);

/* smloadosm.c */
int read_osm_file(hpx_ctrl_t *, bx_node_t **, bx_node_t **);
#ifdef MEM_USAGE
size_t onode_mem(void);
#endif

/* smcoast.c */
int cat_poly(struct rdata *);


extern int oline_;

#endif

