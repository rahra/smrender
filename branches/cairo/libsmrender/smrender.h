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
#include <syslog.h>

#include "osm_inplace.h"
#include "bxtree.h"
//#include "smath.h"


#define LOG_WARN LOG_WARNING
#define log_debug(x...) log_msg(LOG_DEBUG, ## x)
#define log_warn(x...) log_msg(LOG_WARN, ## x)

#define DEG2RAD(x) ((x) * M_PI / 180.0)
#define RAD2DEG(x) ((x) * 180.0 / M_PI)

#define LAT_CHAR 0
#define LON_CHAR 1
#define LAT_DEG 2
#define LON_DEG 3

//typedef struct rdata rdata_t;
typedef struct smrule smrule_t;
typedef struct action action_t;

struct coord
{
   double lat, lon;
};

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
char *get_param(const char*, double*, const action_t*);
//void set_static_obj_tree(bx_node_t **);
//struct rdata *get_rdata(void);
bx_node_t **get_objtree(void);
int coord_str(double , int , char *, int );

/* smlog.c */
void log_msg(int, const char*, ...) __attribute__((format (printf, 2, 3)));

/* smthread.c */
void sm_threaded(smrule_t*);
int sm_thread_id(void);

#endif

