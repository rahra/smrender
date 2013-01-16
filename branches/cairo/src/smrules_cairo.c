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

#define COL_COMPD(x,y) ((double) (((x) >> (y)) & 0xff) / 255)
#define REDD(x) COL_COMPD(x, 16)
#define GREEND(x) COL_COMPD(x, 8)
#define BLUED(x) COL_COMPD(x, 0)
#define ALPHAD(x) COL_COMPD(x & 0x7f000000, 24)


static cairo_surface_t *sfc_;
static struct rdata *rd_;


void init_main_image(struct rdata *rd, const char *bg)
{
   cairo_status_t stat;

   rd_ = rd;

   sfc_ = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, rd->w, rd->h);
   if ((stat = cairo_surface_status(sfc_)) != CAIRO_STATUS_SUCCESS)
      log_msg(LOG_ERR, "failed to create cairo surface: %s", cairo_status_to_string(stat)),
         exit(EXIT_FAILURE);
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
   if ((e = cairo_status(d->ctx)) != CAIRO_STATUS_SUCCESS)
   {
      log_msg(LOG_ERR, "failed to create cairo surface: %s", cairo_status_to_string(e));
      free(d);
      r->data = NULL;
      return -1;
   }
   if (d->fill.used)
   {
      if (d->fill.width)
         // FIXME: MM2PX contains rd. Should somehow be decoupled
         cairo_set_line_width(d->ctx, MM2PX(d->fill.width));
      else
         cairo_set_line_width(d->ctx, 1);
      cairo_set_source_rgba(d->ctx, REDD(d->fill.col), GREEND(d->fill.col), BLUED(d->fill.col), ALPHAD(d->fill.col));
   }

   sm_threaded(r);

   //log_msg(LOG_DEBUG, "directional = %d, ignore_open = %d", d->directional, !d->collect_open);
   log_msg(LOG_DEBUG, "{%08x, %.1f, %d, %d}, {%08x, %.1f, %d, %d}, %d, %d, %p",
        d->fill.col, d->fill.width, d->fill.style, d->fill.used,
        d->border.col, d->border.width, d->border.style, d->border.used,
        d->directional, d->collect_open, d->wl);

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
      if (!d->collect_open && !is_closed_poly((osm_way_t*) o))
         return 0;

      if (d->collect_open && !is_closed_poly((osm_way_t*) o))
      {
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

#endif

