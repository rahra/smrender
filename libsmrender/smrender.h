/* Copyright 2011-2024 Bernhard R. Fischer, 4096R/8E24F29D <bf@abenteuerland.at>
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

/*! \file smrender.h
 * This file contains fundamental definitions for Smrender.
 *
 * @author Bernhard R. Fischer, <bf@abenteuerland.at>
 * @date 2024/01/15
 */
#ifndef SMRENDER_H
#define SMRENDER_H

#include <stdint.h>
#include <syslog.h>

#include "osm_inplace.h"
#include "bxtree.h"
#include "smaction.h"

#if __STDC_VERSION__ < 199901L
#if __GNUC__ >= 2
#define __func__ __FUNCTION__
#else
#define __func__ "<unknown>"
#endif
#endif

#define LOG_WARN LOG_WARNING
#define log_debug(fmt, x...) log_msg(LOG_DEBUG, "%s() " fmt, __func__, ## x)
#define log_warn(x...) log_msg(LOG_WARN, ## x)
#define LOGF_TIME 1
#define LOGF_COLOR 2

#define DEG2RAD(x) ((x) * M_PI / 180.0)
#define RAD2DEG(x) ((x) * 180.0 / M_PI)

#ifdef UNUSED 
#elif defined(__GNUC__) 
# define UNUSED(x) UNUSED_ ## x __attribute__((unused)) 
#elif defined(__LCLINT__) 
# define UNUSED(x) /*@unused@*/ x 
#else 
# define UNUSED(x) x 
#endif

//! format types for coord_str()
typedef enum
{
   LAT_CHAR, LON_CHAR, LAT_DEG, LON_DEG, LAT_DEG_ONLY, LON_DEG_ONLY
} coord_fmt_t;

typedef struct smrule smrule_t;

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
void set_unique_node_id(int64_t);
void set_unique_way_id(int64_t);
void set_const_tag(struct otag*, char*, char*);
int bs_match_attr(const osm_obj_t*, const struct otag *, const struct stag*);
int bs_match(const bstring_t *, const bstring_t *, const struct specialTag *);
int match_attr(const osm_obj_t*, const char *, const char *);
char *get_param_err(const char *, double *, const action_t *, int *);
char *get_param(const char*, double*, const action_t*);
char *get_parami(const char*, int*, const action_t*);
char *get_paraml(const char*, long*, const action_t*);
int get_param_bool2(const char*, const action_t*, int);
int get_param_bool(const char*, const action_t*);
//void set_static_obj_tree(bx_node_t **);
//struct rdata *get_rdata(void);
bx_node_t **get_objtree(void);
int func_name(char *, int , void *);
int put_object0(bx_node_t**, int64_t, void*, int);
void *get_object0(bx_node_t*, int64_t, int);
void *rem_object(int, int64_t);
int coord_str(double, coord_fmt_t, char*, int);
int strcnt(const char*, int);
int realloc_tags(osm_obj_t *, int );
int realloc_refs(osm_way_t *, int );
const char *safe_null_str(const char *);

/* smlog.c */
int log_msg(int, const char*, ...) __attribute__((format (printf, 2, 3)));
int log_errno(int , const char *);
void set_log_flags(int);
void clear_log_flags(int);

/* smthread.c */
void sm_threaded(smrule_t*);
int sm_thread_id(void);
int sm_is_exec_once(const smrule_t *);
int sm_is_exec(const smrule_t *);
void sm_set_exec_once(smrule_t *);
void sm_set_exec(smrule_t *);
void sm_set_flag(smrule_t *, int );
void sm_clear_flag(smrule_t *, int );
int sm_is_flag_set(const smrule_t *, int );

#endif

