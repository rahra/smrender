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

/*! \file smcore.c
 * This file contains the code of the main execution process.
 *
 *  \author Bernhard R. Fischer, <bf@abenteuerland.at>
 *  \date 2025/01/23
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

// Measure traversal and execution time. Works only in single-threaded mode.
//#define DEBUG_T_TRV

#include <inttypes.h>
#include <signal.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/time.h>   // gettimeofday

#include "smrender.h"
#include "smcore.h"
#include "smaction.h"
#include "rdata.h"
#include "lists.h"

extern volatile sig_atomic_t int_;
volatile sig_atomic_t alarm_;
extern int render_all_nodes_;
//! Number of seconds after a message is logged during a traverse. This may be useful for huge datasets.
int traverse_alarm_ = 60;
#ifdef DEBUG_T_TRV
static uint64_t t_trv_ = 0, t_exec_ = 0;
#endif
#define DEBUG_T_APPLY
#ifdef DEBUG_T_APPLY
static uint64_t t_apply_ = 0;    //!< to measure execution time (only relevant for stats)
#endif

#ifdef DEBUG_T_TRV
void __attribute__((destructor)) print_trv_time(void)
{
   log_msg(LOG_DEBUG, "t_trv_ = %lu.%lu, t_exec_ = %lu.%lu", t_trv_ / 1000000, (t_trv_ % 1000000) / 1000, t_exec_ / 1000000, (t_exec_ % 1000000) / 1000);
}
#endif

//#define ADD_RULE_TAG
#ifdef ADD_RULE_TAG
/*FIXME: The following code does not work yet because at some place tags are
 * copied from one object to another. Those are shallow copies, thus the string
 * pointers of the rules tag point to the same area and are thus overwritten
 * from one object to another. Also copied tags and realloc() may render
 * pointer duplicates inaccessible (because of memory reallocation). A
 * different solution is to be found...
 */


/*! Compare string s2 to the initial part of bstring_t s1, i.e. strlen(s2) number of
 * characters.
 */
static int bs_ncmp2(const bstring_t *s1, const char *s2)
{
   for (int i = 0; i < s1->len && *s2; i++, s2++)
      if (s1->buf[i] != *s2)
         return s1->buf[i] - *s2;
   if (!*s2)
      return 0;
   return -*s2;
}


static int check_rule_tag(const osm_obj_t *o, bstring_t b)
{
   long id;
   char c;
   int i;

   if (bs_ncmp2(&b, type_str(o->type)))
   {
      log_debug("rule type mismatch of %s %"PRId64", tag = '%.*s'", type_str(o->type), o->id, b.len, b.buf);
      return -1;
   }
   i = strlen(type_str(o->type));
   if (b.len <= i)
   {
      log_debug("rule tag truncated of '%s' %"PRId64, type_str(o->type), o->id);
      return -1;
   }
   bs_nadvance(&b, i);
   if (*b.buf != '=')
   {
      log_debug("syntax error in rule tag truncated of '%s' %"PRId64, type_str(o->type), o->id);
      return -1;
   }
   bs_advance(&b);
   if (!b.len)
   {
      log_debug("syntax error in rule tag truncated of '%s' %"PRId64, type_str(o->type), o->id);
      return -1;
   }
   if (*b.buf == '-')
   {
      if (b.len <= 1)
      {
         log_debug("syntax error in rule tag truncated of '%s' %"PRId64, type_str(o->type), o->id);
         return -1;
      }
      c = b.buf[1];
   }
   else
      c = *b.buf;
   if (c < '0' || c > '9')
   {
      log_debug("syntax error in rule tag truncated of '%s' %"PRId64, type_str(o->type), o->id);
      return -1;
   }
   id = bs_tol(b);
   if (id != o->id)
   {
      log_debug("id mismatch in rule tag of %"PRId64" (is %"PRId64")", o->id, id);
      return -1;
   }
   return 0;
}


int alloc_init_rule_tag(const osm_obj_t *o, bstring_t *b)
{
   char *s;
   if ((s = malloc(32)) == NULL)
   {
      log_errno(LOG_ERR, "malloc() failed");
      return -1;
   }
   b->buf = s;
   if ((b->len = snprintf(b->buf, 32, "%s=%"PRId64, type_str(o->type), o->id)) == -1)
   {
      log_msg(LOG_EMERG, "snprintf() failed. WTF?");
      b->len = 0;
   }
   return 0;
}

 
static int add_rule_tag(const smrule_t *r, osm_obj_t *o)
{
   char buf[64];
   int n, plen;
   bstring_t b;

   if ((n = match_attr(o, RULES_TAG, NULL)) >= 0)
   {

      b = o->otag[n].v;
      if (!b.len)
      {
         log_msg(LOG_EMERG, "Rules-tag without content! This may indicate a bug.");
         return -1;
      }
      if (check_rule_tag(o, b))
      {
         log_debug("reallocating rule tag for %s %"PRId64, type_str(o->type), o->id);
         if (alloc_init_rule_tag(o, &b) == -1)
            return -1;
      }
   }
   else
   {
      if ((n = realloc_tags(o, o->tag_cnt + 1)) == -1)
      {
         log_errno(LOG_ERR, "realloc_tags() failed");
         return -1;
      }

      // safety check only
      if (n != o->tag_cnt - 1) log_msg(LOG_ERR, "this should not happen!");

      b.len = o->otag[n].v.len = 0;
      b.buf = o->otag[n].v.buf = NULL;

      o->otag[n].k.buf = RULES_TAG;
      o->otag[n].k.len = strlen(o->otag[n].k.buf);

      if (alloc_init_rule_tag(o, &b) == -1)
         return -1;
   }

   // FIXME: constant shouldn't be hardcoded
   if ((plen = snprintf(buf, sizeof(buf), "%"PRId64, r->oo->id & 0x000000ffffffffff)) == -1)
   {
      log_msg(LOG_ERR, "snprintf() returned -1");
      return -1;
   }
 
   // allocate string memory for oldstring + newstring + ';'
   char *tmp;
   if ((tmp = realloc(b.buf, b.len + plen + 1)) == NULL)
   {
      log_errno(LOG_ERR, "realloc() failed");
      return -1;
   }
   b.buf = tmp;

   if (b.len)
      b.buf[b.len++] = ';';
   memcpy(b.buf + b.len, buf, plen);
   b.len += plen;

   o->otag[n].v = b;

   log_debug("%s, id = %"PRId64", v = '%.*s'", type_str(o->type), o->id, b.len, b.buf);
   return 0;
} 
#endif


/*! Match and apply ruleset to object if it is visible.
 *  @param o Object which should be rendered (to which to action is applied).
 *  @param r Rule.
 *  @param ret This variable receives the return value of the rule's
 *  act_XXX_main() function. It may be set to NULL.
 *  @return If the act_XXX_main() function was called 0 is returned and its
 *  return value will be stored to ret. If the rule's main function was not
 *  called, a positive integer is returned which defines the reason for
 *  act_XXX_main() not being called. These reasons are defined as cpp macros
 *  named ERULE_xxx.
 */
int apply_rule(osm_obj_t *o, smrule_t *r, int *ret)
{
   int i;

   // render only nodes which are on the page
   if (!render_all_nodes_ && o->type == OSM_NODE)
   {
      struct coord c;
      c.lon = ((osm_node_t*) o)->lon;
      c.lat = ((osm_node_t*) o)->lat;
      if (!is_on_page(&c))
         return ERULE_OUTOFBBOX;
   }

   // check if way rule applies to either areas (closed ways) or lines (open
   // ways)
   if (r->oo->type == OSM_WAY)
   {
      // test if it applies to areas only but way is open
      if (sm_is_flag_set(r, ACTION_CLOSED_WAY))
      {
         if (((osm_way_t*) o)->ref_cnt && ((osm_way_t*) o)->ref[0] != ((osm_way_t*) o)->ref[((osm_way_t*) o)->ref_cnt - 1])
            return ERULE_WAYOPEN;
      }
      // test if it applies to lines only but way is closed
      else if (sm_is_flag_set(r, ACTION_OPEN_WAY))
      {
         if (((osm_way_t*) o)->ref_cnt && ((osm_way_t*) o)->ref[0] == ((osm_way_t*) o)->ref[((osm_way_t*) o)->ref_cnt - 1])
            return ERULE_WAYCLOSED;
      }
      // else it doesn't matter if it's open or closed
   }

   // check if tags of rule match tags of object
   for (i = 0; i < r->oo->tag_cnt; i++)
      if (bs_match_attr(o, &r->oo->otag[i], &r->act->stag[i]) == -1)
         return ERULE_NOMATCH;

   // check if object is visible
   if (!o->vis)
      return ERULE_INVISIBLE;

   if (sm_is_flag_set(r, ACTION_EXEC_ONCE) && sm_is_flag_set(r, ACTION_EXEC))
      return ERULE_EXECUTED;

   // call function with this object
#ifdef TH_OBJ_LIST
   if (get_nthreads() > 0 && sm_is_threaded(r))
      i = obj_queue(o);
   else
#endif
   {
      ((smrule_threaded_t*) r)->th->call_cnt++;
      i = r->act->main.func(r, o);
   }
   if (ret != NULL)
      *ret = i;

   sm_set_flag(r, ACTION_EXEC);

#ifdef ADD_RULE_TAG
   add_rule_tag(r, o);
#endif
   return 0;
}


int apply_rule0(osm_obj_t *o, smrule_t *r)
{
   int ret = 0;

   (void) apply_rule(o, r, &ret);
   return ret;
}


int call_fini(smrule_t *r)
{
   int e = 0;

   // safety check
   if (r == NULL)
   {
      log_msg(LOG_ERR, "r == NULL, this should not happen (at least in a singled-threaded env)");
      return e;
   }
   // safety check
   if (r->act == NULL)
   {
      log_msg(LOG_ERR, "r->act == NULL, this should never happen!");
      return e;
   }

   // call de-initialization rule of function rule if available
   if (r->act->fini.func != NULL && !sm_is_flag_set(r, ACTION_FINISHED))
   {
#ifdef DEBUG_T_APPLY
      unsigned acnt = ((smrule_threaded_t*)r)->th->call_cnt;
#endif
      // if it is threaded execution and rule is threaded, fini remaining thread rules
      int nth = get_nthreads();
      if (nth > 0 && sm_is_threaded(r))
      {
         smrule_threaded_t *rth = ((smrule_threaded_t*) r) - nth;
         for (int i = nth - 1; i > 0; i--)
         {
            log_msg(LOG_INFO, "calling rule %016lx, %s_fini()[%d]", (long) r->oo->id, r->act->func_name, i);
            if ((e = r->act->fini.func(&rth[i].r)))
               log_debug("%s_fini()[%d] returned %d", r->act->func_name, i, e);
            log_debug("main() was called %u times", rth[i].th->call_cnt);
#ifdef DEBUG_T_APPLY
            acnt += rth[i].th->call_cnt;
#endif
         }
      }

      log_msg(LOG_INFO, "calling rule %016lx, %s_fini()[%d]", (long) r->oo->id, r->act->func_name, 0);
      if ((e = r->act->fini.func(r)))
         log_debug("%s_fini()[%d] returned %d", r->act->func_name, 0, e);
      log_debug("main() was called %u times", ((smrule_threaded_t*)r)->th->call_cnt);
#ifdef DEBUG_T_APPLY
      log_debug("exec stats: %016lx: %s() acnt = %u, t_apply_ = %.3f ms, %.3f us", (long) r->oo->id, r->act->func_name, acnt, t_apply_ / 1000.0, (double) t_apply_ / acnt);
#endif
      sm_set_flag(r, ACTION_FINISHED);
   }

   return e;
}


int call_ini(smrule_t *r)
{
   int e = 0;

   if (r->act->ini.func != NULL)
   {
      log_msg(LOG_DEBUG, "calling %s_ini()[%d]", r->act->func_name, 0);
      e = r->act->ini.func(r);

      // if _ini was successful and it is threaded execution and rule is threaded init remaining thread rules
      int nth = get_nthreads();
      if (!e && nth > 0 && sm_is_threaded(r))
      {
         smrule_threaded_t *rth = ((smrule_threaded_t*) r) - nth;
         rth[0].r.oo = r->oo;
         rth[0].r.data = r->data;
         for (int i = 1; i < nth; i++)
         {
            log_msg(LOG_DEBUG, "calling %s_ini()[%d]", r->act->func_name, i);
            rth[i].r.oo = r->oo;
            e = r->act->ini.func(&rth[i].r);
            if (e)
               log_msg(LOG_ERR, "%s_ini()[%d] returned %d.", r->act->func_name, i, e);
         }
      }
      else if (e < 0)
      {
         log_msg(LOG_ERR, "%s_ini() failed: %d. Exiting.", r->act->func_name, e);
      }
      else if (e > 0)
      {
         log_msg(LOG_ERR, "%s_ini() failed: %d. Rule will be ignored.", r->act->func_name, e);
         r->act->main.func = NULL;
         r->act->fini.func = NULL;
         e = 0;
      }
   }

   return e;
}


int apply_smrules(smrule_t *r, trv_info_t *ti)
{
   int e = 0;

   if (r == NULL)
   {
      log_msg(LOG_EMERG, "NULL pointer to rule, ignoring");
      return 1;
   }

   if (r->oo->ver != ti->ver)
      return 0;

   if (!r->oo->vis)
   {
      log_msg(LOG_INFO, "ignoring invisible rule %016"PRIx64, r->oo->id);
      return 0;
   }

   if (sm_is_flag_set(r, ACTION_FINISHED) && !sm_is_flag_set(r, ACTION_EXEC_ONCE))
   {
      log_debug("action is reentered");
      call_ini(r);
      sm_clear_flag(r, ACTION_FINISHED);
   }

   // FIXME: wtf is this?
   if (r->act->func_name == NULL)
   {
      log_debug("function has no name");
      return 0;
   }

   log_msg(LOG_INFO, "applying rule id 0x%"PRIx64" '%s'", r->oo->id, r->act->func_name);

   if (r->act->main.func != NULL)
   {
#ifdef TH_OBJ_LIST
      obj_queue_ini(r->act->main.func, r);
#endif
#ifdef DEBUG_T_APPLY
      struct timeval tv;
      gettimeofday(&tv, NULL);
      t_apply_ = tv.tv_usec + tv.tv_sec * 1000000;
#endif
      e = traverse(ti->objtree, 0, r->oo->type - 1, (tree_func_t) apply_rule0, r);
#ifdef TH_OBJ_LIST
      obj_queue_signal();
      sm_wait_threads();
#endif
#ifdef DEBUG_T_APPLY
      gettimeofday(&tv, NULL);
      t_apply_ = tv.tv_usec + tv.tv_sec * 1000000 - t_apply_;
#endif
   }
   else
      log_debug("   -> no main function");

   if (e) log_debug("traverse(apply_rule0) returned %d", e);

   if (e >= 0)
   {
      e = 0;
      call_fini(r);
   }

   return e;
}


/*! This function calls traverse() 3 times subsquently, with IDX_NODE, IDX_WAY,
 * and IDX_REL if dir is set to NODES_FIRST. If dir is set to RELS_FIRST,
 * traverse() will be called with IDX_REL first.
 * @param nt Pointer to the tree to be traversed.
 * @oaran dir Order of execution which is either NODES_FIRST or RELS_FIRST.
 * @param dhandler Function to be called for each object.
 * @param p Optional argument which is passed to dhandler(p).
 * @return The function returns the return value of traverse().
 */
int execute_treefunc(const bx_node_t *nt, int dir, tree_func_t dhandler, void *p)
{
#define NUM_OBJ_INDEX 3
   int e, i, j;

   for (i = 0, e = 0; i < NUM_OBJ_INDEX && !e; i++)
   {
      j = dir == NODES_FIRST ? i : NUM_OBJ_INDEX - 1 - i;
      log_msg(LOG_INFO, "%ss...", type_str(j + 1));
      e = traverse(nt, 0, j, dhandler, p);
   }

   return e;
}


int execute_rules0(bx_node_t *rules, tree_func_t func, void *p)
{
   return execute_treefunc(rules, RELS_FIRST, func, p);
}

 
/* This function traverses the rules and for each rule traverses the objects.
 * execute_rules() -> traverse(apply_smrules()) -> traverse(apply_rule0()) -> apply_rule()
*/
int execute_rules(bx_node_t *rules, int version)
{
   trv_info_t ti = {*get_objtree(), version};
   return execute_treefunc(rules, RELS_FIRST, (tree_func_t) apply_smrules, &ti);
}


/*! Recursively traverse tree and call function dhandler for all leaf nodes
 * which are not NULL.
 * @param nt Pointer to tree root.
 * @param d Depth of traversal. This should be 0 if called at the root of the
 * tree.
 * @param idx The tree actually allows to be an overlapped tree of several
 * trees. idx is the index of the tree which should be traversed.
 * @param dhandler This is the function called at the leaf node.
 * @param p This is a generic argument which is passed to dhandler upon call.
 * @return On success 0 is returned. If dhandler() returns a value != 0
 * traverse() will break the recursion and return the return value of
 * dhandler().
 */
int traverse(const bx_node_t *nt, int d, int idx, tree_func_t dhandler, void *p)
{
   int i, e, sidx, eidx;
   static int sig_msg = 0;
   char buf[32];
   static long _leaf_cnt;
#ifdef DEBUG_T_TRV
   struct timeval tv;
   static uint64_t _t_last, _t_cur;
#endif

   // handle CTRL-C
   if (int_)
   {
      if (!sig_msg)
      {
         sig_msg = 1;
         log_msg(LOG_NOTICE, "SIGINT caught, breaking rendering recursion");
      }
      return 0;
   }

   // handle first entrance of traverse
   if (!d)
   {
      alarm(traverse_alarm_);
      _leaf_cnt = 0;
#ifdef DEBUG_T_TRV
      gettimeofday(&tv, NULL);
      _t_last = tv.tv_sec * 1000000 + tv.tv_usec;
#endif
   }

   // handler timer alarm
   if (alarm_)
   {
      alarm(traverse_alarm_);
      alarm_ = 0;
      if (idx >= 0 && idx < 4)
         log_msg(LOG_INFO, "traverse(nt = %p, d =%d, idx = %d), _leaf_cnt = %ld, %.1f%%", nt, d, idx, _leaf_cnt, 100.0 * _leaf_cnt / get_rdata()->ds.cnt[idx]);
      else
         log_msg(LOG_INFO, "traverse(nt = %p, d =%d, idx = %d), _leaf_cnt = %ld", nt, d, idx, _leaf_cnt);
   }

   if (nt == NULL)
   {
      log_msg(LOG_WARN, "null pointer caught...breaking recursion");
      return -1;
   }

   if ((idx < -1) || (idx >= (1 << BX_RES)))
   {
      log_msg(LOG_CRIT, "traverse(): idx (%d) out of range", idx);
      return -1;
   }

   if (d == sizeof(bx_hash_t) * 8 / BX_RES)
   {
      if (idx == -1)
      {
         sidx = 0;
         eidx = 1 << BX_RES;
      }
      else
      {
         sidx = idx;
         eidx = sidx + 1;
      }

      for (i = sidx, e = 0; i < eidx; i++)
      {
         if (nt->next[i] != NULL)
         {
            _leaf_cnt++;

#ifdef DEBUG_T_TRV
            gettimeofday(&tv, NULL);
            _t_cur = tv.tv_sec * 1000000 + tv.tv_usec;
            t_trv_ += _t_cur - _t_last;
            _t_last = _t_cur;
#endif

            e = dhandler(nt->next[i], p);

#ifdef DEBUG_T_TRV
            gettimeofday(&tv, NULL);
            _t_cur = tv.tv_sec * 1000000 + tv.tv_usec;
            t_exec_ += _t_cur - _t_last;
            _t_last = _t_cur;
#endif

            if (e)
            {
               (void) func_name(buf, sizeof(buf), dhandler);
               log_msg(LOG_WARNING, "dhandler(), sym = '%s', addr = '%p' returned %d", buf, dhandler, e);
               if (e)
               {
                  log_msg(LOG_INFO, "breaking recursion");
                  return e;
               }
            }
         }
      }
      return e;
   }

   for (i = 0; i < 1 << BX_RES; i++)
      if (nt->next[i])
      {
         if ((e = traverse(nt->next[i], d + 1, idx, dhandler, p)))
            return e;
      }

   // disable timer before exit of last traverseal
   if (!d)
   {
      alarm(0);
#ifdef DEBUG_T_TRV
      gettimeofday(&tv, NULL);
      _t_cur = tv.tv_sec * 1000000 + tv.tv_usec;
      t_trv_ += _t_cur - _t_last;
      _t_last = _t_cur;
#endif
   }

   return 0;
}


/*! Find OSM object in object list and return index.
 *  @param optr Pointer to NULL-terminated object list.
 *  @param o Pointer to object.
 *  @return Index greater or equal 0 of object within list. If the list has no
 *  such object the index of the last element (which is a NULL pointer) is
 *  returned. If optr itself is a NULL pointer -1 is returned.
 */
int get_rev_index(osm_obj_t **optr, const osm_obj_t *o)
{
   int i;

   if (optr == NULL)
      return -1;
   for (i = 0; *optr != NULL; optr++, i++)
      if (o == *optr)
         break;
   return i;
}


/*! Add object to reverse pointer index.
 * @param idx_root Pointer to reverse object pointer index.
 * @param id Id of object to add.
 * @param o Pointer to the parent of the object.
 * @return Returns the index of the added object in the index list.
 */
int add_rev_ptr(bx_node_t **idx_root, int64_t id, int idx, osm_obj_t *o)
{
   osm_obj_t **optr;
   int n;

   // get index pointer
   if ((optr = get_object0(*idx_root, id, idx)) == NULL)
   {
      n = 0;
   }
   else
   {
      n = get_rev_index(optr, o);
      // check if index exists
      if (optr[n] != NULL)
         return 1;
   }

   if ((optr = realloc(optr, sizeof(*optr) * (n + 2))) == NULL)
   {
      log_msg(LOG_ERR, "could not realloc() in rev_index_way_nodes(): %s", strerror(errno));
      return -1;
   }
   optr[n] = o;
   optr[n + 1] = NULL;
   put_object0(idx_root, id, optr, idx);
   return 0;
}

 
/*! Add all nodes of a way to the reverse pointer index.
 * @param w Pointer to way.
 * @param idx_root Pointer to reverse object pointer index.
 * @return On success 0 is returned, otherwise -1 (if add_rev_ptr() failes).
 */
int rev_index_way_nodes(osm_way_t *w, bx_node_t **idx_root)
{
   int i;

   for (i = 0; i < w->ref_cnt; i++)
   {
      if (get_object(OSM_NODE, w->ref[i]) == NULL)
      {
         log_msg(LOG_ERR, "node %ld in way %ld does not exist", (long) w->ref[i], (long) w->obj.id);
         continue;
      }

      if (add_rev_ptr(idx_root, w->ref[i], IDX_NODE, (osm_obj_t*) w) == -1)
         return -1;
   }
   return 0;
}


/*! Add all nodes of a relation to the reverse pointer index.
 * @param r Pointer to relation.
 * @param idx_root Pointer to reverse object pointer index.
 * @return On success 0 is returned, otherwise -1 is returned and errno is set.
 */
int rev_index_rel_nodes(osm_rel_t *r, bx_node_t **idx_root)
{
   int i, incomplete = 0;
   osm_obj_t *o;

   for (i = 0; i < r->mem_cnt; i++)
   {
      if ((o = get_object(r->mem[i].type, r->mem[i].id)) == NULL)
      {
         //log_msg(LOG_ERR, "object %ld in relation %ld does not exist", (long) r->mem[i].id, (long) r->obj.id);
         incomplete++;
         continue;
      }

      if (add_rev_ptr(idx_root, r->mem[i].id, r->mem[i].type - 1, (osm_obj_t*) r) == -1)
         return -1;
   }

   if (incomplete)
      log_msg(LOG_NOTICE, "relation %ld incomplete, %d objects missing", (long) r->obj.id, incomplete);

   return 0;
}


/*! Add nodes (refs) to way and update reverse pointer index.
 * @param w Pointer to way.
 * @param n Pointer to array of nodes.
 * @param n_cnt Number of nodes in array.
 * @param pos Index within ref list of way at which the new nodes shall be
 * inserted.
 * @return On success 0 is returned, otherwise -1 is returned and errno is set.
 */
int insert_refs(osm_way_t *w, osm_node_t **n, int n_cnt, int pos)
{
   log_debug("inserting nodes into way %"PRId64" at index %d", w->obj.id, pos);
   // reallocate ref list for new nodes
   if (realloc_refs(w, w->ref_cnt + n_cnt) == -1)
      return -1;

   // insert new nodes into ref list
   memmove(&w->ref[pos + n_cnt], &w->ref[pos], (w->ref_cnt - pos - n_cnt) * sizeof(*w->ref));
   for (int i = 0; i < n_cnt; i++)
   {
      w->ref[i + pos] = n[i]->obj.id;
      if (add_rev_ptr(&get_rdata()->index, w->ref[i + pos], IDX_NODE, (osm_obj_t*) w) == -1)
         return -1;
   }

   return 0;
}


/*! This function lists all parent ids if the object is shared by more than 1
 * parent.
 * The function is a tree function and is to be called by traverse() on the
 * index tree (rd->index).
 */
int find_shared_node_by_rev(osm_obj_t **optr, void *UNUSED(p))
{
   char buf[1024];
   int len;

   if (optr == NULL || optr[0] == NULL || optr[1] == NULL)
      return 0;

   len = snprintf(buf, sizeof(buf), "node is member of ");
   for (; *optr != NULL; optr++)
   {
      len += snprintf(&buf[len], sizeof(buf) - len, "%ld, ", (*optr)->id);
      if (len >= (int) sizeof(buf))
         break;
   }
   log_msg(LOG_NOTICE, "%s", buf);

   return 0;
}

