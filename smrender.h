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

#include "osm_inplace.h"
#include "bstring.h"
//#include "libhpxml.h"
//#include "smlog.h"
#include "bxtree.h"
//#include "smact.h"
//#include "smrules.h"


struct otag
{
   bstring_t k;
   bstring_t v;
};

struct onode
{
   struct osm_node nd;
   int ref_cnt;
   int64_t *ref;
   int tag_cnt;
   struct otag otag[];
};

struct rdata
{
   bx_node_t *nodes;
   bx_node_t *ways;
   gdImage *img;
   double x1c, y1c, x2c, y2c, wc, hc;
   double mean_lat, mean_lat_len;
   int w, h;
   int dpi;
   double scale;
   int col[5];
   struct smevent *ev;
};

enum {WHITE, YELLOW, BLACK, BLUE, VIOLETT};
enum {LAT, LON};

// select projection
// PRJ_DIRECT directly projects the bounding box onto the image.
// PRJ_MERC_PAGE chooses the largest possible bb withing the given bb to fit into the image.
// PRJ_MERC_BB chooses the image size being selected depending on the given bb.
enum {PRJ_DIRECT, PRJ_MERC_PAGE, PRJ_MERC_BB};


struct actImage
{
   char *fn;
   gdImage *img;
};

struct actCaption
{
   int pos;
   int family;
   int type;
   double size;
};

struct actFunction
{
   char *lib;
   int (*func)(const struct rdata*, const struct onode*);
};


struct smrule
{
   struct smrule *next;
   char *k;
   char *v;
};


struct smevent
{
   struct smevent *next;
   struct smrule *rule;
   int type;
   union
   {
      struct actImage img;
      struct actCaption cap;
      struct actFunction func;
   };
};

int read_osm_file(hpx_ctrl_t *, bx_node_t **, bx_node_t **);

struct smevent *dummy_load(void);


#endif
