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

#include "smconfig.h"

#include "osm_inplace.h"
#include "bstring.h"
#include "bxtree.h"
#include "smath.h"

#define SW_AUTHOR "Bernhard R. Fischer"
#define SW_AEMAIL "bf@abenteuerland.at"
#define SW_COPY "© 2011"

#define SPECIAL_DIRECT 0x0000
#define SPECIAL_REGEX 0x0001
#define SPECIAL_GT 0x0002
#define SPECIAL_LT 0x0003
#define SPECIAL_INVERT 0x8000
#define SPECIAL_NOT 0x4000
#define SPECIAL_MASK 0x00ff

#define POS_M 0
#define POS_N 1
#define POS_S 2
#define POS_C 0
#define POS_E 4
#define POS_W 8
#define POS_UC 16

// distance of grid lines in degrees (should be multple of G_TICKS)
#define G_GRID (10.0 / 60.0)
// distance of axis ticks in degrees (should be multiple of G_STICKS)
#define G_TICKS (1.0 / 60.0)
// distance of axis subticks in degrees
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
// convert mm to degrees
#define MM2LAT(x) ((x) * (rd->y1c - rd->y2c) / PX2MM(rd->h))
#define MM2LON(x) ((x) * (rd->x2c - rd->x1c) / PX2MM(rd->w))

#define MAX_ITER 8


typedef struct rdata rdata_t;
typedef struct onode onode_t;
typedef struct orule orule_t;
typedef int (*tree_func_t)(osm_obj_t*, struct rdata*, void*);
typedef int (*ext_func_t)(osm_obj_t*);
typedef union structor
{
      void (*func)(void);
      void *sym;
} structor_t;
 
// indexes to object tree
enum {IDX_NODE, IDX_WAY};

enum {E_SM_OK, E_RTYPE_NA, E_ACT_NOT_IMPL, E_SYNTAX, E_REF_ERR};

struct specialTag
{
   short type;
   union
   {
      regex_t re;
      double val;
   };
};

/* struct otag
{
   bstring_t k;
   bstring_t v;
};*/

struct stag
{
   struct specialTag stk;
   struct specialTag stv;
};

struct auto_rot
{
   double phase;     // phase of weighting function. 0 degress means east (0)
                     // and west (180) is most important
   int autocol;      // background color which is used for auto-rotation detection
   double weight;    // auto-rot weighting (0-1), 1 means everything equal
};

struct actImage
{
   double angle;
   struct auto_rot rot;
   gdImage *img;
};

struct actCaption
{
   short pos;        // position, or'd POS_x macros
   int col;          // caption color
   char *font;       // pointer to font filename
   char *key;        // pointer to caption string
   double size;      // font size in mm
   double angle;     // angle to rotate caption. 0 degress equals east,
                     // counterclockwise. NAN means auto-rotate
   struct auto_rot rot;
};

struct actFunction
{
   union
   {
      ext_func_t func;
      void *sym;
   } main;
   union
   {
      int (*func)(const orule_t*);
      void *sym;
   } ini;
   structor_t fini;
   void *libhandle;
   char *parm;
};

struct actOutput
{
   //char *name;
   FILE *fhandle;
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
      struct actOutput out;
   };
   short tag_cnt;
   struct stag stag[];
};

/*struct onode
{
   struct osm_node nd;
   int ref_cnt;
   int64_t *ref;
   int tag_cnt;
   struct otag otag[];
};*/

struct orule
{
   osm_obj_t *oo;
   struct rule rule;
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
   void *lo_addr, *hi_addr;   // lowest and highest memory address
   int ver_cnt;
   int ver[MAX_ITER];
};

struct rdata
{
   // root node of objects (nodes and ways)
   bx_node_t *obj;
   // root nodes of node rules and way rules
   bx_node_t *rules;
   // pointer to image data
   gdImage *img;
   // left upper and right bottom coordinates
   double x1c, y1c, x2c, y2c;
   // coordinate with/height (wc=x2c-x1c, hc=y1c-y2c)
   double wc, hc;
   // mean latitude and its length in degrees corresponding to the real nautical mails
   double mean_lat, mean_lat_len;
   double mean_lon;
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
};

struct filter
{
   // c1 = left upper corner, c2 = right lower corner of bounding box
   struct coord c1, c2;
   // set use_bbox to 1 if bbox should be honored
   int use_bbox;
   // pointer to rules tree (or NULL if it should be ignored)
   bx_node_t *rules;
};

enum {WHITE, YELLOW, BLACK, BLUE, MAGENTA, BROWN};
enum {LAT, LON};
enum {ACT_NA, ACT_IMG, ACT_CAP, ACT_FUNC, ACT_DRAW, ACT_IGNORE, ACT_OUTPUT};
enum {DRAW_SOLID, DRAW_DASHED, DRAW_DOTTED, DRAW_TRANSPARENT};

// select projection
// PRJ_DIRECT directly projects the bounding box onto the image.
// PRJ_MERC_PAGE chooses the largest possible bb withing the given bb to fit into the image.
// PRJ_MERC_BB chooses the image size being selected depending on the given bb.
enum {PRJ_DIRECT, PRJ_MERC_PAGE, PRJ_MERC_BB};


/* smrender.c */
int traverse(const bx_node_t*, int, int, tree_func_t, struct rdata*, void*);
int print_onode(FILE *, const osm_obj_t*);
int col_freq(struct rdata *, int, int, int, int, double, int);
int cf_dist(struct rdata *, int, int, int, int, double, int, int);
double rot_pos(int, int, double, int *, int *);
double color_frequency(struct rdata *, int, int, int, int, int);
void mk_chart_coords(int, int, struct rdata*, double*, double*);
int poly_area(const osm_way_t*, struct coord *, double *);


/* smutil.c */
int match_attr(const osm_obj_t*, const char *, const char *);
int bs_match_attr(const osm_obj_t*, const struct otag *, const struct stag*);
int bs_match(const bstring_t *, const bstring_t *, const struct specialTag *);
void set_util_rd(struct rdata*);
//void disable_put(void);
int put_object0(bx_node_t**, int64_t, void*, int);
int put_object(osm_obj_t*);
void *get_object0(bx_node_t*, int64_t, int);
void *get_object(int, int64_t);
//struct onode *malloc_object(int , int);
int64_t unique_node_id(void);
int64_t unique_way_id(void);
void set_const_tag(struct otag*, char*, char*);

/* smloadosm.c */
void osm_read_exit(void);
int read_osm_file(hpx_ctrl_t*, bx_node_t**, struct filter*);
size_t onode_mem(void);
void install_sigusr1(void);

/* smcoast.c */
void init_cat_poly(struct rdata *);
int is_closed_poly(const osm_way_t*);

/* smgrid.c */
void grid2(struct rdata*);

//extern int oline_;

#endif

