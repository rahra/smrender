/* Copyright 2011-2025 Bernhard R. Fischer, 4096R/8E24F29D <bf@abenteuerland.at>
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
 *  @date 2025/01/23
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

//! Structure to handle thread
typedef struct sm_thread
{
   int (*main)(void*, osm_obj_t*);  //!< function to execute (this.main())
   void *param;            //!< parameter to pass to tree function
   void **obj;             //!< list of objects
   int obj_cnt;            //!< number of elements in object list
   unsigned id;            //!< thread id
   unsigned cnt;           //!< total number of threads
   int result;             //!< result of this.main()
   int status;             //!< state of process (EXEC/WAIT/EXIT)
   pthread_t thandle;      //!< thread handle
   pthread_cond_t cond;    //!< condition of thread
} sm_thread_t;

//! Structure to pass rule and thread info to tree function.
typedef struct smrule_threaded
{
   smrule_t r;             //!< rule per thread, act points to the same in all threads
   void **shared_data;     //!< points to the same r.data as of the "main" thread
   sm_thread_t *th;        //!< pointer to individual thread's th_param_t
} smrule_threaded_t;

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
int find_shared_node_by_rev(osm_obj_t **, void *);

int apply_smrules(smrule_t *, trv_info_t *);
int apply_smrules0(osm_obj_t*, smrule_t*);
int apply_rule(osm_obj_t*, smrule_t*, int*);
int call_fini(smrule_t*);
int call_ini(smrule_t*);


/* smthread.c */
void sm_wait_threads(void);
void obj_queue_ini(int (*)(void*, osm_obj_t*), void *);
int obj_queue(osm_obj_t *);
void obj_queue_signal(void);
int sm_is_threaded(const smrule_t *);
int get_ncpu(void);
int init_threads(int);
int get_thread_id(void);
sm_thread_t *get_th_param(int);
int get_nthreads(void);

#endif

