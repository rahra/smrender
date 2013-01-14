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
//#include "smcoast.h"
//#include "memimg.h"


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

#endif

