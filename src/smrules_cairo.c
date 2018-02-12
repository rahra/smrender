/* Copyright 2011-2018 Bernhard R. Fischer, 4096R/8E24F29D <bf@abenteuerland.at>
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

/*! \file smrules_cairo.c
 * This file contains all graphical rendering functions using libcairo.
 *
 *  @author Bernhard R. Fischer
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef HAVE_CAIRO

#include <string.h>
#include <syslog.h>
#include <errno.h>
#include <inttypes.h>
#include <ctype.h>
#include <wctype.h>
#ifdef WITH_THREADS
#include <pthread.h>
#endif
#include <cairo.h>
//#undef CAIRO_HAS_FT_FONT
//#undef CAIRO_HAS_FC_FONT
#ifdef CAIRO_HAS_FT_FONT
#include <cairo-ft.h>
#endif
#ifdef CAIRO_HAS_PDF_SURFACE
#include <cairo-pdf.h>
#endif
#ifdef CAIRO_HAS_SVG_SURFACE
#include <cairo-svg.h>
#endif
#ifdef HAVE_RSVG
#include <librsvg/rsvg.h>
#endif
#ifdef HAVE_GLIB
#include <glib.h>
#endif

// this format was defined in version 1.12
#ifndef CAIRO_FORMAT_RGB30
#define CAIRO_FORMAT_RGB30 5
#endif

#define mm2unit(x) mm2ptf(x)
#define THINLINE rdata_px_unit(1, U_PT)
#define mm2wu(x) ((x) == 0.0 ? THINLINE : mm2unit(x))

#include "smrender_dev.h"
#include "smcoast.h"
#include "rdata.h"
#include "bspline.h"
#ifdef HAVE_LIBJPEG
#include "cairo_jpg.h"
#endif

// define this if color difference shall be calculated in the 3d color space.
//#define COL_DIFF_3D
// define this if color difference shall be calculated by the YIQ brightness.
//#define COL_DIFF_BRGT
// define this if color difference shall be calculated by the luminosity.
#define COL_DIFF_LUM
#define COL_STRETCH_BW
#define COL_STRETCH_F 1.25

#define POS_OFFSET mm2unit(1.4)

#define COL_COMP(x, y) ((x) >> (y) & 0xff)
#define COL_COMPD(x, y) ((double) COL_COMP(x, y) / 255.0)
#define REDD(x) COL_COMPD(x, 16)
#define GREEND(x) COL_COMPD(x, 8)
#define BLUED(x) COL_COMPD(x, 0)
#define ALPHAD(x) (1.0 - COL_COMPD(x & 0x7f000000, 23))
#define COL_D(x) ((int) round((x) * 255))
#define COL_DS(x, y) (COL_D(x) << (y))
#define COL_RED(x) COL_DS(x, 16)
#define COL_GREEN(x) COL_DS(x, 8)
#define COL_BLUE(x) COL_D(x)

#define M_2PI (2.0 * M_PI)
#define PT2PX_SCALE (rdata_dpi() / 72.0)
#define PT2PX(x) ((x) * PT2PX_SCALE)
#define PX2PT_SCALE (72.0 / rdata_dpi())
#define DP_LIMIT 0.95
#define TILE_SIZE 256
#define TRANSPIX 0x7fffffff
#define sqr(x) pow(x, 2)

#define CURVE 1
#define WAVY 2
#define WAVY_LENGTH 0.0015
#define PIPE_DOT_SCALE 2.5

#define MAJORAXIS 720.0
#define AUTOROT NAN

#define RENDER_IMMEDIATE 0
#define CREATE_PATH 1
#define PUSH_GROUP
#define AUTOSFC 1

#define CAIRO_SMR_STATS
#ifdef CAIRO_SMR_STATS
enum {CSS_LINE, CSS_CURVE, CSS_STROKE, CSS_FILL, CSS_PAINT, CSS_PUSH, CSS_POP, CSS_MAX};
static int css_stats_[CSS_MAX];
#define CSS_INC(x) (css_stats_[x]++)
#else
#define CSS_INC(x)
#endif

typedef struct diffvec
{
   double dv_diff, dv_var;
   int dv_x, dv_y;
   double dv_angle, dv_quant;
   int dv_index;
} diffvec_t;


typedef struct diffpeak
{
   double dp_start;
   double dp_end;
} diffpeak_t;


typedef struct cartesian
{
   double x, y, z;
} cartesian_t;


static cairo_surface_t *sfc_;
static cairo_rectangle_t ext_;


void __attribute__((constructor)) cairo_smr_init(void)
{
#ifdef HAVE_GLIB
   log_debug("using libcairo %s, using GLIB %d.%d", cairo_version_string(), glib_major_version, glib_minor_version);
#if ! GLIB_CHECK_VERSION(2,36,0)
   log_debug("calling g_type_init()");
   g_type_init();
#endif
#endif
#ifdef PUSH_GROUP
   log_debug("using push()/pop()");
#else
   log_debug("push()/pop() disabled (thus, rendering is slower)");
#endif
}


static inline int cairo_smr_bpp(cairo_format_t fmt)
{
   switch (fmt)
   {
      case CAIRO_FORMAT_ARGB32:
      case CAIRO_FORMAT_RGB24:
      case CAIRO_FORMAT_RGB30:
         return 4;
      
      case CAIRO_FORMAT_RGB16_565:
         return 2;

      // FIXME: not implemented yet case CAIRO_FORMAT_A1:

      case CAIRO_FORMAT_A8:
      default:
         return 1;
 
   }
}


static void cairo_smr_log_surface_data(cairo_surface_t *sfc)
{
   log_debug("format = %d, bpp = %d, stride = %d",
         cairo_image_surface_get_format(sfc), cairo_smr_bpp(cairo_image_surface_get_format(sfc)),
         cairo_image_surface_get_stride(sfc));
}


static cairo_status_t cairo_smr_log_surface_status(cairo_surface_t *sfc)
{
   cairo_status_t e;

   if ((e = cairo_surface_status(sfc)) != CAIRO_STATUS_SUCCESS)
      log_msg(LOG_ERR, "failed to create surface: %s",
            cairo_status_to_string(e));

   return e;
}


static cairo_status_t cairo_smr_log_status(cairo_t *ctx)
{
   cairo_status_t e;

   if ((e = cairo_status(ctx)) != CAIRO_STATUS_SUCCESS)
      log_msg(LOG_ERR, "error in libcairo: %s", cairo_status_to_string(e));

   return e;
}


static void cairo_smr_set_source_color(cairo_t *ctx, int col)
{
   cairo_set_source_rgba(ctx, REDD(col), GREEND(col), BLUED(col), ALPHAD(col));
}


static cairo_surface_t *cairo_smr_surface(void)
{
   cairo_surface_t *sfc;
   cairo_status_t e;

//#define RECORD_TO_PDF
#ifndef RECORD_TO_PDF
   sfc = cairo_recording_surface_create(CAIRO_CONTENT_COLOR_ALPHA, &ext_);
#else
   sfc = cairo_pdf_surface_create("tmp.pdf", ext_.width, ext_.height);
#endif
   if ((e = cairo_surface_status(sfc)) != CAIRO_STATUS_SUCCESS)
   {
      log_msg(LOG_ERR, "failed to create cairo surface: %s", cairo_status_to_string(e));
      return NULL;
   }
   cairo_surface_set_fallback_resolution(sfc, rdata_dpi(), rdata_dpi());
   return sfc;
}


void __attribute__((destructor)) cairo_smr_fini(void)
{
   cairo_surface_destroy(sfc_);
#ifdef CAIRO_SMR_STATS
   for (int i = 0; i < CSS_MAX; i++)
      log_debug("css_stats_[%d] = %d", i, css_stats_[i]);
#endif
}


void cairo_smr_init_main_image(const char *bg)
{
   cairo_t *ctx;

   ext_.x = 0;
   ext_.y = 0;
   ext_.width = rdata_width(U_PT);
   ext_.height = rdata_height(U_PT);

   if ((sfc_ = cairo_smr_surface()) == NULL)
      exit(EXIT_FAILURE);

   if (bg != NULL)
      set_color("bgcolor", parse_color(bg));

   ctx = cairo_create(sfc_);
   cairo_smr_set_source_color(ctx, parse_color("bgcolor"));
   cairo_paint(ctx);
   CSS_INC(CSS_PAINT);
   cairo_destroy(ctx);

   log_msg(LOG_DEBUG, "background color is set to 0x%08x", parse_color("bgcolor"));
}


static cairo_status_t cairo_smr_write_func(void *closure, const unsigned char *data, unsigned int length)
{
   return fwrite(data, length, 1, closure) == 1 ? CAIRO_STATUS_SUCCESS : CAIRO_STATUS_WRITE_ERROR;
}


void *cairo_smr_image_surface_from_bg(cairo_format_t fmt, cairo_antialias_t alias)
{
   cairo_surface_t *sfc;
   cairo_t *dst;

   sfc = cairo_image_surface_create(fmt, round(rdata_width(U_PX)), round(rdata_height(U_PX)));
   cairo_smr_log_surface_status(sfc);
   dst = cairo_create(sfc);
   cairo_smr_log_status(dst);
   cairo_scale(dst, (double) rdata_dpi() / 72, (double) rdata_dpi() / 72);
   cairo_set_source_surface(dst, sfc_, 0, 0);
   cairo_set_antialias(dst, alias);
   cairo_paint(dst);
   CSS_INC(CSS_PAINT);
   cairo_destroy(dst);
   cairo_smr_log_surface_data(sfc);
   return sfc;
}


void *cairo_smr_recording_surface_from_bg(void)
{
   cairo_surface_t *sfc;
   cairo_t *ctx;

   sfc = cairo_recording_surface_create(CAIRO_CONTENT_COLOR_ALPHA, &ext_);
   cairo_smr_log_surface_status(sfc);
   ctx = cairo_create(sfc);
   cairo_smr_log_status(ctx);
   cairo_set_source_surface(ctx, sfc_, 0, 0);
   cairo_paint(ctx);
   cairo_destroy(ctx);

   return sfc;
}


static void cairo_smr_page_rotate(cairo_t *ctx)
{
   struct rdata *rd = get_rdata();

   if (rd->rot == 0)
      return;

   log_debug("rotating output by %.1f°", RAD2DEG(rd->rot));
   cairo_rotate(ctx, rd->rot);
}


void save_main_image(FILE *f, int ftype)
{
   cairo_surface_t *sfc;
   cairo_status_t e;
   cairo_t *dst;

   log_msg(LOG_NOTICE, "saving image (ftype = %d)", ftype);

   switch (ftype)
   {
      case FTYPE_PNG:
         sfc = cairo_smr_image_surface_from_bg(CAIRO_FORMAT_ARGB32, CAIRO_ANTIALIAS_DEFAULT);
         if ((e = cairo_surface_write_to_png_stream(sfc, cairo_smr_write_func, f)) != CAIRO_STATUS_SUCCESS)
            log_msg(LOG_ERR, "failed to save png image: %s", cairo_status_to_string(e));
         cairo_surface_destroy(sfc);
         return;

      case FTYPE_PDF:
#ifdef CAIRO_HAS_PDF_SURFACE
         log_debug("PDF: width = %.2f pt (%.2f mm), height = %.2f pt (%.2f mm)",
               rdata_page_width(U_PT), rdata_page_width(U_MM), rdata_page_height(U_PT), rdata_page_height(U_MM));
         sfc = cairo_pdf_surface_create_for_stream(cairo_smr_write_func, f, rdata_page_width(U_PT), rdata_page_height(U_PT));
         //cairo_pdf_surface_restrict_to_version(sfc, CAIRO_PDF_VERSION_1_4);
         dst = cairo_create(sfc);
         cairo_smr_log_status(dst);
         cairo_translate(dst, rdata_page_width(U_PT) / 2, rdata_page_height(U_PT) / 2);
         cairo_smr_page_rotate(dst);
         cairo_set_source_surface(dst, sfc_, rdata_width(U_PT) / -2, rdata_height(U_PT) / -2);
         cairo_paint(dst);
         cairo_show_page(dst);
         cairo_destroy(dst);
         cairo_surface_destroy(sfc);
#else
         log_msg(LOG_NOTICE, "cannot create PDF, cairo was compiled without PDF support");
#endif
         return;
#ifdef CAIRO_HAS_SVG_SURFACE
      case FTYPE_SVG:
         log_debug("width = %.2f pt, height = %.2f pt", rdata_width(U_PT), rdata_height(U_PT));
         sfc = cairo_svg_surface_create_for_stream(cairo_smr_write_func, f, rdata_page_width(U_PT), rdata_page_height(U_PT));
         cairo_svg_surface_restrict_to_version (sfc, CAIRO_SVG_VERSION_1_2);
         dst = cairo_create(sfc);
         cairo_smr_log_status(dst);
         cairo_translate(dst, rdata_page_width(U_PT) / 2, rdata_page_height(U_PT)/2);
         cairo_smr_page_rotate(dst);
         cairo_set_source_surface(dst, sfc_, -rdata_width(U_PT) / 2, -rdata_height(U_PT) / 2);
         cairo_paint(dst);
         cairo_destroy(dst);
         cairo_surface_destroy(sfc);
#else
         log_msg(LOG_NOTICE, "cannot create SVG, cairo was compiled without SVG support");
#endif
         return;
   }
 
   log_msg(LOG_WARN, "cannot save image, file type %d not implemented yet", ftype);
}


int save_image(const char *s, void *img, int ftype)
{
   switch (ftype)
   {
      case FTYPE_PNG:
         return cairo_surface_write_to_png(img, s) == CAIRO_STATUS_SUCCESS ? 0 : -1;
   }

   // FIXME
   log_msg(LOG_ERR, "other file types than png not implemented yet");
   return -1;
}


void *create_tile(void)
{
   cairo_surface_t *sfc;

   sfc = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, TILE_SIZE, TILE_SIZE);
   if (cairo_surface_status(sfc) != CAIRO_STATUS_SUCCESS)
   {
      log_msg(LOG_ERR, "failed to create tile surface: %s",
            cairo_status_to_string(cairo_surface_status(sfc)));
      return NULL;
   }
   return sfc;
}


void delete_tile(void *img)
{
   cairo_surface_destroy(img);
}


void cut_tile(const struct bbox *bb, void *img)
{
   double x, y, w, h;
   cairo_t *ctx;

   geo2pt(bb->ll.lon, bb->ru.lat, &x, &y);
   geo2pt(bb->ru.lon, bb->ll.lat, &w, &h);

   ctx = cairo_create(img);
   log_debug("cutting %.1f/%.1f - %.1f/%.1f", x, y, w, h);
   cairo_scale(ctx, TILE_SIZE / (w - x), TILE_SIZE / (h - y));
   cairo_set_source_surface(ctx, sfc_, -x, -y);
   cairo_paint(ctx);
   cairo_destroy(ctx);
}


void clear_tile(void *img)
{
   cairo_t *ctx;
   ctx = cairo_create(img);
   cairo_smr_set_source_color(ctx, parse_color("bgcolor"));
   cairo_set_operator(ctx, CAIRO_OPERATOR_CLEAR);
   cairo_paint(ctx);
   cairo_destroy(ctx);
}


/*! Return the memory address of a Pixel.
 *  @param x X position.
 *  @param y Y position.
 *  @param s Stride, i.e. the number of bytes per row.
 *  @param bpp Number of bytes per pixel.
 *  @return Memory offset to the base pointer of the pixel matrix.
 */
static inline int cairo_smr_pixel_pos(int x, int y, int s, int bpp)
{
   return x * bpp + y * s;
}


static uint32_t cairo_smr_get_raw_pixel(unsigned char *data, cairo_format_t fmt)
{
   uint32_t rc;

   switch (fmt)
   {
      case CAIRO_FORMAT_ARGB32:
      case CAIRO_FORMAT_RGB24:
         return *((uint32_t*) data);

      case CAIRO_FORMAT_RGB30:
         rc = *((uint32_t*) data);
         return ((rc >> 2) & 0xff) | ((rc >> 4) & 0xff00) | ((rc >> 6) & 0xff0000);
      
      case CAIRO_FORMAT_RGB16_565:
         rc = *((uint16_t*) data);
         return ((rc << 3) & 0xff) | ((rc << 5) & 0xfc00) | ((rc << 8) & 0xf80000);

      // FIXME: not implemented yet case CAIRO_FORMAT_A1:

      case CAIRO_FORMAT_A8:
         rc = *data;
         return rc | ((rc << 8) & 0xff00) | ((rc << 16) & 0xff0000);

      default:
         return 0;
   }
}


int cairo_smr_get_pixel(cairo_surface_t *sfc, int x, int y)
{
   unsigned char *data;

   // FIXME: flush may be done outside get_pixel()
   cairo_surface_flush(sfc);
   if ((data = cairo_image_surface_get_data(sfc)) == NULL)
      return 0;

   return cairo_smr_get_raw_pixel(data + cairo_smr_pixel_pos(x, y,
                  cairo_image_surface_get_stride(sfc), cairo_smr_bpp(cairo_image_surface_get_format(sfc))),
         cairo_image_surface_get_format(sfc));
}

//FIXME: This should be moved to smrparse.c
static void parse_auto_rot(const action_t *act, double *angle, struct auto_rot *rot)
{
   char *val;

   if ((val = get_param("angle", angle, act)) == NULL)
      return;

   if (!strcasecmp("auto", val))
   {
      *angle = AUTOROT;
      if (get_param("auto-color", NULL, act) != NULL)
         log_msg(LOG_NOTICE, "parameter 'auto-color' deprecated");

      if ((val = get_param("weight", &rot->weight, act)) == NULL)
         rot->weight = 1.0;

      // boundary check for 'weight' parameter
      if (rot->weight > 1.0)
      {
         rot->weight = 1.0;
         log_msg(LOG_NOTICE, "weight limited to %.1f", rot->weight);
      }
      else if (rot->weight < -1.0)
      {
         rot->weight = -1.0;
         log_msg(LOG_NOTICE, "weight limited to %.1f", rot->weight);
      }

      (void) get_param("phase", &rot->phase, act);
      rot->mkarea = get_param_bool("mkarea", act);
   }
   else if (!strcasecmp("majoraxis", val))
   {
      *angle = MAJORAXIS;
   }
   else
   {
      *angle = fmod(*angle, 360.0);
   }
}

 
static void parse_dash_style(const char *s, struct drawStyle *ds)
{
   if (s != NULL)
      ds->dashlen = parse_length_mm_array(s, ds->dash, sizeof(ds->dash) / sizeof(*ds->dash));
   if (s == NULL || ds->dashlen <= 0)
      switch (ds->style)
      {
         case DRAW_DASHED:
         case DRAW_PIPE:
            ds->dash[0] = 7;
            ds->dash[1] = 3;
            ds->dashlen = 2;
            break;
         case DRAW_DOTTED:
            ds->dash[0] = 1;
            ds->dashlen = 1;
            break;
         case DRAW_ROUNDDOT:
            ds->dash[0] = 0;
            ds->dash[1] = 2;
            ds->dashlen = 2;
            break;
         default:
            ds->dashlen = 0;
      }
}


int act_draw_ini(smrule_t *r)
{
   struct actDraw *d;
   value_t v;
   char *s;

   // just to be on the safe side
   if ((r->oo->type != OSM_WAY) && (r->oo->type != OSM_REL))
   {
      log_msg(LOG_WARN, "'draw' may be applied to ways or relations only");
      return 1;
   }

   if ((d = malloc(sizeof(*d))) == NULL)
   {
      log_msg(LOG_ERR, "cannot malloc: %s", strerror(errno));
      return -1;
   }

   memset(d, 0, sizeof(*d));
   r->data = d;

   // parse fill settings
   if ((s = get_param("color", NULL, r->act)) != NULL)
   {
      d->fill.col = parse_color(s);
      d->fill.used = 1;
   }
   if (get_param("width", &d->fill.width, r->act) == NULL)
      d->fill.width = 0;
   d->fill.style = parse_style(get_param("style", NULL, r->act));

   // parse border settings
   if ((s = get_param("bcolor", NULL, r->act)) != NULL)
   {
      d->border.col = parse_color(s);
      d->border.used = 1;
   }
   if (get_param("bwidth", &d->border.width, r->act) == NULL)
      d->border.width = 0;
   d->border.style = parse_style(get_param("bstyle", NULL, r->act));

   if (get_param_bool("curve", r->act))
   {
      d->curve = CURVE;
      if (get_param("curve_factor", &d->curve_fact, r->act) == NULL)
         d->curve_fact = DIV_PART;
   }

   if (get_param_bool("wavy", r->act))
   {
      d->curve = WAVY;
      if ((s = get_param("wavy_length", &d->curve_fact, r->act)) != NULL)
      {
         parse_length_def(s, &v, U_MM);
         d->wavy_length = rdata_unit(&v, U_DEG);
      }
      else
         d->wavy_length = WAVY_LENGTH;
   }

   parse_dash_style(get_param("dash", NULL, r->act), &d->fill);
   parse_dash_style(get_param("bdash", NULL, r->act), &d->border);

   // honor direction of ways
   d->directional = get_param_bool("directional", r->act);
   d->collect_open = !get_param_bool("ignore_open", r->act);

   d->wl = init_wlist();

   d->ctx = cairo_create(sfc_);
   if (cairo_smr_log_status(d->ctx) != CAIRO_STATUS_SUCCESS)
   {
      free(d);
      r->data = NULL;
      return -1;
   }
#ifdef PUSH_GROUP
   cairo_push_group(d->ctx);
   CSS_INC(CSS_PUSH);
#endif

   sm_threaded(r);

   //log_msg(LOG_DEBUG, "directional = %d, ignore_open = %d", d->directional, !d->collect_open);
   log_msg(LOG_DEBUG, "{%08x, %.1f, %d, %d, %d, {%.1f, %.1f}}, {%08x, %.1f, %d, %d, %d, {%.1f, %.1f}}, %d, %d, %p",
        d->fill.col, d->fill.width, d->fill.style, d->fill.used, d->fill.dashlen, d->fill.dash[0], d->fill.dash[1],
        d->border.col, d->border.width, d->border.style, d->border.used, d->border.dashlen, d->border.dash[0], d->border.dash[1],
        d->directional, d->collect_open, d->wl);

   return 0;
}


static int cairo_smr_poly_curve(const osm_way_t *w, cairo_t *ctx, double f)
{
   osm_node_t *n;
   int i, cnt, start;
   line_t g, l;
   point_t c1, c2, *pt;

   cnt = w->ref_cnt;
   start = !is_closed_poly(w);
   if (!start)
      cnt--;

   log_debug("w->ref_cnt = %d, cnt = %d, start = %d", w->ref_cnt, cnt, start);
   if ((pt = malloc(cnt * sizeof(*pt))) == NULL)
   {
      log_errno(LOG_ERR, "malloc() failed in cairo_smr_poly_curve()");
      return -1;
   }

   for (i = 0; i < cnt; i++)
   {
      if ((n = get_object(OSM_NODE, w->ref[i])) == NULL)
      {
         log_msg(LOG_EMERG, "node %ld of way %ld at pos %d does not exist", (long) w->ref[i], (long) w->obj.id, i);
         free(pt);
         return -1;
      }
      geo2pt(n->lon, n->lat, &pt[i].x, &pt[i].y);
   }

   cairo_move_to(ctx, pt[(start - 1 + cnt) % cnt].x, pt[(start - 1 + cnt) % cnt].y);
   for (i = start; i < cnt; i++)
   {
      g.A = pt[(i + cnt - 2) % cnt];
      g.B = pt[(i + cnt - 1) % cnt];
      l.A = pt[(i + cnt + 0) % cnt];
      l.B = pt[(i + cnt + 1) % cnt];
    
      control_points(&g, &l, &c1, &c2, f);
      if (start)
      {
         if (i == 1) c1 = g.B;
         if (i == cnt - 1) c2 = l.A;
      }

      cairo_curve_to(ctx, c1.x, c1.y, c2.x, c2.y, pt[i].x, pt[i].y);
      CSS_INC(CSS_CURVE);
      //log_debug("%f %f %f %f %f %f", c1.x, c1.y, c2.x, c2.y, pt[i].x, pt[i].y);
   }

   free(pt);
   return 0;
}


static void wavy(const struct coord *src, const struct coord *dst, cairo_t *ctx)
{
   struct pcoord pc;
   double lat, lon, x1, x2, x3, y1, y2, y3;

   // end point
   geo2pt(dst->lon, dst->lat, &x3, &y3);

   coord_diffp(src, dst, &pc);

   pc.bearing -= 45.0;
   lat = src->lat + pc.dist * M_SQRT1_2 * cos(DEG2RAD(pc.bearing));
   lon = src->lon + pc.dist * M_SQRT1_2 * sin(DEG2RAD(pc.bearing)) / cos(DEG2RAD((lat + src->lat) / 2));
   geo2pt(lon, lat, &x1, &y1);

   pc.bearing += 90.0;
   lat = src->lat + pc.dist * M_SQRT1_2 * cos(DEG2RAD(pc.bearing));
   lon = src->lon + pc.dist * M_SQRT1_2 * sin(DEG2RAD(pc.bearing)) / cos(DEG2RAD((lat + src->lat) / 2));
   geo2pt(lon, lat, &x2, &y2);

   cairo_curve_to(ctx, x1, y1, x2, y2, x3, y3);
   CSS_INC(CSS_CURVE);
}


static int cairo_smr_wavy(const osm_way_t *w, cairo_t *ctx, double dist)
{
   struct pcoord pc;
   struct coord sc, dc, ic;
   osm_node_t *n;
   double d, x, y;
   int i, j;

   if (w->ref == NULL)
   {
      log_msg(LOG_EMERG, "w(%"PRId64")->ref == NULL...this should never happen!", w->obj.id);
      return -1;
   }

   if ((n = get_object(OSM_NODE, w->ref[0])) == NULL)
   {
      log_msg(LOG_ERR, "node %"PRId64" of way %"PRId64" das not exit", w->ref[0], w->obj.id);
      return -1;
   }

   sc.lat = n->lat;
   sc.lon = n->lon;
   geo2pt(n->lon, n->lat, &x, &y);
   cairo_move_to(ctx, x, y);

   for (i = 1, pc.dist = 0, j = 0;; j++)
   {
      if (pc.dist <= 0)
      {
         if (i >= w->ref_cnt)
            break;

         if ((n = get_object(OSM_NODE, w->ref[i])) == NULL)
         {
            log_msg(LOG_ERR, "node %"PRId64" of way %"PRId64" das not exit", w->ref[i], w->obj.id);
            return -1;
         }
         i++;

         dc.lat = n->lat;
         dc.lon = n->lon;

         d = pc.dist;
         coord_diffp(&sc, &dc, &pc);
         pc.dist += d;
      }

      if (pc.dist > dist)
      {
         ic.lat = sc.lat + dist * cos(DEG2RAD(pc.bearing));
         ic.lon = sc.lon + dist * sin(DEG2RAD(pc.bearing)) / cos(DEG2RAD((ic.lat + sc.lat) / 2));

         wavy(&sc, &ic, ctx);
         sc = ic;
      }
      pc.dist -= dist;
   }

   log_debug("%d virtual points inserted", j ); 



   return 0;
}


/*! Create a cairo path from a way.
 */
static void cairo_smr_poly_line(const osm_way_t *w, cairo_t *ctx)
{
   osm_node_t *n;
   double x, y;
   int i;

   for (i = 0; i < w->ref_cnt; i++)
   {
      if ((n = get_object(OSM_NODE, w->ref[i])) == NULL)
      {
         log_msg(LOG_WARN, "node %ld of way %ld at pos %d does not exist", (long) w->ref[i], (long) w->obj.id, i);
         continue;
      }

      geo2pt(n->lon, n->lat, &x, &y);
      cairo_line_to(ctx, x, y);
      CSS_INC(CSS_LINE);
   }
}


/*! Calculate the linewidth.
 *  @param d Pointer to struct actDraw.
 *  @param border 0 if fill width for open polygons needed or 1 for border width.
 *  @param closed 0 if open polygon or 1 if closed.
 *
 * Possible combinations of fill widths
 *                  | open fill  | open border | closed fill | closed border
 *  b_used,  f_used | fw         | 2bw+fw      | -           | 2bw
 *  b_used, !f_used | -          |  bw 1)      | -           |  bw
 * !b_used,  f_used | fw         | -           | -           | -
 * !b_used, !f_used | -          | -           | -           | -
 *
 * remark 1) this could also be 2bw.
 */
static double cairo_smr_border_width(const struct actDraw *d, int closed)
{
   if (!d->fill.used)
      return mm2wu(d->border.width);

   if (!closed)
      return mm2wu(2 * d->border.width) + mm2wu(d->fill.width);

   return mm2wu(2 * d->border.width);
}


static double cairo_smr_fill_width(const struct actDraw *d)
{
   return mm2wu(d->fill.width);
}


static void cairo_smr_dash(cairo_t *ctx, int style, double bwidth, const double *ds, int len)
{
   double dash[MAX_DASHLEN], l;
   int n;

   for (n = 0, l = 0; n < len && n < MAX_DASHLEN; n++)
   {
      dash[n] = mm2wu(bwidth) * ds[n];
      l += dash[n];
   }

   switch (style)
   {
      case DRAW_ROUNDDOT:
         cairo_set_line_cap(ctx, CAIRO_LINE_CAP_ROUND);
         break;

      case DRAW_PIPE:
         cairo_set_line_cap(ctx, CAIRO_LINE_CAP_ROUND);
         dash[0] = 0;
         dash[1] = l;
         n = 2;
         break;
   }
   cairo_set_dash(ctx, dash, n, 0);
}


static inline int cairo_smr_poly(cairo_t *ctx, const struct actDraw *d, const osm_way_t *w)
{
   cairo_new_sub_path(ctx);

   if (d->curve == CURVE)
      return cairo_smr_poly_curve(w, ctx, d->curve_fact);
   if (d->curve == WAVY)
      return cairo_smr_wavy(w, ctx, d->wavy_length);

   cairo_smr_poly_line(w, ctx);
   return 0;
}

 
/*! Render the way properly to the cairo context (version 2, 2015/0207).
 * The border is always stroked immediately, independently if it is an open or
 * closed polygon and fill is carried out immediately on open polygons. On
 * closed polygons the fill operation is carried out only if cw = 0.
 * If cw != 0 it does not fill but just creates a sub-path within the cairo
 * context. Thus, fill must be called after, explicitly. Please note that it is
 * not allowed to mix calls which stroke/fill and those who don't because
 * stroke/fill will always clear all previous paths.
 * @param ctx Pointer to the cairo context.
 * @param d Pointer to the drawing parameters of Smrender.
 * @param w Pointer to the OSM way.
 * @param cw If 0 stroke/fill immediately, otherwise just create sub-path but
 * do not stroke/fill.
 */
static void render_poly_line(cairo_t *ctx, const struct actDraw *d, const osm_way_t *w, int cw)
{
   // safety check
   if (w == NULL) { log_msg(LOG_ERR, "NULL pointer to way"); return; }

   if (d->border.used)
   {
      cairo_smr_set_source_color(ctx, d->border.col);
      // The pipe is a special case: it is a combination of a dashed and a
      // dotted line, the dots are place at the beginning of each dash.
      cairo_set_line_width(ctx, cairo_smr_border_width(d, is_closed_poly(w)));
      cairo_smr_dash(ctx, d->border.style == DRAW_PIPE ? DRAW_DASHED : d->border.style, d->border.width, d->border.dash, d->border.dashlen);
      cairo_smr_poly(ctx, d, w);
      cairo_stroke(ctx);
      CSS_INC(CSS_STROKE);

      if (d->border.style == DRAW_PIPE)
      {
         cairo_set_line_width(ctx, cairo_get_line_width(ctx) * PIPE_DOT_SCALE);
         cairo_smr_dash(ctx, DRAW_PIPE, d->border.width, d->border.dash, d->border.dashlen);
         cairo_smr_poly(ctx, d, w);
         cairo_stroke(ctx);
         CSS_INC(CSS_STROKE);
      }
   }

   if (d->fill.used)
   {
      cairo_smr_poly(ctx, d, w);
      cairo_smr_set_source_color(ctx, d->fill.col);
      if (!is_closed_poly(w))
      {
         cairo_set_line_width(ctx, cairo_smr_fill_width(d));
         cairo_smr_dash(ctx, d->fill.style, d->border.width, d->fill.dash, d->fill.dashlen);
         cairo_stroke(ctx);
         CSS_INC(CSS_STROKE);
      }
      else if (!cw)
      {
         cairo_fill(ctx);
         CSS_INC(CSS_FILL);
      }
   }
}
 

int act_draw_main(smrule_t *r, osm_obj_t *o)
{
#ifdef WITH_THREADS
   static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
#endif
   struct actDraw *d = r->data;
   osm_way_t *w;
   int i, e;

   if (o->type == OSM_WAY)
   {
      if (!is_closed_poly((osm_way_t*) o))
      {
         if (!d->collect_open)
            return 0;

         render_poly_line(d->ctx, d, (osm_way_t*) o, RENDER_IMMEDIATE);
         return 0;
      }

      if (!d->directional)
      {
         render_poly_line(d->ctx, d, (osm_way_t*) o, RENDER_IMMEDIATE);
         return 0;
      }

#ifdef WITH_THREADS
      pthread_mutex_lock(&mutex);
#endif
      (void) gather_poly0((osm_way_t*) o, &d->wl);
#ifdef WITH_THREADS
      pthread_mutex_unlock(&mutex);
#endif
      return 0;
   }
   else if (o->type == OSM_REL)
   {
      for (i = 0; i < ((osm_rel_t*) o)->mem_cnt; i++)
      {
         if (((osm_rel_t*) o)->mem[i].type != OSM_WAY)
            continue;
         if ((w = get_object(OSM_WAY, ((osm_rel_t*) o)->mem[i].id)) == NULL)
            //FIXME: error message may be output here
            continue;
         if ((e = act_draw_main(r, (osm_obj_t*) w)) < 0)
            return e;
         if (e)
            log_msg(LOG_WARN, "draw(way from relation) returned %d", e);
      }
      return 0;
   }

   log_msg(LOG_WARN, "draw() may not be applied to object type %d", o->type);
   return 1;
}


int act_draw_fini(smrule_t *r)
{
   struct actDraw *d = r->data;
   int i;

#ifdef PUSH_GROUP
   cairo_pop_group_to_source(d->ctx);
   CSS_INC(CSS_POP);
   cairo_paint(d->ctx);
   CSS_INC(CSS_PAINT);
#endif

   if (d->directional)
   {
      log_debug("rendering directional polygons (ref_cnt = %d)", d->wl->ref_cnt);
      for (i = 0; i < d->wl->ref_cnt; i++)
      {
         if (is_closed_poly(d->wl->ref[i].w))
         {
            poly_area(d->wl->ref[i].w, NULL, &d->wl->ref[i].area);
            if (d->wl->ref[i].area < 0)
            {
               d->wl->ref[i].area = fabs(d->wl->ref[i].area);
               d->wl->ref[i].cw = d->directional;
            }
         }
      }
      qsort(d->wl->ref, d->wl->ref_cnt, sizeof(struct poly), (int(*)(const void *, const void *)) compare_poly_area);

      cairo_push_group(d->ctx);
      CSS_INC(CSS_PUSH);
      // check if largest polygon is clockwise
      if (d->wl->ref_cnt && d->wl->ref[0].cw)
      {
         // ...and render larger (page size) filled polygon
         log_debug("inserting artifical background");
         render_poly_line(d->ctx, d, page_way(), CREATE_PATH);
      }
      for (i = 0; i < d->wl->ref_cnt; i++)
      {
         log_debug("id = %"PRId64", cw = %d, area = %f", d->wl->ref[i].w->obj.id, d->wl->ref[i].cw, d->wl->ref[i].area);
         render_poly_line(d->ctx, d, d->wl->ref[i].w, CREATE_PATH);
      }
      cairo_smr_set_source_color(d->ctx, d->fill.col);
      cairo_fill(d->ctx);
      cairo_pop_group_to_source(d->ctx);
      CSS_INC(CSS_POP);
      cairo_paint(d->ctx);
      CSS_INC(CSS_PAINT);
   }

   cairo_destroy(d->ctx);
   free(d);
   r->data = NULL;

   return 0;
}


static int farthest_node(const struct coord *c, const osm_way_t *w, struct pcoord *pc)
{
   struct pcoord pct;
   struct coord cd;
   osm_node_t *n;
   int ref = -1;

   memset(pc, 0, sizeof(*pc));

   for (int i = 0; i < w->ref_cnt; i++)
   {
      if ((n = get_object(OSM_NODE, w->ref[i])) == NULL)
      {
         log_msg(LOG_EMERG, "node %"PRId64" not found", w->ref[i]);
         continue;
      }

      cd.lat = n->lat;
      cd.lon = n->lon;
      pct = coord_diff(c, &cd);

      if (pct.dist > pc->dist)
      {
         *pc = pct;
         ref = i;
      }
   }

   return ref;
}


static int area_axis(const osm_way_t *w, double *a)
{
   struct coord c;
   struct pcoord pc, pc_final;
   osm_node_t *n;
   int fpair[2];
   int nref;

   // safety check
   if (w->ref_cnt < 2)
   {
      log_msg(LOG_EMERG, "way %"PRId64" has ill number of nodes: %d", w->obj.id, w->ref_cnt);
      return -1;
   }

   memset(&pc_final, 0, sizeof(pc_final));
   memset(fpair, 0, sizeof(fpair));

   for (;;)
   {
      if ((n = get_object(OSM_NODE, w->ref[fpair[1]])) == NULL)
      {
         log_msg(LOG_EMERG, "node %"PRId64" not found", w->ref[fpair[1]]);
         continue;
      }

      c.lat = n->lat;
      c.lon = n->lon;

      if (!(nref = farthest_node(&c, w, &pc)))
      {
         log_debug("endless loop detected - break");
         break;
      }

      // safety check
      if (nref == -1)
      {
         log_msg(LOG_EMERG, "farthes_node() return -1: this should never happen!");
         return -1;
      }

      if (pc.dist <= pc_final.dist)
         break;

      fpair[0] = fpair[1];
      fpair[1] = nref;
      pc_final = pc;
   }

   log_debug("way.id = %"PRId64", ref[%d] = %"PRId64", ref[%d] = %"PRId64", dist = %f, bearing = %f",
         w->obj.id, fpair[0], w->ref[fpair[0]], fpair[1], w->ref[fpair[1]], pc_final.dist, pc_final.bearing);

   if (a != NULL)
      *a = pc_final.bearing;

   return 0;
}


int act_cap_ini(smrule_t *r)
{
   struct actCaption cap;
   char *s;
#ifdef CAIRO_HAS_FC_FONT
   cairo_font_face_t *cfc;
   FcPattern *pat;
#endif

   memset(&cap, 0, sizeof(cap));
   cap.scl.min_auto_size = MIN_AUTO_SIZE;
   cap.scl.max_auto_size = MAX_AUTO_SIZE;
   cap.scl.min_area_size = MIN_AREA_SIZE;
   cap.scl.auto_scale = AUTO_SCALE;
   //cap.xoff = cap.yoff = POS_OFFSET;

   if ((cap.font = get_param("font", NULL, r->act)) == NULL)
   {
      log_msg(LOG_WARN, "parameter 'font' missing");
      return 1;
   }
   if (get_param("size", &cap.size, r->act) == NULL)
   {
      log_msg(LOG_WARN, "parameter 'size' missing");
      return 1;
   }
   cap.xoff = cap.yoff = mm2unit(cap.size) / 2;
   if ((cap.key = get_param("key", NULL, r->act)) == NULL)
   {
      log_msg(LOG_WARN, "parameter 'key' missing");
      return 1;
   }
   if ((s = get_param("color", NULL, r->act)) != NULL)
      cap.col = parse_color(s);

   (void) get_param("min_size", &cap.scl.min_auto_size, r->act);
   (void) get_param("max_size", &cap.scl.max_auto_size, r->act);
   (void) get_param("min_area", &cap.scl.min_area_size, r->act);
   (void) get_param("auto_scale", &cap.scl.auto_scale, r->act);

   (void) get_param("xoff", &cap.xoff, r->act);
   (void) get_param("yoff", &cap.yoff, r->act);

   parse_auto_rot(r->act, &cap.angle, &cap.rot);
   if ((cap.akey = get_param("anglekey", NULL, r->act)) != NULL && isnan(cap.angle))
   {
      log_msg(LOG_NOTICE, "anglekey=%s overrides angle=auto", cap.akey);
      cap.angle = 0;
   }

   cap.pos = parse_alignment(r->act);
   if ((cap.halignkey = get_param("alignkey", NULL, r->act)) != NULL)
   {
      cap.valignkey = cap.halignkey;
      cap.pos &= ~POS_DIR_MSK;
   }
   else
   {
      if ((cap.halignkey = get_param("halignkey", NULL, r->act)) != NULL)
         cap.pos &= ~(POS_E | POS_W);
      if ((cap.valignkey = get_param("valignkey", NULL, r->act)) != NULL)
         cap.pos &= ~(POS_N | POS_S);
   }
   log_debug("halignkey = %s, valignkey = %s", safe_null_str(cap.halignkey), safe_null_str(cap.valignkey));
   if (*cap.key == '*')
   {
      cap.key++;
      cap.pos |= POS_UC;
   }
 
   cap.hide = get_param_bool("hide", r->act);

   cap.ctx = cairo_create(sfc_);
   if (cairo_smr_log_status(cap.ctx) != CAIRO_STATUS_SUCCESS)
      return -1;

#ifdef CAIRO_HAS_FC_FONT
   if ((pat = FcNameParse((FcChar8*) cap.font)) == NULL)
   {
      log_msg(LOG_ERR, "FcNameParse(\"%s\") failed", cap.font);
      return -1;
   }
   cfc = cairo_ft_font_face_create_for_pattern(pat);
   FcPatternDestroy(pat);
   cairo_set_font_face(cap.ctx, cfc);
   cairo_font_face_destroy(cfc); 
#else
   cairo_select_font_face (cap.ctx, cap.font, 0, 0);
#endif

   cairo_smr_set_source_color(cap.ctx, cap.col);
   cairo_set_line_width(cap.ctx, THINLINE);
#ifdef PUSH_GROUP
   cairo_push_group(cap.ctx);
   CSS_INC(CSS_PUSH);
#endif

   if ((r->data = malloc(sizeof(cap))) == NULL)
   {
      log_msg(LOG_ERR, "cannot malloc: %s", strerror(errno));
      return -1;
   }

   // activate multi-threading if angle is not "auto"
   if (!isnan(cap.angle))
      sm_threaded(r);

   log_debug("%04x, %08x, '%s', '%s', %.1f, {%.1f, %.1f, %.1f, %.2f}, %.1f, %.1f, %.1f, {%.1f, %08x, %.1f}",
         cap.pos, cap.col, cap.font, cap.key, cap.size,
         cap.scl.max_auto_size, cap.scl.min_auto_size, cap.scl.min_area_size, cap.scl.auto_scale,
         cap.angle, cap.xoff, cap.yoff,
         cap.rot.phase, cap.rot.autocol, cap.rot.weight);
   memcpy(r->data, &cap, sizeof(cap));
   return 0;
}


#ifndef HAVE_MBTOWC
static void strupper(char *s)
{
   if (s == NULL)
      return;

   for (; *s != '\0'; s++)
      *s = toupper(/*(unsigned)*/ *s);
}

#else

static int strupper(char *s)
{
   wchar_t wc;
   char *su, *ss, *ostr = s;
   int wl, sl, len;

   // safety check
   if (s == NULL)
      return -1;

   len = strlen(s);
   if ((ss = su = malloc(len + 1)) == NULL)
      return -1;

   for (;;)
   {
      if ((wl = mbtowc(&wc, s, len)) == -1)
      {
         log_msg(LOG_ERR, "mbtowc() failed at '%s'", s);
         break;
         /* free(su);
         return NULL; */
      }

      if (!wl)
         break;

      // FIXME: len should be decremented?
      s += wl;
      wc = towupper(wc);
      if ((sl = wctomb(ss, wc)) == -1)
      {
         log_msg(LOG_ERR, "wctomb() failed");
         break;
         /*free(su);
         return NULL;*/
      }
      ss += sl;
   }

   *ss = '\0';
   strcpy(ostr, su);
   free(su);
   return 0;
}
#endif


/*! This function calculates the relative origin for a given bounding box
 * (width, height) dependent on the position definition pos (N, S, E, W) in
 * respect to the origin 0/0.
 * @param pos Position definition which is a bitwise OR'ed combination of
 * POS_N, POS_S, POS_E, and POS_W.
 * @param width Width of the bounding box.
 * @param height Height of the bounding box.
 * @param xoff Offset in x direction from origin.
 * @param yoff Offset in y direction from origin.
 * @param ox Pointer to a double which will receive the horizontal result.
 * @param oy Ponter to a double which will receive the vertical result.
 */
static void pos_offset(int pos, double width, double height, double xoff, double yoff, double *ox, double *oy)
{
   switch (pos & 0x3)
   {
      case POS_N:
         *oy = 0 - yoff;
         break;

      case POS_S:
         *oy = height + yoff;
         break;

      default:
         *oy = height / 2;
   }
   
   switch (pos & 0xc)
   {
      case POS_E:
         *ox = 0 + xoff;
         break;

      case POS_W:
         *ox = -width - xoff;
         break;

      default:
         *ox = -width / 2;
   }
   log_debug("pos = %04x, ox = %.2f, oy = %.2f, width = %.2f, height = %.2f", pos, *ox, *oy, width, height);
}


static cairo_surface_t *cairo_smr_cut_out(cairo_surface_t *bg, double x, double y, double r)
{
   cairo_surface_t *sfc;
   cairo_t *ctx;

   sfc = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, round(PT2PX(r)), round(PT2PX(r)));
   if (cairo_surface_status(sfc) != CAIRO_STATUS_SUCCESS)
   {
      log_msg(LOG_ERR, "failed to create background surface: %s",
            cairo_status_to_string(cairo_surface_status(sfc)));
      return NULL;
   }

   ctx = cairo_create(sfc);
   cairo_scale(ctx, PT2PX_SCALE, PT2PX_SCALE);
   x = -x + r / 2;
   y = -y + r / 2;
   cairo_set_source_surface(ctx, bg, x, y);
   cairo_paint(ctx);
   cairo_destroy(ctx);

   return sfc;
}


static cairo_surface_t *cairo_smr_plane(int w, int h, int x, int col)
{
   cairo_surface_t *sfc;
   cairo_t *ctx;

   sfc = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, round(PT2PX(w)), round(PT2PX(h)));
   if (cairo_surface_status(sfc) != CAIRO_STATUS_SUCCESS)
   {
      log_msg(LOG_ERR, "failed to create surface: %s",
            cairo_status_to_string(cairo_surface_status(sfc)));
      return NULL;
   }

   ctx = cairo_create(sfc);
   cairo_scale(ctx, PT2PX_SCALE, PT2PX_SCALE);
   cairo_smr_set_source_color(ctx, col);
   cairo_rectangle(ctx, x, 0, w - x, h);
   cairo_fill(ctx);
   //CSS_INC(CSS_FILL);
   cairo_destroy(ctx);

   return sfc;
}


static uint32_t cairo_smr_double_to_gray(double a)
{
   int c;

   if (a > 1.0) a = 1;
   if (a < 0.0) a = 0.0;

   c = round(a * 255.0);
   return c | c << 8 | c << 16 | 0xff000000;
}


#ifdef COL_STRETCH_BW
static void rot_y(cartesian_t *c, double a)
{
   double x =  c->x * cos(a) + c->z * sin(a);
   double z = -c->x * sin(a) + c->z * cos(a);
   c->x = x;
   c->z = z;
}


static void rot_z(cartesian_t *c, double a)
{
   double x =  c->x * cos(a) - c->y * sin(a);
   double y =  c->x * sin(a) + c->y * cos(a);
   c->x = x;
   c->y = y;
}


static uint32_t cairo_smr_rgb_to_color(double r, double g, double b)
{
   return COL_RED(r) | COL_GREEN(g) | COL_BLUE(b);
}


static void cairo_smr_color_bw_stretch(double f, uint32_t *col)
{
   cartesian_t c;

   c.x = REDD(*col);
   c.y = GREEND(*col);
   c.z = BLUED(*col);

	// rotate color space that bw components are along z-axis
   rot_z(&c, -M_PI_4);
   rot_y(&c, -acos(1 / sqrt(3)));

	// scale x/y components
   c.x /= f;
   c.y /= f;

	// rotate back into RGB color space
   rot_y(&c,  acos(1 / sqrt(3)));
   rot_z(&c,  M_PI_4);

   *col = (*col & 0xff000000) | cairo_smr_rgb_to_color(c.x, c.y, c.z);
}
#endif


#ifdef COL_DIFF_LUM
static double cairo_smr_rgb_luminosity(double r, double g, double b)
{
   // Luminosity (CIE XYZ formula)
   return 0.2125 * r + 0.7154 * g + 0.0721 * b;
}


static double cairo_smr_color_luminosity(uint32_t col)
{
   return cairo_smr_rgb_luminosity(REDD(col), GREEND(col), BLUED(col));
}
#endif


#ifdef COL_DIFF_BRGT
static double cairo_smr_rgb_brightness(double r, double g, double b)
{
   // YIQ brightness forumlar
   return r * 0.299 + g * 0.587 + b * 0.114;
}


static double cairo_smr_color_brightness(uint32_t col)
{
   return cairo_smr_rgb_brightness(REDD(col), GREEND(col), BLUED(col));
}
#endif


#ifdef COL_DIFF_3D
static double cairo_smr_color_dist(uint32_t c1, uint32_t c2)
{
   return sqrt((sqr(REDD(c1) - REDD(c2)) + sqr(GREEND(c1) - GREEND(c2)) + sqr(BLUED(c1) - BLUED(c2))) / 3.0);
}
#endif


/*! This calculates the difference and its variance between two surfaces.
 * Pixels which have a transparancy of more than 20% are ignored.
 * @param dst Destination surface. This surface will receive the actual
 * difference as a 1-dimensional color space (gray scale).
 * @param src To this surface dst is compared to.
 * @param v Pointer to variable which will receive the variance if not NULL.
 * @return Returns the difference which always is 0 <= diff <= 1. Values near
 * to 1 mean a high difference.
 */
static double cairo_smr_dist(cairo_surface_t *dst, cairo_surface_t *src, double *v)
{
   unsigned char *psrc, *pdst;
   uint32_t dst_pixel;
   double dist, avg, var;
   int x, y, mx, my, cnt;

   cairo_surface_flush(src);
   cairo_surface_flush(dst);
   psrc = cairo_image_surface_get_data(src);
   mx = cairo_image_surface_get_width(dst);
   my = cairo_image_surface_get_height(dst);
   pdst = cairo_image_surface_get_data(dst);

   avg = 0;
   cnt = 0;
   var = 0;
   for (y = 0; y < my; y++)
   {
      for (x = 0; x < mx; x++)
      {
         // ignore (partially) transparent pixels
         if (ALPHAD(*(((uint32_t*) pdst) + x)) > 0.2 || ALPHAD(*(((uint32_t*) psrc) + x)) > 0.2)
         {
#define CLEAR_TRANS_PIX
#ifdef CLEAR_TRANS_PIX
            *(((uint32_t*) pdst) + x) = TRANSPIX;
#endif
            continue;
         }

         // see http://www.w3.org/TR/AERT#color-contrast and
         // http://www.hgrebdes.com/colour/spectrum/colourvisibility.html and
         // http://colaargh.blogspot.co.uk/2012/08/readability-of-type-in-colour-w3c.html
         // for visibility of colors and the background.
         // Also read this: http://www.lighthouse.org/accessibility/design/accessible-print-design/effective-color-contrast

         uint32_t dst_col = *(((uint32_t*) pdst) + x);
         uint32_t src_col = *(((uint32_t*) psrc) + x);

#ifdef COL_STRETCH_BW
         cairo_smr_color_bw_stretch(COL_STRETCH_F, &dst_col);
         cairo_smr_color_bw_stretch(COL_STRETCH_F, &src_col);
#endif

#ifdef COL_DIFF_BRGT
         dist = fabs(cairo_smr_color_brightness(dst_col) -
                  cairo_smr_color_brightness(src_col));
#endif
#ifdef COL_DIFF_LUM
         dist = fabs(cairo_smr_color_luminosity(dst_col) -
                  cairo_smr_color_luminosity(src_col));
#endif
#ifdef COL_DIFF_3D
         // calculate 3D color distance
         dist = cairo_smr_color_dist(dst_col, src_col);
#endif

         dst_pixel = cairo_smr_double_to_gray(dist);
         *(((uint32_t*) pdst) + x) = dst_pixel;
         avg += dist;
         var += sqr(dist);
         cnt++;
      }
      pdst += cairo_image_surface_get_stride(dst);
      psrc += cairo_image_surface_get_stride(src);
   }
   cairo_surface_mark_dirty(dst);
   if (cnt)
      avg /= cnt;
   if (v != NULL)
      *v = var - sqr(avg);
   return avg;
}


static void cairo_smr_diff(cairo_t *ctx, cairo_surface_t *bg, int x, int y, double a)
{
   // drehpunkt festlegen
   cairo_save(ctx);
   cairo_translate(ctx, x / 2, y / 2);
   // winkel, ccw, ost = 0
   cairo_rotate(ctx, a);
   // ausschneiden
   cairo_set_operator(ctx, CAIRO_OPERATOR_OVER);
   cairo_set_source_surface(ctx, bg, cairo_image_surface_get_width(bg) / -2, cairo_image_surface_get_height(bg) / -2);
   cairo_paint(ctx);
   cairo_restore(ctx);
   return;
}


static int cmp_dp(const diffpeak_t *src, const diffpeak_t *dst)
{
   if (src->dp_end - src-> dp_start > dst->dp_end - dst->dp_start)
      return -1;
   if (src->dp_end - src-> dp_start < dst->dp_end - dst->dp_start)
      return 1;
   return 0;
}


static void dv_mkarea(const struct coord *cnode, double r, const diffvec_t *dv, int cnt)
{
   osm_node_t *n;
   osm_way_t *w;
   char buf[32];
   int i;

   w = malloc_way(1, cnt + 1);
   osm_way_default(w);
   for (i = 0; i < cnt; i++)
   {
      n = malloc_node(2);
      osm_node_default(n);
      w->ref[dv[i].dv_index] = n->obj.id;

      geo2pxf(cnode->lon, cnode->lat, &n->lon, &n->lat);
      // FIXME: there is something wrong with the radius. It is too small, but
      // with PT2PX() it gets to large.
      pxf2geo(n->lon + r * dv[i].dv_quant * cos(M_2PI - dv[i].dv_angle),
              n->lat + r * dv[i].dv_quant * sin(M_2PI - dv[i].dv_angle),
              &n->lon, &n->lat);

      //log_debug("i = %d, angle = %.1f, diff = %.2f, quant = %.2f", i, fmod2(RAD2DEG(M_PI_2 - dv[i].dv_angle), 360), dv[i].dv_diff, dv[i].dv_quant);
      snprintf(buf, sizeof(buf), "%.1f;%.1f", fmod2(RAD2DEG(M_PI_2 - dv[i].dv_angle), 360), dv[i].dv_quant * 100);
      set_const_tag(&n->obj.otag[1], "smrender:autorot:angle", strdup(buf));
      put_object((osm_obj_t*) n);
   }
   w->ref[i] = w->ref[0];
   put_object((osm_obj_t*) w);
}


/*! This function weights a diffvec according to the phase and weight. This is
 * squeezing the circle to an ellipse in order to let some angles be preferred
 * over some other angles.
 * @param dv Pointer to diffvec_t list.
 * @param len Number of diffvecs in dv.
 * @param phase Angle of major axis, 0 means horizontal.
 * @param weight Weight parameter, 0 <= weight <= 1. This is the factor to
 * which the minor axis is shortened, i.e. 1 is no distortion.
 */
static void dv_weight(diffvec_t *dv, int len, double phase, double weight)
{
   for (; len; len--, dv++)
   {
      if (weight < 0.0)
         dv->dv_diff = 1.0 - dv->dv_diff;
      dv->dv_diff *= 1.0 - (1.0 - fabs(weight)) * (1.0 - cos(dv->dv_angle * 2.0 + phase)) / 2.0;
   }
}


/*! 
 * @param max_angle Number of degrees to be rotated. It should be M_PI or
 * M_2PI.
 */
static void dv_sample(cairo_surface_t *bg, cairo_surface_t *fg, diffvec_t *dv, int num_dv)
{
   cairo_surface_t *dst;
   cairo_t *ctx;
   double x, y, a;
   int i;

   x = cairo_image_surface_get_width(fg);
   y = cairo_image_surface_get_height(fg);
   dst = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, x, y);
   cairo_smr_log_surface_status(dst);
   ctx = cairo_create(dst);
   cairo_smr_log_status(ctx);

   for (a = 0, i = 0; i < num_dv; a += M_2PI / num_dv, i++)
   {
      cairo_smr_diff(ctx, bg, x, y, a);
      dv[i].dv_diff = cairo_smr_dist(dst, fg, &dv[i].dv_var);
      dv[i].dv_angle = a;
      dv[i].dv_x = 0;
      dv[i].dv_y = 0;
      dv[i].dv_index = i;
//#define DV_SAMPLES_TO_DISK
#ifdef DV_SAMPLES_TO_DISK
      char buf[256];
      snprintf(buf, sizeof(buf), "dv_sample_%d_%d_%.1f.png", (int) x, (int) y, RAD2DEG(a));
      cairo_surface_write_to_png(dst, buf);
      //log_debug("diff = %.1f, sqrt(var) = %.1f, a = %.1f", dv[i].dv_diff, sqrt(dv[i].dv_var), RAD2DEG(dv[i].dv_angle));
#endif
   }

   cairo_destroy(ctx);
   cairo_surface_destroy(dst);
}


/*! This function stretches the dv_diff values to the range 0.0 to 1.0.
 * @param dv Pointer to list of diffvec_ts.
 * @param num_dv Number of diffvec_ts in dv.
 */
static void dv_quantize(diffvec_t *dv, int num_dv)
{
   double min, max;
   int i;

   min = 1.0;
   max = 0.0;
   for (i = 0; i < num_dv; i++)
   {
      if (dv[i].dv_diff > max)
         max = dv[i].dv_diff;
      if (dv[i].dv_diff < min)
         min = dv[i].dv_diff;
   }

   for (i = 0; i < num_dv; i++)
   {
      // FIXME: DIV0
      dv[i].dv_quant = (dv[i].dv_diff - min) / (max - min);
      if (isnan(dv[i].dv_quant))
         dv[i].dv_quant = 1.0;
   }
}


static inline int mod(int a, int n)
{
   a %= n;
   return a >= 0 ? a : a + n;
}


static int dp_get(const diffvec_t *dv, int num_dv, diffpeak_t **rp)
{
   int i, peak, cnt = 0, last;
   diffpeak_t *dp = NULL;

   // check if first element is below (peak = 0) or above the limit (peak = 1)
   peak = dv[0].dv_quant >= DP_LIMIT ? 1 : 0;
   // loop over all elements + 1 (modulo number of elements) to wrap around in
   // case of the edge is exactly between 360 and 0 degrees.
   for (i = 1, last = num_dv; i <= last; i++)
   {
      // check if it is a negative edge (value drops below limit)
      if (peak && dv[i % num_dv].dv_quant < DP_LIMIT)
      {
         peak = 0;
         // check if there was a positive edge before
         if (cnt)
         {
            // set end angle of preceding peak
            if (dv[i % num_dv].dv_angle > dv[(i - 1) % num_dv].dv_angle)
               dp[cnt - 1].dp_end = (dv[i % num_dv].dv_angle + dv[(i - 1) % num_dv].dv_angle) / 2;
            else
               dp[cnt - 1].dp_end = (dv[i % num_dv].dv_angle + dv[(i - 1) % num_dv].dv_angle + M_2PI) / 2;

            // wrap angle around 360 if necessary
            if (dp[cnt - 1].dp_end < dp[cnt - 1].dp_start)
               dp[cnt - 1].dp_end += M_2PI;
         }
         continue;
      }
      // check if it is a positive edge (value raises above limit)
      if (!peak && dv[i % num_dv].dv_quant >= DP_LIMIT)
      {
         peak = 1;
         // check if it is the first positive edge
         if (!cnt)
         {
            last = i + num_dv - 1;
         }
         cnt++;
         if ((*rp = realloc(dp, cnt * sizeof(*dp))) == NULL)
         {
            log_msg(LOG_ERR, "failed to realloc diffpeak_t: %s", strerror(errno));
            free(dp);
            return -1;
         }
         dp = *rp;
         if (dv[i % num_dv].dv_angle > dv[(i - 1) % num_dv].dv_angle)
            dp[cnt - 1].dp_start = (dv[i % num_dv].dv_angle + dv[(i - 1) % num_dv].dv_angle) / 2;
         else
            dp[cnt - 1].dp_start = (dv[i % num_dv].dv_angle + dv[(i - 1) % num_dv].dv_angle + M_2PI) / 2;
      }
   }
   *rp = dp;
   return cnt;
}


static double find_angle(const struct coord *c, const struct auto_rot *rot, cairo_surface_t *fg, cairo_surface_t *bg)
{
   diffvec_t *dv;
   cairo_surface_t *sfc;
   diffpeak_t *dp;
   double x, y, a, r;
   int i, num_steps;

   // safety check...code works only with image surfaces
   if (cairo_surface_get_type(fg) != CAIRO_SURFACE_TYPE_IMAGE)
   {
      log_msg(LOG_WARN, "this works only with image surfaces");
      return 0;
   }

   geo2pt(c->lon, c->lat, &x, &y);
   r = rdata_px_unit(hypot(cairo_image_surface_get_width(fg), cairo_image_surface_get_height(fg)), U_PT);

   // make a step every 0.5mm of the circumference
   num_steps = round(r * M_PI * 1.0 * 25.4 / 72.0);
   log_debug("diameter = %.2f pt, num_steps = %d", r * M_PI, num_steps);
   if ((dv = malloc(num_steps * sizeof(*dv))) == NULL)
   {
      log_msg(LOG_ERR, "failed to malloc(): %s", strerror(errno));
      free(dv);
      return 0;
   }

   if ((sfc = cairo_smr_cut_out(bg, x, y, r)) == NULL)
   {
      log_msg(LOG_ERR, "failed to cut out auto-rotation background");
      free(dv);
      return 0;
   }

   dv_sample(sfc, fg, dv, num_steps);
   cairo_surface_destroy(sfc);

   dv_weight(dv, num_steps, DEG2RAD(rot->phase), rot->weight);
   dv_quantize(dv, num_steps);
   if (rot->mkarea)
      dv_mkarea(c, r, dv, num_steps);
   if ((i = dp_get(dv, num_steps, &dp)) == -1)
   {
      log_msg(LOG_ERR, "something went wrong in dp_get()");
      return 0;
   }

   if (i)
   {
      qsort(dp, i, sizeof(diffpeak_t), (int(*)(const void*, const void*)) cmp_dp);
      a = M_2PI - (dp->dp_end + dp->dp_start) / 2;
   }
   else
      a = 0;

   free(dp);
   free(dv);

   return a;
}


static const char *pos_to_str(int pos)
{
   if (pos & POS_E)
      return "east";
   if (pos & POS_W)
      return "west";
   return "center";
}


/*! This function looks up the tag with the key 'key' in the object o and
 * interpretes its value as alignment parameter (north, east,...).
 * @param o Pointer to object.
 * @param key Name of key.
 * @return Returns an integer containg POS_N, POS_S, POS_E, POS_W locically
 * or'd together. If an error occurs, 0 is returned (which is the center
 * position) and errno is set to EINVAL if the key contains an invalid value.
 * If the object has no such key, errno is set to ENOMSG.
 */
static int retr_align_key_pos(const osm_obj_t *o, const char *key)
{
   char align[10];
   int n, pos = 0;

   if ((n = match_attr(o, key, NULL)) >= 0)
   {
      if (o->otag[n].v.len <= 9)
      {
         memcpy(align, o->otag[n].v.buf, o->otag[n].v.len);
         align[o->otag[n].v.len] = '\0';
         pos = parse_alignment_str(align);
      }
      else
      {
         log_msg(LOG_WARN, "key %.*s contains ill tag value", o->otag[n].k.len, o->otag[n].k.buf);
         errno = EINVAL;
      }
   }
   else
      errno = ENOMSG;

   return pos;
}


static int cap_coord(const struct actCaption *cap, const struct coord *c, const bstring_t *str, osm_obj_t *o)
{
   cairo_text_extents_t tx;
   cairo_font_extents_t fe;
   cairo_surface_t *pat;
   char buf[str->len + 1];
   double x, y, a, r;
   double width, height;
   short pos;
   int n;

   if (cap->size == 0.0)
      return 0;

   cairo_save(cap->ctx);
   geo2pt(c->lon, c->lat, &x, &y);
   cairo_translate(cap->ctx, x, y);

#ifdef AUTOSFC
   if (isnan(cap->angle))
   {
      cairo_save(cap->auto_ctx);
      cairo_translate(cap->auto_ctx, x, y);
   }
#endif

   memcpy(buf, str->buf, str->len);
   buf[str->len] = '\0';
   if (cap->pos & POS_UC)
      strupper(buf);

   pos = cap->pos;
   // parse alignkey if specified in ruleset
   if (cap->halignkey != NULL || cap->valignkey != NULL)
   {
      log_debug("detecting alignkey, pos = 0x%04x", pos);
      if (cap->halignkey == cap->valignkey)
         pos = retr_align_key_pos(o, cap->halignkey);
      else 
      {
         if (cap->halignkey != NULL)
            pos = (pos & ~(POS_E | POS_W)) | (retr_align_key_pos(o, cap->halignkey) & (POS_E | POS_W));
         if (cap->valignkey != NULL)
            pos = (pos & ~(POS_N | POS_S)) | (retr_align_key_pos(o, cap->valignkey) & (POS_N | POS_S));
      }
      log_debug("new pos = 0x%04x", pos);
   }
 
   cairo_set_font_size(cap->ctx, mm2unit(cap->size));
   cairo_font_extents(cap->ctx, &fe);
   cairo_text_extents(cap->ctx, buf, &tx);

   if (isnan(cap->angle))
   {
      // FIXME: position check not finished yet
      if (cap->pos & 0xc)
         pos = (cap->pos & 0xfff0) | POS_E;
      else
         pos = cap->pos;

      //r = hypot(tx.width + tx.x_bearing, fe.ascent / 2) + POS_OFFSET;
      // FIXME: not sure if this is correct (POS_OFFSET --> xoff/yoff)
      r = hypot(tx.width + tx.x_bearing + cap->xoff, fe.ascent / 2 + cap->yoff);
      width = tx.width + tx.x_bearing + cap->xoff;
      height = fe.ascent;
      if (cap->pos & 0xc)
      {
         r *= 2;
         if ((pat = cairo_smr_plane(width * 2, height, width, cap->col)) == NULL)
            return -1;
      }
      else
      {
         if ((pat = cairo_smr_plane(width, height, 0, cap->col)) == NULL)
            return -1;
      }

      a = find_angle(c, &cap->rot, pat, cap->auto_sfc);

      // flip text if necessary
      if (a > M_PI_2 && a < 3 * M_PI_2)
      {
         a -= M_PI;
         if (pos & POS_E)
         {
            pos = (cap->pos & 0xfff0) | POS_W;
            //log_debug("flip east/west");
         }
      }

#define AUTO_SUBTAGS
#ifdef AUTO_SUBTAGS
#define AUTOANGLE_SUBTAG "autoangle"
#define AUTOALIGN_SUBTAG "autoalign"
      // add "autoangle"
      if (realloc_tags(o, o->tag_cnt + 1) != -1)
      {
         o->otag[o->tag_cnt - 1].k.len = strlen(cap->key) + strlen(AUTOANGLE_SUBTAG) + 2;
         o->otag[o->tag_cnt - 1].v.len = 8;
         if ((o->otag[o->tag_cnt - 1].k.buf = malloc(o->otag[o->tag_cnt - 1].k.len)) == NULL)
         {
            log_msg(LOG_ERR, "malloc() failed: %s", strerror(errno));
            o->tag_cnt--;
         }
         else if ((o->otag[o->tag_cnt - 1].v.buf = malloc(o->otag[o->tag_cnt - 1].v.len)) == NULL)
         {
            log_msg(LOG_ERR, "malloc() failed: %s", strerror(errno));
            free(o->otag[o->tag_cnt - 1].k.buf);
            o->tag_cnt--;
         }
         else
         {
            o->otag[o->tag_cnt - 1].k.len = snprintf(o->otag[o->tag_cnt - 1].k.buf,
                  o->otag[o->tag_cnt - 1].k.len,
                  "%s:"AUTOANGLE_SUBTAG, cap->key);
            o->otag[o->tag_cnt - 1].v.len = snprintf(o->otag[o->tag_cnt - 1].v.buf,
                  o->otag[o->tag_cnt - 1].v.len, "%.1f", RAD2DEG(a));
         }
      }

      // autoalign
      if (realloc_tags(o, o->tag_cnt + 1) != -1)
      {
         o->otag[o->tag_cnt - 1].k.len = strlen(cap->key) + strlen(AUTOALIGN_SUBTAG) + 2;
         o->otag[o->tag_cnt - 1].v.len = 8;
         if ((o->otag[o->tag_cnt - 1].k.buf = malloc(o->otag[o->tag_cnt - 1].k.len)) == NULL)
         {
            log_msg(LOG_ERR, "malloc() failed: %s", strerror(errno));
            o->tag_cnt--;
         }
         else if ((o->otag[o->tag_cnt - 1].v.buf = malloc(o->otag[o->tag_cnt - 1].v.len)) == NULL)
         {
            log_msg(LOG_ERR, "malloc() failed: %s", strerror(errno));
            free(o->otag[o->tag_cnt - 1].k.buf);
            o->tag_cnt--;
         }
         else
         {
            o->otag[o->tag_cnt - 1].k.len = snprintf(o->otag[o->tag_cnt - 1].k.buf,
                  o->otag[o->tag_cnt - 1].k.len,
                  "%s:"AUTOALIGN_SUBTAG, cap->key);
            o->otag[o->tag_cnt - 1].v.len = snprintf(o->otag[o->tag_cnt - 1].v.buf,
                  o->otag[o->tag_cnt - 1].v.len, "%s", pos_to_str(pos));
         }
      }
#endif
   }
   else
   {
      a = 0;
      if (cap->akey != NULL && (n = match_attr(o, cap->akey, NULL)) >= 0)
         a = DEG2RAD(bs_tod(o->otag[n].v));
      a += DEG2RAD(360 - cap->angle);
   }

   cairo_rotate(cap->ctx, a);
   pos_offset(pos, tx.width + tx.x_bearing, fe.ascent, cap->xoff, cap->yoff, &x, &y);
   /* debugging
   if (isnan(cap->angle))
   {
      cairo_rectangle(cap->ctx, x, y, tx.width + tx.x_bearing + cap->xoff, -fe.ascent);
      cairo_stroke(cap->ctx);
   }*/
#ifdef AUTOSFC
   if (isnan(cap->angle))
   {
      cairo_rotate(cap->auto_ctx, a);
      cairo_rectangle(cap->auto_ctx, x, y, tx.width + tx.x_bearing + cap->xoff, -fe.ascent);
      cairo_fill(cap->auto_ctx);
      cairo_restore(cap->auto_ctx);
   }
#endif
   if (!cap->hide)
   {
      cairo_move_to(cap->ctx, x, y);
      cairo_show_text(cap->ctx, buf);
   }
   cairo_restore(cap->ctx);

   return 0;
}


static int cap_way(const struct actCaption *cap, osm_way_t *w, const bstring_t *str)
{
   struct actCaption tmp_cap;
   struct coord c;
   double ar;

   // FIXME: captions on open polygons missing
   if (!is_closed_poly(w))
      return 0;

   if (poly_area(w, &c, &ar))
      return 0;

   memcpy(&tmp_cap, cap, sizeof(tmp_cap));
   if (tmp_cap.size == 0.0)
   {
      double area_mm2 = fabs(ar) * rdata_square_mm() / rdata_square_nm();
      tmp_cap.size = cap->scl.auto_scale * sqrt(area_mm2);
      log_debug("tmp_cap.size = %.1f, ar = %f [nm2], ar = %.1f [mm2], str = \"%.*s\"",
            tmp_cap.size, fabs(ar), area_mm2,  str->len, str->buf);

      // check upper font size limit
      if (cap->scl.max_auto_size != 0.0 && tmp_cap.size > cap->scl.max_auto_size)
         tmp_cap.size = cap->scl.max_auto_size;

      // check lower font size limit
      if (cap->scl.min_auto_size != 0.0 && tmp_cap.size < cap->scl.min_auto_size)
      {
         // omit caption if area is smaller than allowed
         if (area_mm2 < cap->scl.min_area_size)
            tmp_cap.size = 0.0;
         else
            tmp_cap.size = cap->scl.min_auto_size;
      }
   }

   if (tmp_cap.angle == MAJORAXIS)
   {
      area_axis(w, &tmp_cap.angle);
      // correct bearing to math angle
      tmp_cap.angle = fmod2(90 - tmp_cap.angle, 360);
      // correct angle if that font is upright readable
      // FIXME: that's probably the wrong place to do that
      if (tmp_cap.angle > 90 && tmp_cap.angle <= 270)
         tmp_cap.angle -= 180.0;
      log_debug("tmp_cap.angle = %.1f", tmp_cap.angle);
   }

   return cap_coord(&tmp_cap, &c, str, (osm_obj_t*) w);
}


int act_cap_main(smrule_t *r, osm_obj_t *o)
{
   struct coord c;
   int n;

#ifdef AUTOSFC
   struct actCaption *cap = (struct actCaption*) r->data;
   // create temporary background surface at first call to cap_main()
   if (isnan(cap->angle) && (cap->auto_sfc == NULL))
   {
      cap->auto_sfc = cairo_smr_recording_surface_from_bg();
      cap->auto_ctx = cairo_create(cap->auto_sfc);
      cairo_smr_set_source_color(cap->auto_ctx, cap->col);
   }
#endif

   if ((n = match_attr(o, ((struct actCaption*) r->data)->key, NULL)) == -1)
   {
      //log_debug("node %ld has no caption tag '%s'", nd->nd.id, rl->rule.cap.key);
      return 0;
   }

   switch (o->type)
   {
      case OSM_NODE:
         c.lon = ((osm_node_t*) o)->lon;
         c.lat = ((osm_node_t*) o)->lat;
         return cap_coord(r->data, &c, &o->otag[n].v, o);

      case OSM_WAY:
         return cap_way(r->data, (osm_way_t*) o, &o->otag[n].v);
   }

   return 1;
}


int act_cap_fini(smrule_t *r)
{
   struct actCaption *cap = r->data;

#ifdef PUSH_GROUP
   cairo_pop_group_to_source(cap->ctx);
   CSS_INC(CSS_POP);
   cairo_paint(cap->ctx);
   CSS_INC(CSS_PAINT);
#endif
   cairo_destroy(cap->ctx);

#ifdef AUTOSFC
   if (cap->auto_ctx != NULL)
      cairo_destroy(cap->auto_ctx);
   if (cap->auto_sfc != NULL)
   {
      //cairo_surface_write_to_png(cap->auto_sfc, "autosfcbg.png");
      cairo_surface_destroy(cap->auto_sfc);
   }
#endif

   free(cap);
   r->data = NULL;

   return 0;
}

 
int act_img_ini(smrule_t *r)
{
   cairo_surface_t *sfc;
   cairo_t *ctx;
   struct actImage img;
   char *name;
   int e;

   if (r->oo->type != OSM_NODE && r->oo->type != OSM_WAY)
   {
      log_msg(LOG_WARN, "img() only applicable to nodes and ways");
      return -1;
   }

   if ((name = get_param("file", NULL, r->act)) == NULL)
   {
      log_msg(LOG_WARN, "parameter 'file' missing");
      return -1;
   }

   memset(&img, 0, sizeof(img));

   if (get_param("scale", &img.scale, r->act) == NULL)
      img.scale = 1;
   img.scale *= get_rdata()->img_scale;

   if (strlen(name) >= 4 && !strcasecmp(name + strlen(name) - 4, ".svg"))
   {
#ifdef HAVE_RSVG
      log_debug("opening SVG '%s'", name);

      cairo_rectangle_t rect;
      RsvgDimensionData dd;
      RsvgHandle *rh;

      if ((rh = rsvg_handle_new_from_file(name, NULL)) == NULL)
      {
         log_msg(LOG_ERR, "error opening file %s", name);
         return -1;
      }

      //rsvg_handle_set_dpi(rh, 72);
      rsvg_handle_get_dimensions(rh, &dd);
      log_debug("svg dimension: w = %d, h = %d", dd.width, dd.height);

      img.w = dd.width * img.scale;
      img.h = dd.height * img.scale;
      rect.x = 0;
      rect.y = 0;
      rect.width = img.w;
      rect.height = img.h;
      img.img = cairo_recording_surface_create(CAIRO_CONTENT_COLOR_ALPHA, &rect);
      ctx = cairo_create(img.img);
      cairo_scale(ctx, img.scale, img.scale);
      if (!rsvg_handle_render_cairo(rh, ctx))
      {
         log_msg(LOG_ERR, "rsvg_handle_render_cairo() failed");
         return -1;
      }
      cairo_destroy(ctx);

      g_object_unref(rh);
#else
      log_msg(LOG_WARN, "unabled to load file %s: compiled without SVG support", name);
#endif
   }
   else
   {
      if (strlen(name) >= 4 && !strcasecmp(name + strlen(name) - 4, ".jpg"))
      {
#ifdef HAVE_LIBJPEG
         log_debug("opening JPG '%s'", name);
         sfc = cairo_image_surface_create_from_jpeg(name);
         if ((e = cairo_surface_status(sfc)) != CAIRO_STATUS_SUCCESS)
         {
            log_msg(LOG_ERR, "cannot open file %s: %s", name, cairo_status_to_string(e));
            return -1;
         }
#else
         log_msg(LOG_WARN, "unabled to load file %s: compiled without JPG support", name);
#endif
      }
      else
      {
         log_debug("opening PNG '%s'", name);
         sfc = cairo_image_surface_create_from_png(name);
         if ((e = cairo_surface_status(sfc)) != CAIRO_STATUS_SUCCESS)
         {
            log_msg(LOG_ERR, "cannot open file %s: %s", name, cairo_status_to_string(e));
            return -1;
         }
      }

      img.w = cairo_image_surface_get_width(sfc) * img.scale;
      img.h = cairo_image_surface_get_height(sfc) * img.scale;
      img.img = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, img.w, img.h);
      if ((e = cairo_surface_status(img.img)) != CAIRO_STATUS_SUCCESS)
      {
         log_msg(LOG_ERR, "cannot open file %s: %s", name, cairo_status_to_string(e));
         //cairo_surface_destroy(img.img);
         return -1;
      }
      ctx = cairo_create(img.img);
      cairo_scale(ctx, img.scale, img.scale);
      cairo_set_source_surface(ctx, sfc, 0, 0);
      cairo_paint(ctx);
      cairo_destroy(ctx);
      cairo_surface_destroy(sfc);
   }

   img.ctx = cairo_create(sfc_);
   if ((e = cairo_status(img.ctx)) != CAIRO_STATUS_SUCCESS)
   {
      log_msg(LOG_ERR, "cannot create cairo context: %s", cairo_status_to_string(e));
      cairo_surface_destroy(img.img);
      return -1;
   }

   parse_auto_rot(r->act, &img.angle, &img.rot);
   img.akey = get_param("anglekey", NULL, r->act);
   if (img.akey != NULL && isnan(img.angle))
   {
      log_msg(LOG_NOTICE, "ignoring angle=auto");
      img.angle = 0;
   }
   img.alignkey = get_param("alignkey", NULL, r->act);

   if (r->oo->type == OSM_NODE)
   {
      cairo_scale(img.ctx, PX2PT_SCALE, PX2PT_SCALE);
   }
   else if (r->oo->type == OSM_WAY)
   {
      if (isnan(img.angle))
      {
         log_msg(LOG_NOTICE, "ignoring angle=auto");
         img.angle = 0;
      }
      img.pat = cairo_pattern_create_for_surface(img.img);
      if (cairo_pattern_status(img.pat) != CAIRO_STATUS_SUCCESS)
      {
         log_msg(LOG_ERR, "failed to create pattern");
         return -1;
      }
      cairo_matrix_t m;
      cairo_matrix_init_scale(&m, 1/PX2PT_SCALE, 1/PX2PT_SCALE);
      cairo_matrix_rotate(&m, DEG2RAD(img.angle));
      cairo_pattern_set_matrix(img.pat, &m);
      cairo_pattern_set_extend(img.pat, CAIRO_EXTEND_REPEAT);
      cairo_set_source(img.ctx, img.pat);
   }

#ifdef PUSH_GROUP
   cairo_push_group(img.ctx);
   CSS_INC(CSS_PUSH);
#endif
  
   if ((r->data = malloc(sizeof(img))) == NULL)
   {
      log_msg(LOG_ERR, "cannot malloc: %s", strerror(errno));
      return -1;
   }

//   if (!isnan(img.angle))
//      sm_threaded(r);

   memcpy(r->data, &img, sizeof(img));

   return 0;
}


int img_fill(struct actImage *img, osm_way_t *w)
{
   if (!is_closed_poly(w))
      return 0;

   cairo_smr_poly_line(w, img->ctx);
   cairo_fill(img->ctx);
   CSS_INC(CSS_FILL);
   return 0;
}


int img_place(const struct actImage *img, const osm_node_t *n)
{
   double x, y, a;
   struct coord c;

   cairo_save(img->ctx);
   geo2pxf(n->lon, n->lat, &x, &y);
   cairo_translate(img->ctx, x, y);

   if (isnan(img->angle))
   {
      // auto-rot code
      c.lat = n->lat;
      c.lon = n->lon;

      cairo_surface_t *fg = img->img;
      int nimg = 0;
      if (cairo_surface_get_type(img->img) != CAIRO_SURFACE_TYPE_IMAGE)
      {
         log_debug("create temporary image surface");
#ifdef HAVE_CAIRO_SURFACE_CREATE_SIMILAR_IMAGE
         fg = cairo_surface_create_similar_image(img->img, CAIRO_FORMAT_ARGB32, img->w, img->h);
#else
         fg = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, img->w, img->h);
#endif
         cairo_t *fgx = cairo_create(fg);
         cairo_set_source_surface(fgx, img->img, 0, 0);
         cairo_paint(fgx);
         CSS_INC(CSS_PAINT);
         cairo_destroy(fgx);
      }

      a = find_angle(&c, &img->rot, fg, sfc_);

      if (nimg)
         cairo_surface_destroy(fg);
   }
   else
   {
      int m = -1;
      a = 0;
      if (img->akey != NULL && (m = match_attr(&n->obj, img->akey, NULL)) >= 0)
         a = DEG2RAD(bs_tod(n->obj.otag[m].v));
      if (m >= 0)
         log_debug("detected anglekey: %.1f", RAD2DEG(a));

      if (img->alignkey != NULL && (m = match_attr(&n->obj, img->alignkey, NULL)) >= 0)
      {
         int pos;
         char buf[n->obj.otag[m].v.len + 1];
         memcpy(buf, n->obj.otag[m].v.buf, sizeof(buf) - 1);
         buf[n->obj.otag[m].v.len] = '\0';
         pos = parse_alignment_str(buf);
         if (pos & POS_W)
            //a = a + M_PI;
            //a = -(a + M_PI);
            a = a + M_PI_2;
      }
      a += DEG2RAD(360 - img->angle);
   }

   cairo_rotate(img->ctx, a);
   cairo_set_source_surface(img->ctx, img->img, img->w / -2.0, img->h / -2.0);
   cairo_paint(img->ctx);
   CSS_INC(CSS_PAINT);
   cairo_restore(img->ctx);

   return 0;
}


int act_img_main(smrule_t *r, osm_obj_t *o)
{
   if (o->type == OSM_NODE)
      return img_place(r->data, (osm_node_t*) o);
   if (o->type == OSM_WAY)
      return img_fill(r->data, (osm_way_t*) o);

   log_msg(LOG_WARN, "img() not applicable to object type %d", o->type);
   return 1;
}


int act_img_fini(smrule_t *r)
{
   struct actImage *img = r->data;

#ifdef PUSH_GROUP
   cairo_pop_group_to_source(img->ctx);
   CSS_INC(CSS_POP);
   cairo_paint(img->ctx);
   CSS_INC(CSS_PAINT);
#endif

   if (img->pat)
      cairo_pattern_destroy(img->pat);

   cairo_destroy(img->ctx);
   cairo_surface_destroy(img->img);
   free(img);
   r->data = NULL;
   return 0;
}


int act_clip_ini(smrule_t *r)
{
   char *s, *nptr, *eptr;
   double *bc;
   int i;

   if ((bc = malloc(sizeof(*bc) * 4)) == NULL)
   {
      log_errno(LOG_ERR, "malloc() failed");
      return 1;
   }

   if ((s = get_param("border", NULL, r->act)) == NULL)
   {
      for (i = 0; i < 4; i++)
         bc[i] = G_MARGIN + G_TW + G_STW;
      log_debug("setting border to default = %.1f mm", bc[0]);
   }
   else
   {
      for (s = strtok_r(s, ",", &nptr), i = 0; s != NULL && i < 4; s = strtok_r(NULL, ",", &nptr), i++)
      {
         errno = 0;
         bc[i] = strtod(s, &eptr); 
         if (errno)
         {
            log_msg(LOG_WARN, "parameter '%s' out of range", s);
            free(bc);
            return 1;
         }
         if (s == eptr)
         {
            log_msg(LOG_WARN, "cannot convert '%s'", s);
            free(bc);
            return 1;
         }
      }

      if (i < 4)
      {
         log_msg(LOG_WARN, "border requires 4 values");
         free(bc);
         return 1;
      }
   }

   r->data = bc;
   return 0;
}


/*! Install a clipping region.
 * FIXME: This does not yet work because clipping works only within a cairo
 * context but not across, from one to another.
 */
int act_clip_fini(smrule_t *r)
{
   double *bc = r->data;
   cairo_t *ctx;

   log_msg(LOG_DEBUG, "%.1f, %.1f, %.1f, %.1f", bc[0], bc[1], bc[2], bc[3]);

   ctx = cairo_create(sfc_);

   cairo_move_to(ctx, 0, 0);
   cairo_line_to(ctx, rdata_width(U_PT), 0);
   cairo_line_to(ctx, rdata_width(U_PT), rdata_height(U_PT));
   cairo_line_to(ctx, 0, rdata_height(U_PT));
   cairo_line_to(ctx, 0, 0);
 
   cairo_move_to(ctx, mm2unit(bc[3]), mm2unit(bc[0]));
   cairo_line_to(ctx, mm2unit(bc[3]), rdata_height(U_PT) - mm2unit(bc[2]));
   cairo_line_to(ctx, rdata_width(U_PT) - mm2unit(bc[1]), rdata_height(U_PT) - mm2unit(bc[2]));
   cairo_line_to(ctx, rdata_width(U_PT) - mm2unit(bc[1]), mm2unit(bc[0]));
   cairo_line_to(ctx, mm2unit(bc[3]), mm2unit(bc[0]));

   cairo_smr_set_source_color(ctx, parse_color("bgcolor"));
   cairo_fill(ctx);
   CSS_INC(CSS_FILL);
 
   cairo_destroy(ctx);

   return 0;
}


#endif

