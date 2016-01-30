/* Copyright 2011-2015 Bernhard R. Fischer, 2048R/5C5FFD47 <bf@abenteuerland.at>
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
 */

#ifndef SMCORE_H
#define SMCORE_H

// return values of apply_rule()
#define ERULE_OUTOFBBOX 1  //!< The node is outside of the area to render.
#define ERULE_WAYOPEN 2    //!< The rule applies only to closed ways.
#define ERULE_WAYCLOSED 3  //!< The rule applies only to open ways.
#define ERULE_NOMATCH 4    //!< The tags of the rule do not match the object.
#define ERULE_INVISIBLE 5  //!< The object is invisible.

#define RULES_TAG "smrender:rules"

typedef int (*tree_func_t)(osm_obj_t*, void*);

// indexes to object tree
enum {IDX_NODE, IDX_WAY, IDX_REL};

/* smcore.c */
int traverse(const bx_node_t*, int, int, tree_func_t, void*);
int execute_rules(bx_node_t *, int );
int rev_index_way_nodes(osm_way_t *, bx_node_t **);
int rev_index_rel_nodes(osm_rel_t *, bx_node_t **);
int get_rev_index(osm_obj_t**, const osm_obj_t*);

/* smthread.c */
void sm_wait_threads(void);
int traverse_queue(const bx_node_t *, int , tree_func_t , void *);
int sm_is_threaded(const smrule_t *);

#endif

