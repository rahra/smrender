/* Copyright 2011 Bernhard R. Fischer, 2048R/5C5FFD47 <bf@abenteuerland.at>
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

/*! This file contains 'utility' code. This is getting and putting object from
 * and into the object tree, some bstring_t functions (which are not found in
 * bstring.c), and the matching functions which are applied to the objects
 * before execution the rules' actions.
 *
 *  @author Bernhard R. Fischer
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <limits.h>
#include <string.h>
#include <syslog.h>
#include <errno.h>
#include <math.h>
#ifdef WITH_THREADS
#include <pthread.h>
#endif
#ifdef HAVE_DLFCN_H
#define __USE_GNU
#include <dlfcn.h>
#endif

#include "smrender.h"
#include "bxtree.h"
#include "smaction.h"


static bx_node_t *obj_tree_ = NULL;


bx_node_t **get_objtree(void)
{
   return &obj_tree_;
}


/*! Reallocate tag memory for cnt number of tags. The newly added tags (if cnt
 * > o->tag_cnt) or NOT initialized!
 * @param o Pointer to OSM object.
 * @param cnt Total number of tags.
 * @return The function returns the number of tags before the reallocation. The
 * member o->tag_cnt is set to cnt. On error -1 is returned. See realloc(3) for
 * possible errors.
 */
int realloc_tags(osm_obj_t *o, int cnt)
{
   struct otag *new_tags;
   int ocnt;

   if ((new_tags = realloc(o->otag, cnt * sizeof(*o->otag))) == NULL)
      return -1;
   o->otag = new_tags;
   ocnt = o->tag_cnt;
   o->tag_cnt = cnt;
   return ocnt;
}


void set_const_tag(struct otag *tag, char *k, char *v)
{
   tag->k.buf = k;
   tag->k.len = strlen(k);
   tag->v.buf = v;
   tag->v.len = strlen(v);
}


#define UNIQUE_ID_START UINT64_C(-100000000000)
int64_t unique_node_id(void)
{
   static int64_t uid = UNIQUE_ID_START;
#ifdef WITH_THREADS
   static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
   int64_t id;

   pthread_mutex_lock(&mutex);
   id = uid--;
   pthread_mutex_unlock(&mutex);
   return id;
#else
   return uid--;
#endif
}


int64_t unique_way_id(void)
{
   static int64_t uid = UNIQUE_ID_START;
#ifdef WITH_THREADS
   static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
   int64_t id;

   pthread_mutex_lock(&mutex);
   id = uid--;
   pthread_mutex_unlock(&mutex);
   return id;
#else
   return uid--;
#endif
}


int put_object0(bx_node_t **tree, int64_t id, void *p, int idx)
{
   bx_node_t *bn;

   if ((idx < 0) || (idx >= (1 << BX_RES)))
   {
      log_msg(LOG_ERR, "index to tree node out of range: %d", idx);
      return -1;
   }

   if ((bn = bx_add_node(tree, id)) == NULL)
   {
      log_msg(LOG_ERR, "bx_add_node() failed in put_object0()");
      return -1;
   }
   /* too much debugging....
   if (bn->next[0] != NULL)
   {
      log_msg(LOG_DEBUG, "nt->next[0] contains valid pointer, overwriting.");
   }*/

   bn->next[idx] = p;
   return 0;
}


int put_object(osm_obj_t *o)
{
   /*if (obj_tree_ == NULL)
   {
      log_msg(LOG_EMERG, "static object tree unset in libsmrender. Call set_static_obj_tree()!");
      return -1;
   }*/
 
   return put_object0(&obj_tree_, o->id, o, o->type - 1);
}


void *get_object0(bx_node_t *tree, int64_t id, int idx)
{
   bx_node_t *bn;

   if ((idx < 0) || (idx >= (1 << BX_RES)))
   {
      log_msg(LOG_ERR, "get_object0(): index (%d) to tree node out of range.", idx);
      return NULL;
   }

   if ((bn = bx_get_node(tree, id)) == NULL)
   {
      //log_msg(LOG_ERR, "bx_get_node() failed");
      return NULL;
   }
   if (bn->next[idx] == NULL)
   {
      //log_msg(LOG_ERR, "nt->next[0] contains NULL pointer");
      return NULL;
   }

   return bn->next[idx];
}


void *get_object(int type, int64_t id)
{
   /*if (obj_tree_ == NULL)
   {
      log_msg(LOG_EMERG, "static object tree unset in libsmrender. Call set_static_obj_tree()!");
      return NULL;
   }*/
   return get_object0(obj_tree_, id, type - 1);
}


/***** The following functions should be moved to different file. *****/


int bs_cmp2(const bstring_t *s1, const bstring_t *s2)
{
   if (s1->len != s2->len)
      return s1->len > s2->len ? 1 : -1;
   return memcmp(s1->buf, s2->buf, s1->len);
}


/*! Match a bstring to a pattern and tag special matching (such as regex) into consideration.
 *  @param dst String to match.
 *  @param pat Pattern which is applied to string dst.
 *  @param st Definition of special match options.
 *  @return On successful match, 1 is returned, otherwise 0.
 */
int bs_match(const bstring_t *dst, const bstring_t *pat, const struct specialTag *st)
{
   int r = 1;
   char buf[dst->len + 1];
   double val;

   if (st == NULL)
      return bs_cmp2(dst, pat) == 0;

   if ((st->type & SPECIAL_MASK) == SPECIAL_DIRECT)
   {
      r = bs_cmp2(dst, pat);
   }
   else if ((st->type & SPECIAL_MASK) == SPECIAL_REGEX)
   {
      // FIXME: this could be avoid if tags are 0-terminated.
      memcpy(buf, dst->buf, dst->len);
      buf[dst->len] = '\0';
 
      r = regexec(&st->re, buf, 0, NULL, 0);
   }
   else if ((st->type & SPECIAL_MASK) == SPECIAL_GT)
   {
      val = bs_tod(*dst);
      r = !(val > st->val);
   }
   else if ((st->type & SPECIAL_MASK) == SPECIAL_LT)
   {
      val = bs_tod(*dst);
      r = !(val < st->val);
   }

   if (st->type & SPECIAL_INVERT)
      return r != 0;

   return r == 0;
}


/*! This function matches the match tag defined by ot and st to the tags of the
 * object o. 
 * @param o Object to with a specific tag is matched.
 * @param ot Tag which is matched to o.
 * @param st Special match information in respect to ot for additional
 * parameters, such as "greater than", "less than", "regex", and so on.
 * @return The function returns the index if the tag with o which matches the
 * requirements or -1 if no tag matches.
 */
int bs_match_attr(const osm_obj_t *o, const struct otag *ot, const struct stag *st)
{
   int i, kmatch, vmatch;

   for (i = 0; i < o->tag_cnt; i++)
   {
      kmatch = vmatch = 0;

      kmatch = ot->k.len ? bs_match(&o->otag[i].k, &ot->k, &st->stk) : 1;
      vmatch = ot->v.len ? bs_match(&o->otag[i].v, &ot->v, &st->stv) : 1;

      if (kmatch && (st->stk.type & SPECIAL_NOT))
         return -1;

      if (vmatch && (st->stv.type & SPECIAL_NOT))
         return -1;

      if (kmatch && vmatch)
         return i;
   }

   // FIXME: Why INT_MAX?
   if ((st->stk.type & SPECIAL_NOT) || (st->stv.type & SPECIAL_NOT))
      return INT_MAX;

   return -1;
}


/*! Match tag.
 *  @return -1 on error (no match), otherwise return number of tag which matches.
 */
int match_attr(const osm_obj_t *o, const char *k, const char *v)
{
   struct otag ot;
   struct stag st;

   memset(&ot, 0, sizeof(ot));
   memset(&st, 0, sizeof(st));

   if (k)
   {
      ot.k.len = strlen(k);
      ot.k.buf = (char*) k;
   }
   if (v)
   {
      ot.v.len = strlen(v);
      ot.v.buf = (char*) v;
   }

   return bs_match_attr(o, &ot, &st);
}


/*! Convert coordinate d to string.
 *  @param c Coordinate.
 *  @param lat_lon Format parameter.
 *  @param buf Output buffer.
 *  @param len Length of output buffer.
 *  @return returns the length of the new string or -1 in case of error.
 */
int coord_str(double c, int lat_lon, char *buf, int len)
{
   // safety check
   if (buf == NULL)
      return -1;

   switch (lat_lon)
   {
      case LAT_CHAR:
         return snprintf(buf, len, "%02d %c %.1f'", abs(c), c < 0 ? 'S' : 'N', (double) ((int) round(fabs(c) * T_RESCALE) % T_RESCALE) / TM_RESCALE);

      case LON_CHAR:
         return snprintf(buf, len, "%03d %c %.1f'", abs(c), c < 0 ? 'W' : 'E', (double) ((int) round(fabs(c) * T_RESCALE) % T_RESCALE) / TM_RESCALE);

      case LAT_DEG:
         return snprintf(buf, len, "%02d° %.1f'", abs(c), (double) ((int) round(fabs(c) * T_RESCALE) % T_RESCALE) / TM_RESCALE);

      case LON_DEG:
         return snprintf(buf, len, "%03d° %.1f'", abs(c), (double) ((int) round(fabs(c) * T_RESCALE) % T_RESCALE) / TM_RESCALE);
   }

   return -1;
}


int func_name(char *buf, int size, void *sym_addr)
{
#ifdef HAVE_DLADDR
   Dl_info dli;

   memset(&dli, 0, sizeof(dli));
   (void) dladdr(sym_addr, &dli);

   if (dli.dli_sname == NULL && size)
      *buf = '\0';
   else if ((int) strlen(dli.dli_sname) >= size)
      strncpy(buf, dli.dli_sname, size - 1), 
         buf[size - 1] = '\0';
   else
      strcpy(buf, dli.dli_sname);
#else
   *buf = '\0';
#endif

   return strlen(buf);
}


int strcnt(const char *s, int c)
{
   int n;

   for (n = 0; *s != '\0'; s++)
      if (*s == c)
         n++;

   return n;
}


char *get_param_err(const char *attr, double *dval, const action_t *act, int *err)
{
   fparam_t **fp;

   if ((act == NULL) || (act->fp == NULL) || (attr == NULL))
      return NULL;

   for (fp = act->fp; *fp != NULL; fp++)
   {
      if (!strcmp(attr, (*fp)->attr))
      {
         if (dval != NULL)
            *dval = (*fp)->dval;
         if (err != NULL)
            *err = (*fp)->conv_error;
         return (*fp)->val;
      }
   }
   return NULL;
}


char *get_param(const char *attr, double *dval, const action_t *act)
{
   return get_param_err(attr, dval, act, NULL);
}


char *get_parami(const char *attr, int *ival, const action_t *act)
{
   double f;
   char *r;

   if ((r = get_param(attr, &f, act)) == NULL)
      return NULL;

   if (ival != NULL)
      *ival = f;

   return r;
}


/*! This function tests an attribute to be true or false.
 * @param attr Pointer to attribute key.
 * @param act Pointer to action_t (which contains the list of action parameters).
 * @return The function returns 1 (true) if such an attribute exists and
 * contains the value '1' or 'yes'. If no such attribute exists or if the
 * attribute does not contain a "true" value the function returns 0 (false).
 */
int get_param_bool(const char *attr, const action_t *act)
{
   double f;
   char *s;

   if ((s = get_param(attr, &f, act)) == NULL)
      return 0;

   if ((f != 0) || !strcasecmp(s, "yes") || !strcasecmp(s, "true"))
      return 1;

   if (!strcasecmp(s, "no") || !strcasecmp(s, "false") || !strcasecmp(s, "0"))
      return 0;

   return 0;
}


int sm_is_threaded(const smrule_t *r)
{
   return (r->act->flags & ACTION_THREADED) != 0;
}


void sm_threaded(smrule_t *r)
{
   log_debug("activating multi-threading for rule 0x%016lx", r->oo->id);
   r->act->flags |= ACTION_THREADED;
}

#ifdef WITH_THREADS

#define MAX_THREAD_HANDLE 32

int sm_thread_id(void)
{
   static pthread_t thandle[MAX_THREAD_HANDLE];
   static int tcnt = 0;
   pthread_t this;
   int i;

   this = pthread_self();

   // check if thread handle already exists
   for (i = 0; i < tcnt; i++)
      if (this == thandle[i])
         return i;

   // check if list of thread handles is already full
   if (tcnt >= MAX_THREAD_HANDLE)
      return -1;

   thandle[tcnt] = this;
   return tcnt++;
}

#else

int sm_thread_id(void)
{
   return 0;
}

#endif


/*! Check if character pointer is NULL pointer and return pointer to constant
 * string "NULL" in that case.
 * @param s Pointer to string.
 * @return If s is NULL pointer, a pointer to the constant string "NULL" is
 * returned, otherwise it returns s.
 */
const char *safe_null_str(const char *s)
{
   return s == NULL ? "NULL" : s;
}

