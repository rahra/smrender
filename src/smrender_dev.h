/* Copyright 2011-2025 Bernhard R. Fischer, 4096R/8E24F29D <bf@abenteuerland.at>
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

/*! \file smrender_dev.h
 * This file contains almost all internal declarations.
 *
 * \author Bernhard R. Fischer, <bf@abenteuerland.at>
 * \version 2025/01/16
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
#define POS_1 32
#define POS_NE (POS_N | POS_E)
#define POS_SE (POS_S | POS_E)
#define POS_SW (POS_S | POS_W)
#define POS_NW (POS_N | POS_W)
#define POS_N1 (POS_N | POS_1)
#define POS_E1 (POS_E | POS_1)
#define POS_S1 (POS_S | POS_1)
#define POS_W1 (POS_W | POS_1)
#define POS_DIR_MSK (POS_N | POS_S | POS_E | POS_W)

#define COORD_LAT 0
#define COORD_LON 1

#define MAJORAXIS 720.0
#define AUTOROT NAN

#define ESM_OK 0
#define ESM_ERROR -1
#define ESM_NOFILE -2
#define ESM_TIMEDIFF -3
#define ESM_TRUNCATED -4
#define ESM_NULLPTR -5
#define ESM_OUTDATED -6

//! macro to convert minutes to degrees
#define MIN2DEG(x) ((double) (x) / 60.0)
//! distance of grid lines in degrees (should be multple of G_TICKS)
#define G_GRID (10.0 / 60.0)
//! distance of axis ticks in degrees (should be multiple of G_STICKS)
#define G_TICKS (1.0 / 60.0)
//! distance of axis subticks in degrees
#define G_STICKS (G_TICKS / 4.0)
//! margin from paper edge to border of chart (mm)
#define G_MARGIN 15.0
//! width of ticks border (mm)
#define G_TW 5.0
//! width of subticks border (mm)
#define G_STW 2.5
//! line width of chart border (mm)
#define G_BW 0.1
#define G_FONT "/usr/share/fonts/truetype/ttf-liberation/LiberationSans-Regular.ttf"
#define G_FTSIZE 3
#define G_SFTSIZE 2
//! default macros for auto-sized caption limits
#define MIN_AUTO_SIZE 0.5
#define MAX_AUTO_SIZE 12.0
#define MIN_AREA_SIZE 8.0
#define AUTO_SCALE 0.2
#define DEFAULT_CAP_FONT "serif"
#define DEFAULT_CAP_SIZE 4.0
//! default curve factor
#define DIV_PART 0.2

#define ANGLE_DIFF 10

#define MAX_SHAPE_PCOUNT 2000

//! default oversampling factor
#ifdef HAVE_CAIRO
#define DEFAULT_OVS 1
#else
#define DEFAULT_OVS 2
#endif

#define MIN_ID 0xffffff0000000000LL
#define MAX_ID INT64_MAX

//! scaling factor for bbox of URL output (-u)
#define BB_SCALE 0.01

#define JPG_QUALITY 80

#define FTYPE_PNG 0
#define FTYPE_JPG 1
#define FTYPE_PDF 2
#define FTYPE_SVG 3

//! maximum number of dash definitions in a dash array
#define MAX_DASHLEN 4

//enum {WHITE, YELLOW, BLACK, BLUE, MAGENTA, BROWN, TRANSPARENT, BGCOLOR, MAXCOLOR};
enum {LAT, LON};
typedef enum {DRAW_SOLID, DRAW_DASHED, DRAW_DOTTED, DRAW_TRANSPARENT, DRAW_PIPE, DRAW_ROUNDDOT} draw_style_t;
enum {SHAPE_REGULAR, SHAPE_SECTORED, SHAPE_STARED};

#define DEFAULT_NINDENT 3
#define RI_CONDENSED (1 << 0)
#define RI_SHORT (1 << 1)
#define RI_VISIBLE (1 << 2)
typedef struct rinfo
{
   int version;
   FILE *f;
   const char *fname;
   int flags;
   int indent;
   int nindent;
} rinfo_t;

typedef struct keylist
{
   int count;
   char **key;
} keylist_t;

struct auto_rot
{
   double phase;     //!< phase of weighting function. 0 degress means east (0) and west (180) is most important
   int autocol;      //!< (deprecated) background color which is used for auto-rotation detection
   double weight;    //!< auto-rot weighting (0-1), 1 means everything equal
   int mkarea;       //!< if set to 1, OSM ways/nodes are generated according to the diffvec_t
};

struct auto_scale
{
   double max_auto_size;   //!< min font size [mm] for auto-size area captions
   double min_auto_size;   //!< max font size [mm] for auto-size area captions
   double min_area_size;   //!< minimum size [mm2] of area for auto-sized area captions
   double auto_scale;      //!< scaling factor
};
 
struct actImage
{
   double angle;
   struct auto_rot rot;
   double scale;        //!< scale image by this factor
   char *akey;          //!< angle is defined in a tag
   char *alignkey;      //!< alignment defined in a tag
   double trans;        //!< transparancy of image, 0.0 = opaque, 1.0 = absolute transparent
#ifdef HAVE_CAIRO
   cairo_surface_t *img;
   cairo_pattern_t *pat;
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

struct col_spec
{
   int col;          //!< color code
   char *key;        //!< name of color key (or NULL if not used)
};

struct drawStyle
{
   struct col_spec cs;
   double width;
   draw_style_t style;
   short used;
   int dashlen;
   double dash[MAX_DASHLEN];
};

struct actCaption
{
   short pos;              //!< position, or'd POS_x macros
   struct col_spec cs;     //!< caption color
   char *font;             //!< pointer to font filename
   char *key;              //!< pointer to caption string
   keylist_t klist;        //!< keylist for filter
   double size;            //!< font size in mm
   struct auto_scale scl;
   double angle;           //!< angle to rotate caption. 0 degress equals east, counterclockwise. NAN means auto-rotate
   char *akey;             //!< angle is defined in a tag
   char *halignkey;        //!< keys defining alignment for tag-dependent alignment
   char *valignkey;
   double xoff, yoff;      //!< x/y offset from origin
   int hide;               //!< if set to 1 do everything except showing the caption
   struct auto_rot rot;
   struct drawStyle fill;  //!< this defines if the background is filled
   double bgbox_scale;     //!< factor to scale the background box
   int fontbox;            //!< generate OSM data-based box
#ifdef HAVE_CAIRO
   cairo_t *ctx;
   cairo_surface_t *auto_sfc;
   cairo_t *auto_ctx;
#endif
};

struct actDraw
{
   struct drawStyle fill;
   struct drawStyle border;
   int directional;
   int collect_open;
   int curve;
   union
   {
      double curve_fact, wavy_length;
   };
   struct wlist *wl;
#ifdef HAVE_CAIRO
   cairo_t *ctx;
#endif
   struct actImage img;
};

struct act_shape
{
   short pcount;
   double size;
   double angle;
   double weight;
   double phase;
   char *key;
   double start, end;
   char *startkey, *endkey;
   int type;
   double r2;
};

struct grid
{
   double lat_ticks, lon_ticks;
   double lat_sticks, lon_sticks;
   double lat_g, lon_g;
   double g_margin, g_tw, g_stw;
   int copyright, cmdline;
   //! number of points per within each grid line (must be >= 2)
   int gpcnt;
   //! render chart border as polygon in transversal Mercator
   int polygon_window;
};

typedef struct ruler
{
   //! number of sections
   int rcnt;
   //! length of sections
   double rsec;
   //! units: 0 = km, 1 = nm
   int unit;
   //! position on paper
   double x, y;
} ruler_t;

struct file
{
   char *name;
   long size;
   int fd;
};

typedef struct renum
{
   int64_t id;
   bx_node_t *tree;
   int pass;
} renum_t;

/* smrender.c */
int print_onode(FILE *, const osm_obj_t*);
int col_freq(struct rdata *, int, int, int, int, double, int);
int cf_dist(struct rdata *, int, int, int, int, double, int, int);
double rot_pos(int, int, double, int *, int *);
double color_frequency(struct rdata *, int, int, int, int, int);
void mk_chart_coords(int, int, struct rdata*, double*, double*);
int poly_area(const osm_way_t*, struct coord *, double *);
struct rdata *get_rdata(void);
size_t save_osm(const char *, bx_node_t *, const struct bbox *, const char *);
void print_version(void);
void usage(const char *);


/* smcoast.c */
int is_closed_poly(const osm_way_t*);
void init_cat_poly(struct rdata*);

/* smrules.c */
void cairo_smr_init_main_image(const char*);
#ifdef HAVE_CAIRO
int cairo_smr_get_pixel(cairo_surface_t*, int, int);
void save_main_image(FILE*, int);
void *create_tile(void);
void delete_tile(void *);
void cut_tile(const struct bbox *, void *);
void clear_tile(void *);
int save_image(const char *, void *, int);
void *cairo_smr_image_surface_from_bg(cairo_format_t, cairo_antialias_t);
#else
#define save_main_image(a, b)
#define create_tile() NULL
#define delete_tile(a)
#define cut_tile(a, b)
#define save_image(a, b, x) 0
#endif

/* smlog.c */
FILE *init_log(const char*, int);
void set_log_time(int);

/* smrparse.c */
int set_color(const char *, int);
int get_color(int);
int parse_color(const char *);
void parse_col_spec(char *, struct col_spec *);
int parse_style(const char *s);
int parse_matchtag(struct otag *, struct stag *);
int init_rules(osm_obj_t*, void*);
fparam_t **parse_fparam(char*);
void free_fparam(fparam_t **);
int parse_alignment(const action_t *act);
int parse_alignment_str(const char *);
int parse_length(const char *, value_t *);
int parse_length_def(const char *, value_t *, unit_t );
int parse_length_mm_array(const char *, double *, int );
int parse_keylist(const char *, keylist_t *);
int parse_coord(const char *, double *);
int parse_coord2(const char *, double *, int );
void parse_auto_rot(const action_t *, double *, struct auto_rot *);
void parse_dash_style(const char *, struct drawStyle *);

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

/* smtile.c */
int create_tiles(const char *, const struct rdata *, int , int );

/* smjson.c */
int rules_info(const struct rdata *, rinfo_t *, const struct dstats *);
size_t save_json(const char *, bx_node_t *, int );

/* smindex.c */
int index_write(const char *, bx_node_t *, const void *, const struct dstats *);
ssize_t sm_write(int , const void *, size_t );
int index_read(const char *, const void *, struct dstats *);

#endif

