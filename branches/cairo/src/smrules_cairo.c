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
//#ifdef WITH_THREADS
//#include <pthread.h>
//#endif
#include <cairo.h>

#include "smrender_dev.h"
#include "smcoast.h"
//#include "memimg.h"

#define COL_COMPD(x,y) ((double) (((x) >> (y)) & 0xff) / 255.0)
#define REDD(x) COL_COMPD(x, 16)
#define GREEND(x) COL_COMPD(x, 8)
#define BLUED(x) COL_COMPD(x, 0)
#define ALPHAD(x) (1.0 - COL_COMPD(x & 0x7f000000, 23))


static cairo_surface_t *sfc_;


static void cairo_set_source_color(cairo_t *ctx, int col)
{
   log_debug("cairo_set_source_rgba(r = %.2f, g = %.2f, b = %.2f, a = %.2f",
         REDD(col), GREEND(col), BLUED(col), ALPHAD(col));
   cairo_set_source_rgba(ctx, REDD(col), GREEND(col), BLUED(col), ALPHAD(col));
}


void init_main_image(struct rdata *rd, const char *bg)
{
   cairo_status_t stat;
   cairo_t *ctx;

   sfc_ = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, rd->w, rd->h);
   if ((stat = cairo_surface_status(sfc_)) != CAIRO_STATUS_SUCCESS)
      log_msg(LOG_ERR, "failed to create cairo surface: %s", cairo_status_to_string(stat)),
         exit(EXIT_FAILURE);

   if (bg != NULL)
      set_color("bgcolor", parse_color(bg));

   ctx = cairo_create(sfc_);
   cairo_set_source_color(ctx, parse_color("bgcolor"));
   //cairo_set_source_rgb(ctx, 1, 1, 1);
   //cairo_set_source_rgba(ctx, 1, 1, 1, 1);
   cairo_paint(ctx);
   cairo_destroy(ctx);

   log_msg(LOG_DEBUG, "background color is set to 0x%08x", parse_color("bgcolor"));
}


static cairo_status_t co_write_func(void *closure, const unsigned char *data, unsigned int length)
{
   return fwrite(data, length, 1, closure) == 1 ? CAIRO_STATUS_SUCCESS : CAIRO_STATUS_WRITE_ERROR;
}


void save_main_image(struct rdata *rd, FILE *f)
{
   cairo_status_t stat;

   log_msg(LOG_INFO, "saving image");
   if ((stat = cairo_surface_write_to_png_stream(sfc_, co_write_func, f)) != CAIRO_STATUS_SUCCESS)
      log_msg(LOG_ERR, "failed to save image: %s", cairo_status_to_string(stat));
}


int save_image(const char *s, void *img, int ftype)
{
   if (!ftype)
      return cairo_surface_write_to_png(img, s) == CAIRO_STATUS_SUCCESS ? 0 : -1;

   // FIXME
   log_msg(LOG_ERR, "other file types than png not implemented yet");
   return -1;
}


int get_pixel(struct rdata *rd, int x, int y)
{
   return 0;
}


void *create_tile(void)
{
   return NULL;
}


void delete_tile(void *img)
{
}


void cut_tile(const struct bbox *bb, void *img)
{
}


void reduce_resolution(struct rdata *rd)
{
}


int act_draw_ini(smrule_t *r)
{
   struct actDraw *d;
   cairo_status_t e;
   double a;
   char *s;
   int i;

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

   //FIXME: not all errors are handled
   for (i = 0; i < 4; i ++)
   {
      d->ctx[i] = cairo_create(sfc_);
      if ((e = cairo_status(d->ctx[i])) != CAIRO_STATUS_SUCCESS)
      {
         log_msg(LOG_ERR, "failed to create cairo surface: %s", cairo_status_to_string(e));
         free(d);
         r->data = NULL;
         return -1;
      }
   }

   if (d->fill.used)
   {
      if (d->fill.width)
      {
         cairo_set_line_width(d->ctx[DCX_OPEN_FILL], mm2pxf(d->fill.width));
         cairo_set_line_width(d->ctx[DCX_CLOSED_FILL], 0);
      }
      else
      {
         cairo_set_line_width(d->ctx[DCX_OPEN_FILL], 1);
         cairo_set_line_width(d->ctx[DCX_CLOSED_FILL], 0);
      }
      cairo_set_source_color(d->ctx[DCX_OPEN_FILL], d->fill.col);
      cairo_set_source_color(d->ctx[DCX_CLOSED_FILL], d->fill.col);
   }

   if (d->border.used)
   {
      if (d->border.width)
      {
         cairo_set_line_width(d->ctx[DCX_OPEN_BORDER], mm2pxf(d->border.width * 2 + d->fill.used * d->fill.width));
         cairo_set_line_width(d->ctx[DCX_CLOSED_BORDER], mm2pxf(d->border.width * 2));
      }
      else
      {
         cairo_set_line_width(d->ctx[DCX_OPEN_BORDER], 1);
         cairo_set_line_width(d->ctx[DCX_CLOSED_BORDER], 1);
      }
      cairo_set_source_color(d->ctx[DCX_OPEN_BORDER], d->border.col);
      cairo_set_source_color(d->ctx[DCX_CLOSED_BORDER], d->border.col);

   }

   sm_threaded(r);

   //log_msg(LOG_DEBUG, "directional = %d, ignore_open = %d", d->directional, !d->collect_open);
   log_msg(LOG_DEBUG, "{%08x, %.1f, %d, %d}, {%08x, %.1f, %d, %d}, %d, %d, %p",
        d->fill.col, d->fill.width, d->fill.style, d->fill.used,
        d->border.col, d->border.width, d->border.style, d->border.used,
        d->directional, d->collect_open, d->wl);

   return 0;
}


int poly_line(const osm_way_t *w, cairo_t *fgctx, cairo_t *bgctx)
{
   int i, first = 0;
   osm_node_t *n;
   double x, y;

   for (i = 0; i < w->ref_cnt; i++)
   {
      if ((n = get_object(OSM_NODE, w->ref[i])) == NULL)
      {
         log_msg(LOG_WARN, "node %ld of way %ld at pos %d does not exist", (long) w->ref[i], (long) w->obj.id, i);
         continue;
      }

      geo2pxf(n->lon, n->lat, &x, &y);

      if (!first)
      {
         if (fgctx != NULL)
            cairo_move_to(fgctx, x, y);
         if (bgctx != NULL)
            cairo_move_to(bgctx, x, y);
         first++;
      }
      else
      {
         if (fgctx != NULL)
            cairo_line_to(fgctx, x, y);
         if (bgctx != NULL)
            cairo_line_to(bgctx, x, y);
      }
   }
   return 0;
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

         poly_line((osm_way_t*) o, d->fill.used ? d->ctx[DCX_OPEN_FILL] : NULL, d->border.used ? d->ctx[DCX_OPEN_BORDER] : NULL);
         cairo_stroke(d->ctx[DCX_OPEN_BORDER]);
         cairo_stroke(d->ctx[DCX_OPEN_FILL]);
         return 0;
      }

      if (!d->directional)
      {
         poly_line((osm_way_t*) o, d->fill.used ? d->ctx[DCX_CLOSED_FILL] : NULL, d->border.used ? d->ctx[DCX_CLOSED_BORDER] : NULL);
         cairo_stroke(d->ctx[DCX_CLOSED_BORDER]);
         cairo_fill(d->ctx[DCX_CLOSED_FILL]);
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


int act_draw_fine(smrule_t *r)
{
   struct actDraw *d = r->data;
   int i;

   for (i = 0; i < 4; i++)
      cairo_destroy(d->ctx[i]);

   free(d);
   r->data = NULL;

   return 0;
}


#endif

