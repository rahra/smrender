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

#include "smconfig.h"

#include "osm_inplace.h"
#include "smlog.h"
//#include "bstring.h"
//#include "bxtree.h"
#include "smath.h"

typedef struct rdata rdata_t;
typedef struct smrule smrule_t;
typedef struct action action_t;

struct smrule
{
   osm_obj_t *oo;
   void *data;       // arbitrary data
   action_t *act;
};

/* smutil.c */
int put_object(osm_obj_t*);
void *get_object(int, int64_t);
int64_t unique_node_id(void);
int64_t unique_way_id(void);
void set_const_tag(struct otag*, char*, char*);
int match_attr(const osm_obj_t*, const char *, const char *);

/* smrparse.c */
char *get_param(const char*, double*, const action_t*);

#endif

