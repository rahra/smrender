/* Copyright 2011-2024 Bernhard R. Fischer, 4096R/8E24F29D <bf@abenteuerland.at>
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

/*! \file smcore.h
 * This file contains the prototypes for the core routines of the execution
 * engine.
 *
 *  @author Bernhard R. Fischer
 *  @data 2024/10/29
 */

#ifndef SMCORE_H
#define SMCORE_H

#define NODES_FIRST 0
#define RELS_FIRST 1

//! return values of apply_rule()
enum {
   ERULE_OUTOFBBOX,     //!< The node is outside of the area to render.
   ERULE_WAYOPEN,       //!< The rule applies only to closed ways.
   ERULE_WAYCLOSED,     //!< The rule applies only to open ways.
   ERULE_NOMATCH,       //!< The tags of the rule do not match the object.
   ERULE_INVISIBLE,     //!< The object is invisible.
   ERULE_EXECUTED       //!< Rule is already exectured.
};

#define RULES_TAG "smrender:rules"

typedef int (*tree_func_t)(osm_obj_t*, void*);

typedef struct trv_info
{
   //! tree of objects to which is to be traversed by each rule
   bx_node_t *objtree;
   //! version of rules to apply
   long ver;
} trv_info_t;

//! Structure to pass parameter to tree function including the thread id.
typedef struct th_param
{
   tree_func_t dhandler;   //!< tree function
   void *param;            //!< parameter to pass to tree function
   int id;                 //!< thread id
   int cnt;                //!< total number of threads
} th_param_t;

// indexes to object tree
enum {IDX_NODE, IDX_WAY, IDX_REL};

/* smcore.c */
int traverse(const bx_node_t*, int, int, tree_func_t, void*);
int execute_treefunc(const bx_node_t*, int, tree_func_t, void *);
int execute_rules0(bx_node_t *, tree_func_t , void *);
int execute_rules(bx_node_t *, int );
int rev_index_way_nodes(osm_way_t *, bx_node_t **);
int rev_index_rel_nodes(osm_rel_t *, bx_node_t **);
int get_rev_index(osm_obj_t**, const osm_obj_t*);
int insert_refs(osm_way_t *, osm_node_t **, int, int);

int apply_smrules(smrule_t *, trv_info_t *);
int apply_smrules0(osm_obj_t*, smrule_t*);
int apply_rule(osm_obj_t*, smrule_t*, int*);
int apply_rule_threaded(osm_obj_t*, th_param_t*);
int call_fini(smrule_t*);
int call_ini(smrule_t*);

/* smthread.c */
void sm_wait_threads(void);
int traverse_queue(const bx_node_t *, int, int, tree_func_t, void *);
int sm_is_threaded(const smrule_t *);

#endif

