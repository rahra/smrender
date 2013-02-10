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

/*! This file contains the code of the rule parser and main loop of the render
 * as well as the code for traversing the object (nodes/ways) tree.
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
#include <ctype.h>
#ifdef WITH_THREADS
#include <pthread.h>
#endif
#include <cairo.h>
#ifdef CAIRO_HAS_FT_FONT
#include <cairo-ft.h>
#endif
#ifdef CAIRO_HAS_PDF_SURFACE
#include <cairo-pdf.h>
#endif

#define mm2unit(x) mm2ptf(x)
#define THINLINE rdata_px_unit(1, U_PT)
#define mm2wu(x) ((x) == 0.0 ? THINLINE : mm2unit(x))

#include "smrender_dev.h"
#include "smcoast.h"
#include "rdata.h"

// define this if color difference shall be calculated in the 3d color space.
//#define COL_DIFF_3D
// define this if color difference shall be calculated by the YIQ brightness.
//#define COL_DIFF_BRGT
// define this if color difference shall be calculated by the luminosity.
#define COL_DIFF_LUM

#define POS_OFFSET mm2ptf(1.4)

#define COL_COMP(x, y) ((x) >> (y) & 0xff)
#define COL_COMPD(x, y) ((double) COL_COMP(x, y) / 255.0)
#define REDD(x) COL_COMPD(x, 16)
#define GREEND(x) COL_COMPD(x, 8)
#define BLUED(x) COL_COMPD(x, 0)
#define ALPHAD(x) (1.0 - COL_COMPD(x & 0x7f000000, 23))

#define M_2PI (2.0 * M_PI)
#define PT2PX_SCALE (rdata_dpi() / 72.0)
#define PT2PX(x) ((x) * PT2PX_SCALE)
#define PX2PT_SCALE (72.0 / rdata_dpi())
#define DP_LIMIT 0.95
#define TILE_SIZE 256


typedef struct diffvec
{
   double dv_diff;
   int dv_x, dv_y;
   double dv_angle, dv_quant;
   int dv_index;
} diffvec_t;


typedef struct diffpeak
{
   double dp_start;
   double dp_end;
} diffpeak_t;


static cairo_surface_t *sfc_;
static cairo_rectangle_t ext_;


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

   sfc = cairo_recording_surface_create(CAIRO_CONTENT_COLOR_ALPHA, &ext_);
   if ((e = cairo_surface_status(sfc)) != CAIRO_STATUS_SUCCESS)
   {
      log_msg(LOG_ERR, "failed to create cairo surface: %s", cairo_status_to_string(e));
      return NULL;
   }
   cairo_surface_set_fallback_resolution(sfc, rdata_dpi(), rdata_dpi());
   return sfc;
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
   cairo_destroy(ctx);

   log_msg(LOG_DEBUG, "background color is set to 0x%08x", parse_color("bgcolor"));
}


static cairo_status_t cairo_smr_write_func(void *closure, const unsigned char *data, unsigned int length)
{
   return fwrite(data, length, 1, closure) == 1 ? CAIRO_STATUS_SUCCESS : CAIRO_STATUS_WRITE_ERROR;
}


void save_main_image(FILE *f, int ftype)
{
   cairo_surface_t *sfc;
   cairo_status_t e;
   cairo_t *dst;

   log_msg(LOG_INFO, "saving image (ftype = %d)", ftype);

   switch (ftype)
   {
      case FTYPE_PNG:
         sfc = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, rdata_width(U_PX), rdata_height(U_PX));
         dst = cairo_create(sfc);
         cairo_smr_log_status(dst);
         cairo_scale(dst, (double) rdata_dpi() / 72, (double) rdata_dpi() / 72);
         cairo_set_source_surface(dst, sfc_, 0, 0);
         cairo_paint(dst);
         cairo_destroy(dst);
         if ((e = cairo_surface_write_to_png_stream(sfc, cairo_smr_write_func, f)) != CAIRO_STATUS_SUCCESS)
            log_msg(LOG_ERR, "failed to save png image: %s", cairo_status_to_string(e));
         cairo_surface_destroy(sfc);
         return;

      case FTYPE_PDF:
#ifdef CAIRO_HAS_PDF_SURFACE
         log_debug("width = %.2f pt, height = %.2f pt", rdata_width(U_PT), rdata_height(U_PT));
         sfc = cairo_pdf_surface_create_for_stream(cairo_smr_write_func, f, rdata_width(U_PT), rdata_height(U_PT));
         cairo_pdf_surface_restrict_to_version(sfc, CAIRO_PDF_VERSION_1_4);
         dst = cairo_create(sfc);
         cairo_smr_log_status(dst);
         cairo_set_source_surface(dst, sfc_, 0, 0);
         cairo_paint(dst);
         cairo_show_page(dst);
         cairo_destroy(dst);
         cairo_surface_destroy(sfc);
#else
         log_msg(LOG_NOTICE, "cannot create PDF, cairo was compiled without PDF support");
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


int cairo_smr_pixel_pos(int x, int y, int s)
{
   return x * sizeof (uint32_t) + y * s;
}


int cairo_smr_get_pixel(cairo_surface_t *sfc, int x, int y)
{
   unsigned char *data;

   // FIXME: flush may be done outside get_pixel()
   cairo_surface_flush(sfc);
   if ((data = cairo_image_surface_get_data(sfc)) == NULL)
      return 0;

   return *(data + cairo_smr_pixel_pos(x, y, cairo_image_surface_get_stride(sfc)));
}


int cairo_smr_get_bg_pixel(int x, int y)
{
   return cairo_smr_get_pixel(sfc_, x, y);
}


static void parse_auto_rot(const action_t *act, double *angle, struct auto_rot *rot)
{
   char *val;

   if ((val = get_param("angle", angle, act)) == NULL)
      return;

   if (strcmp("auto", val))
      return;

   *angle = NAN;
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
}

 
int act_draw_ini(smrule_t *r)
{
   struct actDraw *d;
   double a;
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

   // honor direction of ways
   if (get_param("directional", &a, r->act) == NULL)
      a = 0;
   d->directional = a != 0;

   if (get_param("ignore_open", &a, r->act) == NULL)
      a = 0;
   d->collect_open = a == 0;

   d->wl = init_wlist();

   d->ctx = cairo_create(sfc_);
   if (cairo_smr_log_status(d->ctx) != CAIRO_STATUS_SUCCESS)
   {
      free(d);
      r->data = NULL;
      return -1;
   }
   cairo_push_group(d->ctx);

   sm_threaded(r);

   //log_msg(LOG_DEBUG, "directional = %d, ignore_open = %d", d->directional, !d->collect_open);
   log_msg(LOG_DEBUG, "{%08x, %.1f, %d, %d}, {%08x, %.1f, %d, %d}, %d, %d, %p",
        d->fill.col, d->fill.width, d->fill.style, d->fill.used,
        d->border.col, d->border.width, d->border.style, d->border.used,
        d->directional, d->collect_open, d->wl);

   return 0;
}


/*! Create a cairo path from a way.
 */
static void cairo_smr_poly_line(const osm_way_t *w, cairo_t *ctx)
{
   osm_node_t *n;
   double x, y;
   int i;

   cairo_new_path(ctx);
   for (i = 0; i < w->ref_cnt; i++)
   {
      if ((n = get_object(OSM_NODE, w->ref[i])) == NULL)
      {
         log_msg(LOG_WARN, "node %ld of way %ld at pos %d does not exist", (long) w->ref[i], (long) w->obj.id, i);
         continue;
      }

      geo2pt(n->lon, n->lat, &x, &y);
      cairo_line_to(ctx, x, y);
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


/*! Render the way properly to the cairo context.
 */
static void render_poly_line(cairo_t *ctx, const struct actDraw *d, const osm_way_t *w, int cw)
{
   if (d->border.used)
   {
      cairo_smr_set_source_color(ctx, d->border.col);
      cairo_set_line_width(ctx, cairo_smr_border_width(d, is_closed_poly(w)));
      cairo_smr_poly_line(w, ctx);
      cairo_stroke(ctx);
   }

   if (d->fill.used)
   {
      cairo_smr_poly_line(w, ctx);
      if (cw)  // this should only be allowed if it is a closed polygon
      {
         //log_debug("cw: clearing");
         cairo_save(ctx);
         cairo_set_operator(ctx, CAIRO_OPERATOR_CLEAR);
         cairo_fill(ctx);
         cairo_restore(ctx);
      }
      else
      {
         //log_debug("ccw: filling with #%08x", d->fill.col);
         cairo_smr_set_source_color(ctx, d->fill.col);
         if (is_closed_poly(w))
            cairo_fill(ctx);
         else
         {
            cairo_set_line_width(ctx, cairo_smr_fill_width(d));
            cairo_stroke(ctx);
         }
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

         render_poly_line(d->ctx, d, (osm_way_t*) o, 0);
         return 0;
      }

      if (!d->directional)
      {
         render_poly_line(d->ctx, d, (osm_way_t*) o, 0);
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

   cairo_pop_group_to_source(d->ctx);
   cairo_paint(d->ctx);

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
      for (i = 0; i < d->wl->ref_cnt; i++)
      {
         log_debug("cw = %d, area = %f", d->wl->ref[i].cw, d->wl->ref[i].area);
         render_poly_line(d->ctx, d, d->wl->ref[i].w, d->wl->ref[i].cw);

      }
      cairo_pop_group_to_source(d->ctx);
      cairo_paint(d->ctx);
   }

   cairo_destroy(d->ctx);
   free(d);
   r->data = NULL;

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
   if ((cap.key = get_param("key", NULL, r->act)) == NULL)
   {
      log_msg(LOG_WARN, "parameter 'key' missing");
      return 1;
   }
   if (*cap.key == '*')
   {
      cap.key++;
      cap.pos |= POS_UC;
   }
   if ((s = get_param("color", NULL, r->act)) != NULL)
      cap.col = parse_color(s);

   parse_auto_rot(r->act, &cap.angle, &cap.rot);

   if ((s = get_param("halign", NULL, r->act)) != NULL)
   {
      if (!strcmp(s, "east"))
         cap.pos |= POS_E;
      else if (!strcmp(s, "west"))
         cap.pos |= POS_W;
      else
         log_msg(LOG_WARN, "unknown alignment '%s'", s);
   }
   if ((s = get_param("valign", NULL, r->act)) != NULL)
   {
      if (!strcmp(s, "north"))
         cap.pos |= POS_N;
      else if (!strcmp(s, "south"))
         cap.pos |= POS_S;
      else
         log_msg(LOG_WARN, "unknown alignment '%s'", s);
   }

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
   cairo_push_group(cap.ctx);

   if ((r->data = malloc(sizeof(cap))) == NULL)
   {
      log_msg(LOG_ERR, "cannot malloc: %s", strerror(errno));
      return -1;
   }

   // activate multi-threading if angle is not "auto"
   if (!isnan(cap.angle))
      sm_threaded(r);

   log_msg(LOG_DEBUG, "%04x, %08x, '%s', '%s', %.1f, %.1f, {%.1f, %08x, %.1f}",
         cap.pos, cap.col, cap.font, cap.key, cap.size, cap.angle,
         cap.rot.phase, cap.rot.autocol, cap.rot.weight);
   memcpy(r->data, &cap, sizeof(cap));
   return 0;
}


static void strupper(char *s)
{
   if (s == NULL)
      return;

   for (; *s != '\0'; s++)
      *s = toupper(/*(unsigned)*/ *s);
}


static void pos_offset(int pos, double width, double height, double *ox, double *oy)
{
   switch (pos & 0x3)
   {
      case POS_N:
         *oy = 0 - POS_OFFSET;
         break;

      case POS_S:
         *oy = height + POS_OFFSET;
         break;

      default:
         *oy = height / 2;
   }
   
   switch (pos & 0xc)
   {
      case POS_E:
         *ox = 0 + POS_OFFSET;
         break;

      case POS_W:
         *ox = -width - POS_OFFSET;
         break;

      default:
         *ox = -width / 2;
   }
   log_debug("pos = %04x, ox = %.2f, oy = %.2f, width = %.2f, height = %.2f", pos, *ox, *oy, width, height);
}


static cairo_surface_t *cairo_smr_cut_out(double x, double y, double r)
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
   cairo_set_source_surface(ctx, sfc_, x, y);
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
   cairo_destroy(ctx);

   return sfc;
}


static inline double min(double a, double b)
{
   return a < b ? a : b;
}


static inline double max(double a, double b)
{
   return a > b ? a : b;
}


static inline double sqr(double a)
{
   return a * a;
}


static uint32_t cairo_smr_double_to_gray(double a)
{
   int c;

   if (a > 1.0) a = 1;
   if (a < 0.0) a = 0.0;

   c = round(a * 255.0);
   return c | c << 8 | c << 16 | 0xff000000;
}


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


static double cairo_smr_dist(cairo_surface_t *dst, cairo_surface_t *src)
{
   unsigned char *psrc, *pdst;
   uint32_t dst_pixel;
   double dist, avg;
   int x, y, mx, my, cnt;

   cairo_surface_flush(src);
   cairo_surface_flush(dst);
   psrc = cairo_image_surface_get_data(src);
   mx = cairo_image_surface_get_width(dst);
   my = cairo_image_surface_get_height(dst);
   pdst = cairo_image_surface_get_data(dst);

   avg = 0;
   cnt = 0;
   for (y = 0; y < my; y++)
   {
      for (x = 0; x < mx; x++)
      {
         // ignore (partially) transparent pixels
         if (ALPHAD(*(((uint32_t*) pdst) + x)) > 0.2 || ALPHAD(*(((uint32_t*) psrc) + x)) > 0.2)
            continue;

         // see http://www.w3.org/TR/AERT#color-contrast and
         // http://www.hgrebdes.com/colour/spectrum/colourvisibility.html and
         // http://colaargh.blogspot.co.uk/2012/08/readability-of-type-in-colour-w3c.html
         // for visibility of colors and the background.
         // Also read this: http://www.lighthouse.org/accessibility/design/accessible-print-design/effective-color-contrast

#ifdef COL_DIFF_BRGT
         dist = fabs(cairo_smr_color_brightness(*(((uint32_t*) pdst) + x)) -
                  cairo_smr_color_brightness(*(((uint32_t*) psrc) + x)));
#endif
#ifdef COL_DIFF_LUM
         dist = fabs(cairo_smr_color_luminosity(*(((uint32_t*) pdst) + x)) -
                  cairo_smr_color_luminosity(*(((uint32_t*) psrc) + x)));
#endif
#ifdef COL_DIFF_3D
         // calculate 3D color distance
         dist = cairo_smr_color_dist(*(((uint32_t*) pdst) + x), *(((uint32_t*) psrc) + x));
#endif

         dst_pixel = cairo_smr_double_to_gray(dist);
         *(((uint32_t*) pdst) + x) = dst_pixel;
         avg += dist;
         cnt++;
      }
      pdst += cairo_image_surface_get_stride(dst);
      psrc += cairo_image_surface_get_stride(src);
   }
   cairo_surface_mark_dirty(dst);
   if (cnt)
      avg /= cnt;
   return avg;
}


//static void cairo_smr_diff(cairo_surface_t *dst, cairo_t *ctx, cairo_surface_t *bg, cairo_surface_t *fg, int x, int y, double a)
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


static double fmod2(double a, double n)
{
   a = fmod(a, n);
   return a < 0 ? a + n : a;
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
      dv[i].dv_diff = cairo_smr_dist(dst, fg);
      dv[i].dv_angle = a;
      dv[i].dv_x = 0;
      dv[i].dv_y = 0;
      dv[i].dv_index = i;
   }

   cairo_destroy(ctx);
   cairo_surface_destroy(dst);
}


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


static double find_angle(const struct coord *c, const struct auto_rot *rot, cairo_surface_t *fg)
{
   diffvec_t *dv;
   cairo_surface_t *sfc;
   diffpeak_t *dp;
   double x, y, a, r;
   int i, num_steps;

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

   if ((sfc = cairo_smr_cut_out(x, y, r)) == NULL)
   {
      log_msg(LOG_ERR, "failed to cut out auto-rotation background");
      free(dv);
      return 0;
   }

   dv_sample(sfc, fg, dv, num_steps);
   cairo_surface_destroy(sfc);

   dv_weight(dv, num_steps, DEG2RAD(rot->phase), rot->weight);
   dv_quantize(dv, num_steps);
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


static int cap_coord(const struct actCaption *cap, const struct coord *c, const bstring_t *str)
{
   cairo_text_extents_t tx;
   cairo_font_extents_t fe;
   cairo_surface_t *pat;
   char buf[str->len + 1];
   double x, y, a, r;
   double width, height;
   short pos;

   cairo_save(cap->ctx);
   geo2pt(c->lon, c->lat, &x, &y);
   cairo_translate(cap->ctx, x, y);

   memcpy(buf, str->buf, str->len);
   buf[str->len] = '\0';
   if (cap->pos & POS_UC)
      strupper(buf);

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

      r = hypot(tx.width + tx.x_bearing, fe.ascent / 2) + POS_OFFSET;
      width = tx.width + tx.x_bearing + POS_OFFSET;
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

      a = find_angle(c, &cap->rot, pat);

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
   }
   else
   {
      a = DEG2RAD(360 - cap->angle);
      pos = cap->pos;
   }

   cairo_rotate(cap->ctx, a);
   pos_offset(pos, tx.width + tx.x_bearing, fe.ascent, &x, &y);
   cairo_move_to(cap->ctx, x, y);
   cairo_show_text(cap->ctx, buf);
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
      tmp_cap.size = 80 * sqrt(fabs(ar) / rdata_square_nm());
#define MIN_AUTO_SIZE 0.7
#define MAX_AUTO_SIZE 12.0
      if (tmp_cap.size < MIN_AUTO_SIZE) tmp_cap.size = MIN_AUTO_SIZE;
      if (tmp_cap.size > MAX_AUTO_SIZE) tmp_cap.size = MAX_AUTO_SIZE;
      //log_debug("r->rule.cap.size = %f (%f 1/1000)", r->rule.cap.size, r->rule.cap.size / 100 * 1000);
   }

   return cap_coord(&tmp_cap, &c, str);
}


int act_cap_main(smrule_t *r, osm_obj_t *o)
{
   struct coord c;
   int n;

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
         return cap_coord(r->data, &c, &o->otag[n].v);

      case OSM_WAY:
         return cap_way(r->data, (osm_way_t*) o, &o->otag[n].v);
   }

   return 1;
}


int act_cap_fini(smrule_t *r)
{
   struct actCaption *cap = r->data;

   cairo_pop_group_to_source(cap->ctx);
   cairo_paint(cap->ctx);
   cairo_destroy(cap->ctx);

   free(cap);
   r->data = NULL;

   return 0;
}

 
int act_img_ini(smrule_t *r)
{
   struct actImage img;
   char *name;
   int e;

   if (r->oo->type != OSM_NODE)
   {
      log_msg(LOG_WARN, "img() only applicable to nodes");
      return -1;
   }

   if ((name = get_param("file", NULL, r->act)) == NULL)
   {
      log_msg(LOG_WARN, "parameter 'file' missing");
      return -1;
   }

   memset(&img, 0, sizeof(img));
   img.img = cairo_image_surface_create_from_png(name);
   if ((e = cairo_surface_status(img.img)) != CAIRO_STATUS_SUCCESS)
   {
      log_msg(LOG_ERR, "cannot open file %s: %s", name, cairo_status_to_string(e));
      //cairo_surface_destroy(img.img);
      return -1;
   }

   img.w = cairo_image_surface_get_width(img.img);
   img.h = cairo_image_surface_get_height(img.img);

   img.ctx = cairo_create(sfc_);
   if ((e = cairo_status(img.ctx)) != CAIRO_STATUS_SUCCESS)
   {
      log_msg(LOG_ERR, "cannot create cairo context: %s", cairo_status_to_string(e));
      cairo_surface_destroy(img.img);
      return -1;
   }
   cairo_scale(img.ctx, PX2PT_SCALE, PX2PT_SCALE);
   cairo_push_group(img.ctx);

   parse_auto_rot(r->act, &img.angle, &img.rot);
  
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


int act_img_main(smrule_t *r, osm_node_t *n)
{
   struct actImage *img = r->data;
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

      x = cairo_image_surface_get_width(img->img);
      y = cairo_image_surface_get_height(img->img);
      cairo_device_to_user(img->ctx, &x, &y);

      a = find_angle(&c, &img->rot, img->img);
   }
   else
   {
      a = DEG2RAD(360 - img->angle);
   }

   cairo_rotate(img->ctx, a);
   cairo_set_source_surface(img->ctx, img->img, img->w / -2.0, img->h / -2.0);
   cairo_paint(img->ctx);
   cairo_restore(img->ctx);

   return 0;
}


int act_img_fini(smrule_t *r)
{
   struct actImage *img = r->data;

   cairo_pop_group_to_source(img->ctx);
   cairo_paint(img->ctx);

   cairo_destroy(img->ctx);
   cairo_surface_destroy(img->img);
   free(img);
   r->data = NULL;
   return 0;
}

#endif

