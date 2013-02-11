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

#ifndef SMRENDER_DEV_H
#define SMRENDER_DEV_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdint.h>
#ifdef HAVE_CAIRO
#include <cairo.h>
#endif
#include <regex.h>

#include "smrender.h"
#include "smaction.h"
#include "rdata.h"

#include "osm_inplace.h"
#include "bstring.h"
#include "bxtree.h"
#include "smath.h"
#include "lists.h"
#include "libhpxml.h"


#define EXIT_NORULES 128
#define EXIT_NODATA 129

#define USER_GRID 2
#define AUTO_GRID 1
#define NO_GRID 0

#define POS_M 0
#define POS_N 1
#define POS_S 2
#define POS_C 0
#define POS_E 4
#define POS_W 8
#define POS_UC 16

// macro to convert minutes to degrees
#define MIN2DEG(x) ((double) (x) / 60.0)
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

#define MAX_SHAPE_PCOUNT 2000

// convert mm to pixels
#define MM2PX(x) mm2pxi(x)
// convert mm to pdf points (pt)
#define MM2PT(x) round((double) (x) * 72.0 / 25.4)
// convert pixels to mm
#define PX2MM(x) px2mm(x)
// convert mm to degrees
#define MM2LAT(x) ((x) * (rd->bb.ru.lat - rd->bb.ll.lat) / PX2MM(rd->h))
#define MM2LON(x) ((x) * (rd->bb.ru.lon - rd->bb.ll.lon) / PX2MM(rd->w))
// default oversampling factor
#ifdef HAVE_CAIRO
#define DEFAULT_OVS 1
#else
#define DEFAULT_OVS 2
#endif

#define MIN_ID 0xffffff0000000000LL
#define MAX_ID INT64_MAX

// scaling factor for bbox of URL output (-u)
#define BB_SCALE 0.01

#define JPG_QUALITY 80

#define FTYPE_PNG 0
#define FTYPE_JPG 1
#define FTYPE_PDF 2


typedef int (*tree_func_t)(osm_obj_t*, void*);

// indexes to object tree
enum {IDX_NODE, IDX_WAY, IDX_REL};
//enum {WHITE, YELLOW, BLACK, BLUE, MAGENTA, BROWN, TRANSPARENT, BGCOLOR, MAXCOLOR};
enum {LAT, LON};
enum {DRAW_SOLID, DRAW_DASHED, DRAW_DOTTED, DRAW_TRANSPARENT};

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
   //image_t *img;
#ifdef HAVE_CAIRO
   cairo_surface_t *img;
   double w, h;
   cairo_t *ctx;
#endif
};

struct cap_data
{
   osm_obj_t *o;
   struct diff_vec *dv;
   int n;
   int x, y;
   double angle;
   int offset;
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
#ifdef HAVE_CAIRO
   cairo_t *ctx;
#endif
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
   int directional;
   int collect_open;
   struct wlist *wl;
#ifdef HAVE_CAIRO
   cairo_t *ctx;
#endif
};

struct act_shape
{
   short pcount;
   double size;
   double angle;
   char *key;
};

struct grid
{
   double lat_ticks, lon_ticks;
   double lat_sticks, lon_sticks;
   double lat_g, lon_g;
   double g_margin, g_tw, g_stw;
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

struct file
{
   char *name;
   long size;
   int fd;
};


/* smrender.c */
int traverse(const bx_node_t*, int, int, tree_func_t, void*);
int print_onode(FILE *, const osm_obj_t*);
int col_freq(struct rdata *, int, int, int, int, double, int);
int cf_dist(struct rdata *, int, int, int, int, double, int, int);
double rot_pos(int, int, double, int *, int *);
double color_frequency(struct rdata *, int, int, int, int, int);
void mk_chart_coords(int, int, struct rdata*, double*, double*);
int poly_area(const osm_way_t*, struct coord *, double *);
struct rdata *get_rdata(void);
int save_osm(const char *, bx_node_t *, const struct bbox *, const char *);


/* smutil.c */
int bs_match_attr(const osm_obj_t*, const struct otag *, const struct stag*);
int bs_match(const bstring_t *, const bstring_t *, const struct specialTag *);
void set_util_rd(struct rdata*);
int put_object0(bx_node_t**, int64_t, void*, int);
void *get_object0(bx_node_t*, int64_t, int);
int coord_str(double, int, char*, int);
inline long col_cmp(int, int);
int func_name(char*, int, void*);
int strcnt(const char*, int);

/* smloadosm.c */
void osm_read_exit(void);
int read_osm_file(hpx_ctrl_t*, bx_node_t**, const struct filter*, struct dstats*);
hpx_ctrl_t *open_osm_source(const char*, int);

/* smcoast.c */
int is_closed_poly(const osm_way_t*);
void init_cat_poly(struct rdata*);

/* smrules.c */
void cairo_smr_init_main_image(const char*);
int cairo_smr_get_pixel(cairo_surface_t*, int, int);
#ifdef HAVE_CAIRO
void save_main_image(FILE*, int);
void *create_tile(void);
void delete_tile(void *);
void cut_tile(const struct bbox *, void *);
int save_image(const char *, void *, int);
void *cairo_smr_image_surface_from_bg(void);
#else
#define save_main_image(a, b)
#define create_tile() NULL
#define delete_tile(a)
#define cut_tile(a, b)
#define save_image(a, b, x) 0
#endif

/* smlog.c */
FILE *init_log(const char*, int);

/* smrparse.c */
int set_color(const char *, int);
int get_color(int);
int parse_color(const char *);
int parse_style(const char *s);
int parse_matchtype(bstring_t*, struct specialTag*);
int init_rules(osm_obj_t*, void*);
fparam_t **parse_fparam(char*);
void free_fparam(fparam_t **);

/* smkap.c */
int save_kap(FILE *, struct rdata *);
int gen_kap_header(FILE *, struct rdata *);

/* smfunc.c */
int ins_eqdist(osm_way_t *, double);
int dist_median(const osm_way_t *, double *);

/* smgrid.c */
void init_grid(struct grid *);
void auto_grid(const struct rdata *, struct grid *);
void grid(struct rdata *, const struct grid *);

/* smqr.c */
image_t *smqr_image(void);

/* smthread.c */
void sm_wait_threads(void);
int traverse_queue(const bx_node_t *, int , tree_func_t , void *);
int sm_is_threaded(const smrule_t *);

/* smtile.c */
int create_tiles(const char *, const struct rdata *, int , int );

#endif

