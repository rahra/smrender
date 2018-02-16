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

/*! \file smcore.c
 * This file contains the code of the main execution process.
 *
 *  @author Bernhard R. Fischer
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <inttypes.h>
#include <signal.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>

#include "smrender.h"
#include "smcore.h"
#include "smaction.h"
#include "rdata.h"
#include "lists.h"

extern volatile sig_atomic_t int_;
extern int render_all_nodes_;


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
      switch (r->act->way_type)
      {
         // test if it applies to areas only but way is open
         case ACTION_CLOSED_WAY:
            if (((osm_way_t*) o)->ref_cnt && ((osm_way_t*) o)->ref[0] != ((osm_way_t*) o)->ref[((osm_way_t*) o)->ref_cnt - 1])
               return ERULE_WAYOPEN;
            break;
         // test if it applies to lines only but way is closed
         case ACTION_OPEN_WAY:
            if (((osm_way_t*) o)->ref_cnt && ((osm_way_t*) o)->ref[0] == ((osm_way_t*) o)->ref[((osm_way_t*) o)->ref_cnt - 1])
               return ERULE_WAYCLOSED;
            break;
      }

   // check if tags of rule match tags of object
   for (i = 0; i < r->oo->tag_cnt; i++)
      if (bs_match_attr(o, &r->oo->otag[i], &r->act->stag[i]) == -1)
         return ERULE_NOMATCH;

   // check if object is visible
   if (!o->vis)
      return ERULE_INVISIBLE;

   // call function with this object
   i = r->act->main.func(r, o);
   if (ret != NULL)
      *ret = i;

#ifdef ADD_RULE_TAG
   add_rule_tag(r, o);
#endif
   return 0;
}


int apply_smrules0(osm_obj_t *o, smrule_t *r)
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
   if (r->act->fini.func != NULL && !r->act->finished)
   {
      log_msg(LOG_INFO, "calling rule %016lx, %s_fini", (long) r->oo->id, r->act->func_name);
      if ((e = r->act->fini.func(r)))
         log_debug("_fini returned %d", e);
      r->act->finished = 1;
   }

   return e;
}


#ifdef THREADED_RULES

static list_t *li_fini_;


static void __attribute__((constructor)) init_fini_list(void)
{
   if ((li_fini_ = li_new()) == NULL)
      perror("li_new()"), exit(EXIT_FAILURE);
}


static void __attribute__((destructor)) del_fini_list(void)
{
   li_destroy(li_fini_, NULL);
}


int queue_fini(smrule_t *r)
{
   if ((li_add(li_fini_, r)) == NULL)
   {
      log_msg(LOG_ERR, "li_add() failed: %s", strerror(errno));
      return -1;
   }
   return 0;
}


int dequeue_fini(void)
{
   list_t *elem, *prev;

   log_msg(LOG_INFO, "calling pending _finis");
   for (elem = li_last(li_fini_); elem != li_head(li_fini_); elem = prev)
   {
      li_unlink(elem);
      call_fini(elem->data);
      prev = elem->prev;
      li_del(elem, NULL);
   }

   return 0;
}


#endif


int apply_smrules1(smrule_t *r, long ver, const bx_node_t *ot)
{
   int e = 0;

   if (r == NULL)
   {
      log_msg(LOG_EMERG, "NULL pointer to rule, ignoring");
      return 1;
   }

   if (r->oo->ver != (int) ver)
      return 0;

   if (!r->oo->vis)
   {
      log_msg(LOG_INFO, "ignoring invisible rule %016"PRIx64, r->oo->id);
      return 0;
   }

   // FIXME: wtf is this?
   if (r->act->func_name == NULL)
   {
      log_debug("function has no name");
      return 0;
   }

#ifdef THREADED_RULES
   // if rule is not threaded
   if (!sm_is_threaded(r))
   {
      // wait for all threads (previous rules) to finish
      sm_wait_threads();
      // call finalization functions in the appropriate order
      dequeue_fini();
   }
#endif

   log_msg(LOG_INFO, "applying rule id 0x%"PRIx64" '%s'", r->oo->id, r->act->func_name);

   if (r->act->main.func != NULL)
   {
#ifdef THREADED_RULES
      if (sm_is_threaded(r))
         e = traverse_queue(ot, r->oo->type - 1, (tree_func_t) apply_smrules0, r);
      else
#endif
         e = traverse(ot, 0, r->oo->type - 1, (tree_func_t) apply_smrules0, r);
   }
   else
      log_debug("   -> no main function");

   if (e) log_debug("traverse(apply_smrules0) returned %d", e);

   if (e >= 0)
   {
      e = 0;
#ifdef THREADED_RULES
      queue_fini(r);
#else
      call_fini(r);
#endif
   }

   return e;
}


int apply_smrules(smrule_t *r, long ver)
{
   return apply_smrules1(r, ver, *get_objtree());
}


int execute_rules(bx_node_t *rules, int version)
{
   // FIXME: order rel -> way -> node?
   log_msg(LOG_NOTICE, " relations...");
   traverse(rules, 0, IDX_REL, (tree_func_t) apply_smrules, (void*) (long) version);
#ifdef THREADED_RULES
   sm_wait_threads();
   dequeue_fini();
#endif
   log_msg(LOG_NOTICE, " ways...");
   traverse(rules, 0, IDX_WAY, (tree_func_t) apply_smrules, (void*) (long) version);
#ifdef THREADED_RULES
   sm_wait_threads();
   dequeue_fini();
#endif
   log_msg(LOG_NOTICE, " nodes...");
   traverse(rules, 0, IDX_NODE, (tree_func_t) apply_smrules, (void*) (long) version);
#ifdef THREADED_RULES
   sm_wait_threads();
   dequeue_fini();
#endif
   return 0;
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

   if (int_)
   {
      if (!sig_msg)
      {
         sig_msg = 1;
         log_msg(LOG_NOTICE, "SIGINT catched, breaking rendering recursion");
      }
      return 0;
   }

   if (nt == NULL)
   {
      log_msg(LOG_WARN, "null pointer catched...breaking recursion");
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
            if ((e = dhandler(nt->next[i], p)))
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

