/* Copyright 2011 Bernhard R. Fischer, 2048R/5C5FFD47 <bf@abenteuerland.at>
 *
 * This file is part of smfilter.
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
 * along with smfilter. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef OSM_INPLACE_H
#define OSM_INPLACE_H

#include <stdint.h>
#include <time.h>

#include "bstring.h"
#include "libhpxml.h"


//#define LATCL(x) ((int)((x+90.0)*256.0/180.0)&0xff)
//#define LONCL(x) ((int)((x+180.0)*256.0/360.0)&0xff)
//#define NCL(y,x) ((LONCL(x)<<8)|LATCL(y))

#define JAN2004 1072915200

#define get_v(x,y) get_value("v",x,y)

enum {OSM_NA, OSM_NODE, OSM_WAY};


#define SIZEOF_OSM_NODE_S sizeof(struct osm_node)


struct osm_node
{
   // osm data
   int64_t id;
   float lat, lon;
   int ver, cs, uid;
   int vis;
   time_t tim;
//   char act[1024];

   // osmx specific type
   int type;
};

time_t parse_time(bstring_t);
int proc_osm_node(const hpx_tag_t*, struct osm_node*);
struct osm_node *malloc_node(void);
int get_value(const char *k, hpx_tag_t *tag, bstring_t *b);

#endif

