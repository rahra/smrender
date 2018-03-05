/* Copyright 2011-2015 Bernhard R. Fischer, 2048R/5C5FFD47 <bf@abenteuerland.at>
 *
 * This file is part of Smrender (originally Smfilter).
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
 * along with Smrender. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef OSM_INPLACE_H
#define OSM_INPLACE_H

#include <stdint.h>
#include <time.h>

#include "bstring.h"


#define JAN2004 1072915200

#define get_v(x,y) get_value("v",x,y)

enum {OSM_NA, OSM_NODE, OSM_WAY, OSM_REL};
enum {ROLE_NA, ROLE_EMPTY};
#define ROLE_FIRST_FREE_NUM (ROLE_EMPTY+1)


#define SIZEOF_OSM_OBJ(x) ((x)->type == OSM_NODE ? sizeof(osm_node_t) : \
                           (x)->type == OSM_WAY ? sizeof(osm_way_t) : \
                           (x)->type == OSM_REL ? sizeof(osm_rel_t) : 0)


struct otag
{
   bstring_t k, v;
};

typedef struct osm_obj
{
   short type;
   short vis;
   int64_t id;
   int ver, cs, uid;
   time_t tim;
   short tag_cnt;
   struct otag *otag;
} osm_obj_t;

typedef struct osm_node
{
   osm_obj_t obj;
   double lat, lon;
} osm_node_t;

typedef struct osm_way
{
   osm_obj_t obj;
   int ref_cnt;
   int64_t *ref;
} osm_way_t;

struct rmember
{
   short type;
   int64_t id;
   short role;
};

typedef struct osm_rel
{
   osm_obj_t obj;
   short mem_cnt;
   struct rmember *mem;
} osm_rel_t;

typedef union osm_storage
{
   osm_obj_t o;
   osm_node_t n;
   osm_way_t w;
   osm_rel_t r;
} osm_storage_t;


time_t parse_time(bstring_t);
void free_obj(osm_obj_t*);
osm_node_t *malloc_node(short );
osm_way_t *malloc_way(short , int );
osm_rel_t *malloc_rel(short , short );
size_t onode_mem(void);
size_t onode_freed(void);
void osm_obj_default(osm_obj_t *);
void osm_way_default(osm_way_t *);
void osm_node_default(osm_node_t *);
const char *role_str(int );
int strrole(const bstring_t *);
const char *type_str(int );

#endif

