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

/*! \file smfunc.c
 * This file contains all rule functions which do not create graphics output.
 *
 *  @author Bernhard R. Fischer
 */
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <inttypes.h>

#include "smrender_dev.h"
#include "smcore.h"
#include "smloadosm.h"


#define DIR_CW 0
#define DIR_CCW 1


struct out_handle
{
   struct out_handle *next;
   char *name;
   int cnt;
   bx_node_t *tree;
};


struct io_handle
{
   struct out_handle *oh;
   bx_node_t *itree;
   hpx_ctrl_t *ctl;
};


struct fmt_info
{
   const char *fmt;
   const char *addtag;
};


struct sub_handler
{
   int version;
   bx_node_t *rules;
   osm_obj_t *parent;
   int finish;
};


struct inherit_data
{
   struct rdata *rdata;
   int force;
   int type;
   int dir;
#define UP 0
#define DOWN 1
};


struct trans_data
{
   osm_obj_t *o;
   int newtag;
   int tag_cnt;
   struct stag *st;
   struct otag *ot;
};


struct settags
{
   int64_t id, lastid;
   osm_obj_t *o;
};


struct random
{
#define RTYPE_INT 0
#define RTYPE_DOUBLE 1
   int type;
   union
   {
      long lo;
      double lod;
   };
   union
   {
      long hi;
      double hid;
   };
   char *key;
};


#define NODE_MIN_DIST (1.0 / 60)
#define MASK_NODE -1.0
#define INC_MAX_NL 64
typedef struct node_list
{
   int node_cnt, max_cnt;
   osm_node_t **node;
   double min_dist;
} node_list_t;


static struct out_handle *oh_ = NULL;


int act_out_ini(smrule_t *r)
{
   struct out_handle **oh;
   char *s;
 
   if ((s = get_param("file", NULL, r->act)) == NULL)
   {
      log_msg(LOG_WARN, "parameter 'file' missing");
      return 1;
   }
 
   for (oh = &oh_; *oh != NULL; oh = &(*oh)->next)
   {
      // check if filename exists
      if (!strcmp((*oh)->name, s))
      {
         log_debug("file '%s' reused", s);
         (*oh)->cnt++;
         r->data = *oh;
         return 0;
      }
   }
 
   if ((*oh = calloc(1, sizeof(struct out_handle))) == NULL)
   {
      log_msg(LOG_ERR, "calloc() failed: %s", strerror(errno));
      return -1;
   }

   if (((*oh)->name = strdup(s)) == NULL)
   {
      log_msg(LOG_ERR, "strdup() failed: %s", strerror(errno));
      return -1;
   }

   (*oh)->cnt++;
   r->data = *oh;

   return 0;
}


int out0(struct out_handle *oh, osm_obj_t *o)
{
   osm_node_t *n;
   osm_rel_t *r;
   int i;

   if (o->type == OSM_REL)
   {
      r = (osm_rel_t*) o;
      for (i = 0; i < r->mem_cnt; i++)
      {
         if ((o = get_object(r->mem[i].type, r->mem[i].id)) == NULL)
         {
            log_debug("get_object(%d, %ld) returned NULL", r->mem[i].type, (long) r->mem[i].id);
            continue;
         }
         // FIXME: if there is a cyclic dependency of a relation in a relation
         // a stack overflow will occur.
         (void) out0(oh, o);
      }
      o = (osm_obj_t*) r;
   }

   if (o->type == OSM_WAY)
   {
      for (i = 0; i < ((osm_way_t*) o)->ref_cnt; i++)
      {
         if ((n = get_object(OSM_NODE, ((osm_way_t*) o)->ref[i])) == NULL)
         {
            log_debug("get_object() returned NULL");
            continue;
         }
         // FIXME: return value should be honored (but put_object0() handles
         // errors correctly, hence, this is not a tragedy)
         (void) put_object0(&oh->tree, n->obj.id, n, n->obj.type - 1);
      }
   }

   return put_object0(&oh->tree, o->id, o, o->type - 1);
}


int act_out_main(smrule_t *r, osm_obj_t *o)
{
   return out0(r->data, o);
}


int act_out_fini(smrule_t *r)
{
   struct out_handle *oh = r->data;
   struct out_handle **olist;
   
   oh->cnt--;
   if (oh->cnt)
   {
      log_debug("file ref count = %d", oh->cnt);
      return 0;
   }
   (void) save_osm(oh->name, oh->tree, NULL, NULL);
   free(oh->name);
   log_debug("freeing temporary object tree");
   bx_free_tree(oh->tree);

   // delete entry from list
   for (olist = &oh_; *olist != NULL; olist = &(*olist)->next)
   {
      if (*olist == oh)
      {
         log_debug("deleting file entry %p", oh);
         *olist = oh->next;
         free(oh);
         // break loop on last entry of list
         if (*olist == NULL)
            break;
      }
   }

   return 0;
}


/*! Calculate the area and the centroid of a closed polygon. The function
 * implements Gauss's area formula aka shoelace formula.
 *  @param w Pointer to closed polygon.
 *  @param center Pointer to struct coord which will receive the coordinates of the
 *  centroid. It may be NULL.
 *  @param area Pointer to variable which will receive the area of the polygon.
 *  If area is positive, the nodes of the polygon are order counterclockwise, if
 *  area is negative they are ordered clockwise.
 *  The result is the area measured in nautical square miles.
 *  @return Returns 0 on success, -1 on error.
 */
int poly_area(const osm_way_t *w, struct coord *center, double *area)
{
   struct coord c;
   double f, x[2], ar;
   osm_node_t *n[2];
   int i;

   if (center == NULL && area == NULL)
      return 0;

   if (!is_closed_poly(w))
   {
      //log_msg(LOG_DEBUG, "poly_area() only allowed on closed polygons");
      return -1;
   }

   if ((n[1] = (osm_node_t*) get_object(OSM_NODE, w->ref[0])) == NULL)
   {
      log_msg(LOG_ERR, "something is wrong with way %ld: node does not exist", w->obj.id);
      return -1;
   }

   ar = 0;
   c.lat = 0;
   c.lon = 0;

   for (i = 0; i < w->ref_cnt - 1; i++)
   {
      n[0] = n[1];
      if ((n[1] = (osm_node_t*) get_object(OSM_NODE, w->ref[i + 1])) == NULL)
      {
         log_msg(LOG_ERR, "something is wrong with way %ld: node does not exist", w->obj.id);
         return -1;
      }

      x[0] = n[0]->lon * cos(DEG2RAD(n[0]->lat));
      x[1] = n[1]->lon * cos(DEG2RAD(n[1]->lat));
      f = x[0] * n[1]->lat - x[1] * n[0]->lat;
      c.lon += (x[0] + x[1]) * f;
      c.lat += (n[0]->lat + n[1]->lat) * f;
      ar += f;
      //log_debug("%d %f %f %f %f %f %f %f/%f %f/%f", i, f, sx, sy, cx, cy, ar, n[0]->nd.lon, n[0]->nd.lat, n[1]->nd.lon, n[1]->nd.lat);
   }

   c.lat /= 3.0 * ar;
   c.lon /= 3.0 * ar * cos(DEG2RAD(c.lat));
   ar = ar * 1800.0;

   if (center != NULL)
      *center = c;

   if (area != NULL)
      *area = ar;

   return 0;
}


int act_poly_area_ini(smrule_t *r)
{
   sm_threaded(r);
   return 0;
}


int act_poly_area_main(smrule_t * UNUSED(r), osm_way_t *w)
{
   double ar;
   struct otag *ot;
   struct coord c;
   char buf[256], *s;

   if (!poly_area(w, &c, &ar))
   {
      //log_msg(LOG_DEBUG, "poly_area of %ld = %f", w->obj.id, ar);
      if ((ot = realloc(w->obj.otag, sizeof(struct otag) * (w->obj.tag_cnt + 1))) == NULL)
      {
         log_msg(LOG_DEBUG, "could not realloc tag list: %s", strerror(errno));
         return 0;
      }
      w->obj.otag = ot;
      snprintf(buf, sizeof(buf), "%.8f", fabs(ar));
      if ((s = strdup(buf)) == NULL)
      {
         log_msg(LOG_DEBUG, "could not strdup");
         return 0;
      }
      set_const_tag(&w->obj.otag[w->obj.tag_cnt], "smrender:area", s);
      w->obj.tag_cnt++;
   }

   return 0;
}


int act_poly_centroid_ini(smrule_t *r)
{
   sm_threaded(r);
   return 0;
}


int act_poly_centroid_main(smrule_t * UNUSED(r), osm_way_t *w)
{
   struct coord c;
   double ar;
   osm_node_t *n;
   char buf[256], *s;

   if (!is_closed_poly(w))
      return 0;

   if (poly_area(w, &c, &ar))
      return 1;

   n = malloc_node(w->obj.tag_cnt + 1);
   // FIXME: generator=smrender gets overwritten
   osm_node_default(n);
   n->lat = c.lat;
   n->lon = c.lon;

   snprintf(buf, sizeof(buf), "%"PRId64, w->obj.id);
   if ((s = strdup(buf)) == NULL)
   {
      free_obj((osm_obj_t*) n);
      log_errno(LOG_ERR, "could not strdup()");
      return 0;
   }
   set_const_tag(&n->obj.otag[0], "smrender:id:way", s);
   memcpy(&n->obj.otag[1], &w->obj.otag[0], sizeof(struct otag) * w->obj.tag_cnt);
   put_object((osm_obj_t*) n);
 
   //log_debug("centroid %.3f/%.3f, ar = %f, way = %ld, node %ld", n->lat, n->lon, ar, w->obj.id, n->obj.id);

   return 0;
}


static int reverse_way(osm_way_t *w)
{
   int64_t ref;

   for (int i = 1; i <= w->ref_cnt / 2 - 1; i++)
   {
      ref = w->ref[i];
      w->ref[i] = w->ref[w->ref_cnt - i - 1];
      w->ref[w->ref_cnt - i - 1] = ref;
   }
   return 0;
}


int act_reverse_way_ini(smrule_t *r)
{
   if (r->oo->type != OSM_NODE)
   {
     log_msg(LOG_ERR, "reverse_way is only applicable to ways");
     return 1;
   }
   return 0;
}


int act_reverse_way_fini(smrule_t * UNUSED(r))
{
   return 0;
}


int act_reverse_way_main(smrule_t * UNUSED(r), osm_obj_t *o)
{
   return reverse_way((osm_way_t*) o);
}


int set_way_direction(osm_way_t *w, int dir)
{
   struct coord c;
   double ar;

   if (!is_closed_poly(w))
      return 0;

   if (poly_area(w, &c, &ar))
      return -1;

   if (((ar < 0) && (dir == DIR_CCW)) || ((ar > 0) && (dir == DIR_CW)))
      return reverse_way(w);

   return 0;
}


int act_set_ccw_main(smrule_t * UNUSED(r), osm_way_t *w)
{
   return set_way_direction(w, DIR_CCW);
}


int act_set_cw_main(smrule_t * UNUSED(r), osm_way_t *w)
{
   return set_way_direction(w, DIR_CW);
}


int64_t random64(void)
{
   /* FIXME: this assumes that RAND_MAX == 1 << 31 - 1, hence, the result
    * will be between 0 and 1 << 63 - 1. If RAND_MAX < 1 << 31 - 1 the result
    * will not be appropriate. */
   return ((int64_t) random()) << 31 | random();
}


int act_set_tags_ini(smrule_t *r)
{
   smrule_t *rule;
   char *s;
   struct settags st;

   memset(&st, 0, sizeof(st));

   // get id string of template object
   if ((s = get_param("id", NULL, r->act)) == NULL)
   {
      log_msg(LOG_WARN, "set_tags requires parameter 'id'");
      return -1;
   }

   // parse id string
   errno = 0;
   st.id = strtol(s, NULL, 0);
   if (errno)
   {
      log_msg(LOG_WARN, "cannot convert id: %s", strerror(errno));
      return -1;
   }

   // try get lastid string
   if ((s = get_param("lastid", NULL, r->act)) != NULL)
   {
      // parse lastid string
      errno = 0;
      st.lastid = strtol(s, NULL, 0);
      if (errno)
      {
         log_msg(LOG_WARN, "cannot convert id: %s", strerror(errno));
         return -1;
      }
      st.o = NULL;
   }
   // if there is no lastid string...
   else
   {
      // get rule according to id
      if ((rule = get_object0(get_rdata()->rules, st.id, r->oo->type - 1)) == NULL)
      {
         log_msg(LOG_WARN, "there is no rule of type %d with id 0x%016lx", r->oo->type, st.id);
         return 1;
      }
      // get object of rule
      if (rule->oo == NULL)
      {
         log_msg(LOG_CRIT, "rule has no object");
         return 1;
      }
      st.o = rule->oo;
   }

   log_debug("set_tags params: id = %"PRId64", lastid = %"PRId64", tag_obj = %p", st.id, st.lastid, st.o);

   // get mem for settags structure
   if ((r->data = calloc(1, sizeof(st))) == NULL)
   {
      log_errno(LOG_ERR, "failed to get memory for struct settags");
      return -1;
   }

   // copy settags data to heap object
   *((struct settags*) r->data) = st;
   return 0;
}


int act_set_tags_main(smrule_t *r, osm_obj_t *o)
{
   struct settags *st = r->data;
   osm_obj_t *templ_o;
   smrule_t *rule;
   struct otag *ot;
   int64_t id;

   if (st->o == NULL)
   {
      id = st->id + random64() % (st->lastid - st->id);
      log_debug("chose id %"PRId64, id);

      if ((rule = get_object0(get_rdata()->rules, id, r->oo->type - 1)) == NULL)
      {
         log_msg(LOG_WARN, "there is no rule with id %"PRId64, id);
         return 1;
      }
      if (rule->oo == NULL)
      {
         log_msg(LOG_CRIT, "rule has no object");
         return -1;
      }
      templ_o = rule->oo;
   }
   else
      templ_o = st->o;

   if ((ot = realloc(o->otag, sizeof(struct otag) * (o->tag_cnt + templ_o->tag_cnt))) == NULL)
   {
      log_msg(LOG_CRIT, "Cannot realloc tag memory: %s", strerror(errno));
      return -1;
   }

   o->otag = ot;
   memcpy(&o->otag[o->tag_cnt], templ_o->otag, sizeof(struct otag) * templ_o->tag_cnt);
   o->tag_cnt += templ_o->tag_cnt;

   return 0;
}


int act_set_tags_fini(smrule_t *r)
{
   free(r->data);
   return 0;
}


int act_shape_ini(smrule_t *r)
{
   struct act_shape *as;
   double pcount = 0.0;
   char *s = "", *s2;
   int e;

   if ((get_param("nodes", &pcount, r->act) == NULL) && ((s = get_param("style", NULL, r->act)) == NULL))
   {
      log_msg(LOG_WARN, "action 'shape' requires parameter 'style' or 'nodes'");
      return 1;
   }

   if ((as = calloc(1, sizeof(*as))) == NULL)
   {
      log_msg(LOG_ERR, "cannot calloc struct act_shape: %s", strerror(errno));
      return -1;
   }

   if (get_param("weight", &as->weight, r->act) == NULL)
      as->weight = 1.0;
   (void) get_param("phase", &as->phase, r->act);
   as->phase *= M_PI / 180.0;

   e = 0;
   as->startkey = get_param_err("start", &as->start, r->act, &e);
   if (e)
      as->start = NAN;
   e = 0;
   as->endkey = get_param_err("end", &as->end, r->act, &e);
   if (e)
      as->end = NAN;

   if ((s2 = get_param("subtype", NULL, r->act)) != NULL)
   {
      if (!strcasecmp(s2, "sectored"))
         as->type = SHAPE_SECTORED;
      else if (!strcasecmp(s2, "stared"))
         as->type = SHAPE_STARED;
      else
         log_msg(LOG_WARN, "unknown subtype '%s'", s2);
   }

   get_param("r2", &as->r2, r->act);

   if (pcount == 0.0)
   {
      if (!strcmp(s, "triangle"))
      {
         as->pcount = 3;
      }
      else if (!strcmp(s, "square"))
      {
         as->pcount = 4;
      }
      else if (!strcmp(s, "circle"))
      {
         // set to maximum number of nodes in case of a circle (this is
         // recalculated, see below)
         as->pcount = MAX_SHAPE_PCOUNT;
      }
      else
      {
         log_msg(LOG_WARN, "unknown shape '%s'", s);
         free(as);
         return 1;
      }
   }
   else
   {
      if (pcount < 3.0)
      {
         log_msg(LOG_WARN, "value for 'nodes' must be at least 3");
         free(as);
         return 1;
      }
      else if (pcount > MAX_SHAPE_PCOUNT)
      {
         log_msg(LOG_WARN, "'nodes' must not exceed %d", MAX_SHAPE_PCOUNT);
         free(as);
         return 1;
      }
      as->pcount = pcount;
   }

   if (get_param("radius", &as->size, r->act) == NULL)
   {
      log_msg(LOG_WARN, "action 'shape' requires parameter 'radius', defaults to 1.0mm");
      as->size = 1.0;
   }
   else if (as->size <= 0.0)
      as->size = 1.0;

   // recalculate node count in case of a circle
   if (as->pcount == MAX_SHAPE_PCOUNT)
      as->pcount = MM2PX(2.0 * as->size * M_PI) / 3;

   (void) get_param("angle", &as->angle, r->act);
   as->key = get_param("key", NULL, r->act);

   log_debug("nodes = %d, radius = %.2f, angle = %.2f, key = '%s', start = %f, startkey = %s, end = %f, endkey = %s, subtype = %d, r2 = %f",
         as->pcount, as->size, as->angle, safe_null_str(as->key), as->start, safe_null_str(as->startkey), as->end, safe_null_str(as->endkey), as->type, as->r2);

   r->data = as;
   return 0;
}


int attr_tod(const osm_obj_t *o, const char *attr, double *val)
{
   int i;

   if ((i = match_attr(o, attr, NULL)) >= 0)
   {
      if (val != NULL)
      {
         *val = bs_tod(o->otag[i].v);
         log_debug("bearing %f", *val);
      }
      return 0;
   }

   log_msg(LOG_INFO, "node %"PRId64" has no tag '%s=*'", o->id, attr);
   return -1;
}


void shape_node(const struct act_shape *as, const osm_node_t *n)
{
   struct rdata *rd = get_rdata();
   osm_node_t *nd[as->pcount], *m;
   double radius, angle, angle_step, a, b, start, end;
   osm_way_t *w, *v;
   int i, j;

   angle = M_PI_2;
   if (as->key != NULL)
   {
      if (!attr_tod(&n->obj, as->key, &angle))
         angle = DEG2RAD(90.0 - angle);
   }

   if (as->startkey != NULL)
   {
      if (as->start == NAN)
      {
         if (!attr_tod(&n->obj, as->startkey, &start))
            start = DEG2RAD(start);
      }
      else
         start = DEG2RAD(as->start);
   }
   else
      start = 0;

   if (as->endkey != NULL)
   {
      if (as->end == NAN)
      {
         if (!attr_tod(&n->obj, as->endkey, &end))
            end = DEG2RAD(end);
      }
      else
         end = DEG2RAD(as->end);
   }
   else
      end = 2 * M_PI;

   start = fmod2(start + M_PI_2, 2 * M_PI);
   end = fmod2(end + M_PI_2, 2 * M_PI);

   radius = MM2LAT(as->size);
   angle += DEG2RAD(as->angle);
   angle_step = 2 * M_PI / as->pcount;

   w = malloc_way(n->obj.tag_cnt + 1, as->pcount + 2);
   osm_way_default(w);
   memcpy(&w->obj.otag[1], n->obj.otag, sizeof(struct otag) * n->obj.tag_cnt);

   log_debug("generating shape way %"PRId64" with <=%d nodes", (long) w->obj.id, as->pcount);

   a = radius;
   b = radius * as->weight;
   for (i = 0, j = 0; i < as->pcount; i++)
   {
      if (as->startkey != NULL && start > angle_step * i - as->phase)
         continue;
      if (as->endkey != NULL && angle_step * i - as->phase > end)
         break;

      nd[j] = malloc_node(1);
      osm_node_default(nd[j]);
      nd[j]->lat = n->lat + a * cos(angle_step * i - as->phase) * cos(-angle) - b * sin(angle_step * i - as->phase) * sin(-angle);
      nd[j]->lon = n->lon + (a * cos(angle_step * i - as->phase) * sin(-angle) + b * sin(angle_step * i - as->phase) * cos(-angle)) / cos(DEG2RAD(n->lat));
      w->ref[j] = nd[j]->obj.id;
      put_object(&nd[j]->obj);

      if (as->type == SHAPE_STARED)
      {
         v = malloc_way(n->obj.tag_cnt + 1, 2);
         osm_way_default(v);
         memcpy(&v->obj.otag[1], n->obj.otag, sizeof(struct otag) * n->obj.tag_cnt);
         if (as->r2 > 0)
         {
            m = malloc_node(1);
            osm_node_default(m);

            m->lat = n->lat + MM2LAT(as->r2) * cos(angle_step * i - as->phase) * cos(-angle) - MM2LAT(as->r2) * as->weight * sin(angle_step * i - as->phase) * sin(-angle);
            m->lon = n->lon + (MM2LAT(as->r2) * cos(angle_step * i - as->phase) * sin(-angle) + MM2LAT(as->r2) * as->weight * sin(angle_step * i - as->phase) * cos(-angle)) / cos(DEG2RAD(n->lat));

            v->ref[0] = m->obj.id;
            put_object(&m->obj);
         }
         else
            v->ref[0] = n->obj.id;
         v->ref[1] = nd[j]->obj.id;
         put_object(&v->obj);
      }

      j++;
   }

   log_debug("%d nodes added", j);
   if (j && as->type != SHAPE_STARED)
   {
      w->ref_cnt = j;
      if (as->startkey == NULL && as->endkey == NULL)
      {
         w->ref[j] = nd[0]->obj.id;
         w->ref_cnt++;
      }
      else if (as->type == SHAPE_SECTORED)
      {
         w->ref[j++] = n->obj.id;
         w->ref[j] = nd[0]->obj.id;
         w->ref_cnt += 2;
      }
      put_object(&w->obj);
   }
   
   if (as->type == SHAPE_STARED)
      free_obj(&w->obj);
}


void shape_way(struct act_shape *as, osm_way_t *w)
{
   osm_node_t *n;
   int i;

   for (i = 0; i < w->ref_cnt; i++)
   {
      if ((n = get_object(OSM_NODE, w->ref[i])) == NULL)
      {
         log_msg(LOG_WARN, "node %ld of way %ld does not exist", (long) w->ref[i], (long) w->obj.id);
         continue;
      }
      shape_node(as, n);
   }
}


int act_shape_main(smrule_t *r, osm_obj_t *o)
{
   if (o->type == OSM_NODE)
   {
      shape_node(r->data, (osm_node_t*) o);
   }
   else if (o->type == OSM_WAY)
   {
      shape_way(r->data, (osm_way_t*) o);
   }
   else
   {
      log_msg(LOG_NOTICE, "shape() on this object type not supported");
      return 1;
   }
   return 0;
}


int act_shape_fini(smrule_t *r)
{
   free(r->data);
   r->data = NULL;
   return 0;
}


int act_ins_eqdist_ini(smrule_t *r)
{
#define DEFAULT_DISTANCE (2.0/60)
   double *dist;
   value_t v;
   char *s;

   if ((dist = malloc(sizeof(*dist))) == NULL)
   {
      log_msg(LOG_ERR, "malloc() failed in act_ins_eqdist_ini(): %s", strerror(errno));
      return -1;
   }

   r->data = dist;

   if ((s = get_param("distance", NULL, r->act)) != NULL)
   {
      parse_length_def(s, &v, U_NM);
      *dist = fabs(rdata_unit(&v, U_DEG));
   }
   else
      *dist = DEFAULT_DISTANCE;

   log_debug("distance = %.3f nm", *dist * 60);
   return 0;
}


int ins_eqdist(osm_way_t *w, double dist)
{
   struct pcoord pc;
   struct coord sc, dc;
   osm_node_t *s, *d, *n;
   double ddist;
   char buf[32];
   int64_t *ref;
   int i, pcnt;

   if (w->obj.type != OSM_WAY)
   {
      log_msg(LOG_WARN, "ins_eqdist() may be applied to ways only!");
      return 1;
   }

   // find first valid point (usually this is ref[0])
   for (i = 0, s = NULL; i < w->ref_cnt - 1; i++)
   {
      if ((s = get_object(OSM_NODE, w->ref[i])) != NULL)
         break;
      log_msg(LOG_WARN, "node %ld of way %ld does not exist", (long) w->ref[i], (long) w->obj.id);
   }

   // safety check
   if (s == NULL)
   {
      log_msg(LOG_EMERG, "no valid node found. This should never happen");
      return -1;
   }

   sc.lat = s->lat;
   sc.lon = s->lon;
   ddist = dist;

   for (++i, pcnt = 0; i < w->ref_cnt; i++)
   {
      if ((d = get_object(OSM_NODE, w->ref[i])) == NULL)
      {
         log_msg(LOG_WARN, "node %ld of way %ld does not exist", (long) w->ref[i], (long) w->obj.id);
         continue;
      }
      dc.lat = d->lat;
      dc.lon = d->lon;
      pc = coord_diff(&sc, &dc);

      if (pc.dist > ddist)
      {
         // create new node
         n = malloc_node(w->obj.tag_cnt + 3);
         osm_node_default(n);
         memcpy(&n->obj.otag[3], w->obj.otag, sizeof(struct otag) * w->obj.tag_cnt);
         pcnt++;
         snprintf(buf, sizeof(buf), "%.1f", dist * pcnt * 60.0);
         set_const_tag(&n->obj.otag[1], "distance", strdup(buf));
         snprintf(buf, sizeof(buf), "%.1f", pc.bearing);
         set_const_tag(&n->obj.otag[2], "bearing", strdup(buf));

         // calculate coordinates
         n->lat = s->lat + ddist * cos(DEG2RAD(pc.bearing));
         n->lon = s->lon + ddist * sin(DEG2RAD(pc.bearing)) / cos(DEG2RAD((n->lat + s->lat) / 2));

         log_debug("insert node %ld, lat_diff = %lf, lon_diff = %lf, cos = %lf", (long) n->obj.id,
               (d->lat - s->lat) * cos(DEG2RAD(pc.bearing)), 
               - (d->lon - s->lon) * sin(DEG2RAD(pc.bearing)),
               cos(DEG2RAD(s->lat)));

         // add object to tree
         put_object((osm_obj_t*) n);

         s = n;
         sc.lat = s->lat;
         sc.lon = s->lon;
         ddist = dist;

         // add node reference to way
         if ((ref = realloc(w->ref, sizeof(int64_t) * (w->ref_cnt + 1))) == NULL)
         {
            log_msg(LOG_ERR, "realloc() failed in ins_eqdist(): %s", strerror(errno));
            return -1;
         }
         w->ref = ref;
         memmove(&ref[i + 1], &ref[i], sizeof(int64_t) * (w->ref_cnt - i));
         ref[i] = n->obj.id;
         w->ref_cnt++;
         //i--;
      }
      else
      {
         ddist -= pc.dist;
         s = d;
         sc.lat = s->lat;
         sc.lon = s->lon;
      }
   }

   return 0;
}


int act_ins_eqdist_main(smrule_t *r, osm_way_t *w)
{
   return ins_eqdist(w, *((double*) r->data));
}


int act_ins_eqdist_fini(smrule_t *r)
{
   free(r->data);
   r->data = NULL;
   return 0;
}


int cmp_double(const void *a, const void *b)
{
   if (*((double*) a) < *((double*) b))
      return -1;
   if (*((double*) a) > *((double*) b))
      return 1;
   return 0;
}


int dist_median(const osm_way_t *w, double *median)
{
   osm_node_t *n;
   struct pcoord pc;
   struct coord c[2];
   double *dist;
   int i;
 
   if (w->obj.type != OSM_WAY)
   {
      log_msg(LOG_ERR, "dist_median() may only be called with ways");
      return -1;
   }
   if (w->ref_cnt < 2)
   {
      log_msg(LOG_WARN, "way %ld has to less nodes (ref_cnt = %d)", w->obj.id, w->ref_cnt);
      return -1;
   }

   if ((dist = malloc(sizeof(*dist) * (w->ref_cnt - 1))) == NULL)
   {
      log_msg(LOG_ERR, "cannot malloc() in dist_median(): %s", strerror(errno));
      return -1;
   }

   if ((n = get_object(OSM_NODE, w->ref[0])) == NULL)
   {
      log_msg(LOG_WARN, "way %ld has no such node with id %ld", w->obj.id, w->ref[0]);
      free(dist);
      return -1;
   }

   c[1].lat = n->lat;
   c[1].lon = n->lon;
   for (i = 0; i < w->ref_cnt - 1; i++)
   {
      c[0] = c[1];
      if ((n = get_object(OSM_NODE, w->ref[i + 1])) == NULL)
      {
         log_msg(LOG_WARN, "way %ld has no such node with id %ld", w->obj.id, w->ref[i + 1]);
         free(dist);
         return -1;
      }
      c[1].lat = n->lat;
      c[1].lon = n->lon;
      pc = coord_diff(&c[0], &c[1]);
      dist[i] = pc.dist;
   }

   qsort(dist, w->ref_cnt - 1, sizeof(*dist), cmp_double);
   *median = dist[(w->ref_cnt - 1) >> 1];
   if (w->ref_cnt & 1)
      *median = (*median + dist[((w->ref_cnt - 1) >> 1) - 1]) / 2.0;

   free(dist);
   return 0;
}


int act_dist_median_main(smrule_t * UNUSED(r), osm_way_t *w)
{
   struct otag *ot;
   char buf[32];
   double dist;

   if (w->obj.type != OSM_WAY)
   {
      log_msg(LOG_WARN, "dist_median() may only be applied to ways");
      return 1;
   }

   if (dist_median(w, &dist))
      return 1;

   if ((ot = realloc(w->obj.otag, sizeof(struct otag) * (w->obj.tag_cnt + 1))) == NULL)
   {
      log_msg(LOG_ERR, "could not realloc tag list in dist_median(): %s", strerror(errno));
      return 1;
   }

   w->obj.otag = ot;
   snprintf(buf, sizeof(buf), "%.8f", dist);
   set_const_tag(&w->obj.otag[w->obj.tag_cnt], "smrender:dist_median", strdup(buf));
   w->obj.tag_cnt++;

   return 0;
}


hpx_ctrl_t *get_ofile_ctl(const char *filename)
{
   hpx_ctrl_t *ctl;
   struct stat st;
   int fd;

   if ((fd = open(filename, O_RDONLY)) == -1)
   {
      log_msg(LOG_ERR, "cannot open file '%s': %s", filename, strerror(errno));
      return NULL;
   }

   if (fstat(fd, &st) == -1)
   {
      log_msg(LOG_ERR, "fstat() failed: %s", strerror(errno));
      (void) close(fd);
      return NULL;
   }

   if ((ctl = hpx_init(fd, -st.st_size)) == NULL)
   {
      log_msg(LOG_ERR, "hpx_init() failed: %s", strerror(errno));
      (void) close(fd);
      return NULL;
   }

   return ctl;
}


/*! 
 *  Action parameters: file=*, infile=*.
 */
int act_diff_ini(smrule_t *r)
{
   struct io_handle *ioh;
   char *s;
   int e;

   if ((s = get_param("infile", NULL, r->act)) == NULL)
   {
      log_msg(LOG_WARN, "parameter 'outfile' missing");
      return 1;
   }

   if ((ioh = malloc(sizeof(*ioh))) == NULL)
   {
      log_msg(LOG_ERR, "malloc failed in act_diff_ini(): %s", strerror(errno));
      return 1;
   }

   if ((ioh->ctl = get_ofile_ctl(s)) == NULL)
   {
      log_debug("get_ofile_ctl() failed");
      free(ioh);
      return 1;
   }

   if ((e = act_out_ini(r)))
   {
      log_msg(LOG_WARN, "act_out_ini() returned %d", e);
      (void) close(ioh->ctl->fd);
      hpx_free(ioh->ctl);
      free(ioh);
      return e;
   }

   log_debug("reading file '%s'", s);
   ioh->oh = r->data;
   ioh->itree = NULL;
   (void) read_osm_file(ioh->ctl, &ioh->itree, NULL, NULL);
   r->data = ioh;

   return 0;
}


int obj_exists(osm_obj_t *o, struct out_handle *oh)
{
   if (get_object(o->type, o->id) == NULL)
      out0(oh, o);

   return 0;
}


int act_diff_fini(smrule_t *r)
{
   struct io_handle *ioh = r->data;
   int e;

   if (ioh == NULL)
      return -1;

   log_debug("traversing nodes");
   traverse(ioh->itree, 0, IDX_NODE, (tree_func_t) obj_exists, ioh->oh);
   log_debug("traversing ways");
   traverse(ioh->itree, 0, IDX_WAY, (tree_func_t) obj_exists, ioh->oh);
   log_debug("traversing relations");
   traverse(ioh->itree, 0, IDX_REL, (tree_func_t) obj_exists, ioh->oh);

   r->data = ioh->oh;
   if ((e = act_out_fini(r)))
      log_msg(LOG_WARN, "act_out_fini() returned %d", e);

   (void) close(ioh->ctl->fd);
   hpx_free(ioh->ctl);
   // FIXME: free objects in tree before
   bx_free_tree(ioh->itree);
   free(ioh);

   return 0;
}


int act_poly_len_ini(smrule_t *r)
{
   if (r->oo->type != OSM_WAY)
   {
      log_msg(LOG_WARN, "poly_len() may be applied to ways only!");
      return 1;
   }
   return 0;
}


int poly_len(const osm_way_t *w, double *dist)
{
   osm_node_t *n;
   struct coord c[2];  
   struct pcoord pc;
   int i;

   if (w->ref_cnt < 2)
   {
      log_msg(LOG_WARN, "way %ld has less than 2 nodes (%d)", w->obj.id, w->ref_cnt);
      return -1;
   }

   if ((n = get_object(OSM_NODE, w->ref[0])) == NULL)
   {
      log_msg(LOG_WARN, "way %ld has no such node with id %ld", w->obj.id, w->ref[0]);
      return -1;
   }

   *dist = 0.0;
   c[1].lat = n->lat;
   c[1].lon = n->lon;
   for (i = 0; i < w->ref_cnt - 1; i++)
   {
      c[0] = c[1];
      if ((n = get_object(OSM_NODE, w->ref[i + 1])) == NULL)
      {
         log_msg(LOG_WARN, "way %ld has no such node with id %ld, ignoring", w->obj.id, w->ref[i + 1]);
         continue;
      }
      c[1].lat = n->lat;
      c[1].lon = n->lon;
      pc = coord_diff(&c[0], &c[1]);
      *dist += pc.dist;
   }

   *dist *= 60.0;
   return 0;
}


int act_poly_len_main(smrule_t * UNUSED(r), osm_way_t *w)
{
   struct otag *ot;
   char buf[32];
   double dist;

   if (poly_len(w, &dist))
   {
      log_msg(LOG_WARN, "could not calculate length of way %ld", w->obj.id);
      return 1;
   }
   
   if ((ot = realloc(w->obj.otag, sizeof(struct otag) * (w->obj.tag_cnt + 1))) == NULL)
   {
      log_msg(LOG_ERR, "could not realloc tag list in poly_len(): %s", strerror(errno));
      return 1;
   }

   w->obj.otag = ot;
   snprintf(buf, sizeof(buf), "%.8f", dist);
   set_const_tag(&w->obj.otag[w->obj.tag_cnt], "smrender:length", strdup(buf));
   w->obj.tag_cnt++;
   return 0;
}


/*! Unthreaded action, simple does nothing. Smrender automatically syncs
 * threads before calling this function. Because this does nothing, the
 * following action is also called "thread-synced" (without any other thread
 * running). With this one can force previous _main functions to have finished
 * before the next action is executed.
 */
int act_sync_threads_ini(smrule_t * UNUSED(r))
{
   return 0;
}


static int parse_id(smrule_t *r)
{
   int64_t id;
   char *s;

   if ((s = get_param("id", NULL, r->act)) == NULL)
   {
      log_msg(LOG_WARN, "rule requires missing parameter 'id'");
      return -1;
   }

   errno = 0;
   id = strtoll(s, NULL, 0);
   if (errno)
      return -1;

   if ((r->data = get_object0(get_rdata()->rules, id, r->oo->type - 1)) == NULL)
      return -1;

   return 0;
}


/*! Disable object, i.e. set visibility to 0.
 */
int act_disable_main(smrule_t * UNUSED(r), osm_obj_t *o)
{
   o->vis = 0;
   return 0;
}


/*! Enable object, i.e. set visibility to 1.
 */
int act_enable_main(smrule_t * UNUSED(r), osm_obj_t *o)
{
   o->vis = 1;
   return 0;
}


int act_enable_rule_ini(smrule_t *r)
{
   return parse_id(r);
}


int act_enable_rule_main(smrule_t *r, osm_obj_t * UNUSED(o))
{
   return act_enable_main(r, r->data);
}


int act_disable_rule_ini(smrule_t *r)
{
   return parse_id(r);
}


int act_disable_rule_main(smrule_t *r, osm_obj_t * UNUSED(o))
{
   return act_disable_main(r, r->data);
}


static void bbox_min_max(const struct coord *cd, struct bbox *bb)
{
   if (cd->lon > bb->ru.lon)
      bb->ru.lon = cd->lon;
   if (cd->lon < bb->ll.lon)
      bb->ll.lon = cd->lon;
   if (cd->lat > bb->ru.lat)
      bb->ru.lat = cd->lat;
   if (cd->lat < bb->ll.lat)
      bb->ll.lat = cd->lat;
}


void bbox_way(const osm_way_t *w, struct bbox *bb)
{
   struct coord cd;
   osm_node_t *n;
   int i;

   if (w == NULL || bb == NULL)
      return;

   bb->ru.lon = -180;
   bb->ll.lon = 180;
   bb->ru.lat = -90;
   bb->ll.lat = 90;

   for (i = 0; i < w->ref_cnt; i++)
   {
      if ((n = get_object(OSM_NODE, w->ref[i])) == NULL)
      {
         log_msg(LOG_WARN, "node %ld in way %ld does not exist", (long) w->ref[i], (long) w->obj.id);
         continue;
      }
      cd.lat = n->lat;
      cd.lon = n->lon;
      bbox_min_max(&cd, bb);
   }
}


int act_exit_main(smrule_t * UNUSED(r), osm_obj_t * UNUSED(o))
{
   return raise(SIGINT);
}


#define EFMT_FMT -1
#define EFMT_LESSPARM -2


static int mk_fmt_str(char *buf, int len, const char *fmt, fparam_t **fp, const osm_obj_t *o)
{
   int cnt, n, prec, zero;
   char *key;
   double v;

   // safety check
   if (fp == NULL)
      return 0;

   for (len--, cnt = 0; *fmt != '\0' && len > 0; fmt++)
   {
      if (*fmt == '%')
      {
         fmt++;
         prec = zero = 0;
         // special case if '%' is at the end of the format string
         if (*fmt == '\0' || *fmt == '%')
         {
            *buf = '%';
            len--;
            buf++;
            cnt++;
            continue;
         }
         // FIXME: What's that?
         else if (*fmt == 'v')
         {
            *buf = ';';
            len--;
            buf++;
            cnt++;
            continue;
         }
         else if (*fmt == '0')
         {
            zero++;
            fmt++;
         }
         else if (*fmt >= '1' && *fmt <= '9')
         {
            prec = *fmt - '0';
            fmt++;
         }
         
         if (!prec)
            prec = 1;

         // find next tag in taglist
         for (key = NULL; *fp != NULL; fp++)
            if (!strcasecmp((*fp)->attr, "key"))
            {
               key = (*fp)->val;
               fp++;
               break;
            }

         // return if no tag exists
         if (key == NULL)
         {
            log_msg(LOG_ERR, "format string expects more keys");
            return EFMT_LESSPARM;
         }

         if ((n = match_attr(o, key, NULL)) == -1)
            return 0;

         switch (*fmt)
         {
            case 's':
               n = snprintf(buf, len, "%.*s", o->otag[n].v.len, o->otag[n].v.buf);
               break;

            case 'd':
               v = bs_tod(o->otag[n].v);
               n = snprintf(buf, len, "%ld", (long) v);
               break;

            case 'f':
               v = bs_tod(o->otag[n].v);
               n = snprintf(buf, len, "%f", v);
               break;

            case 'r':
               v = fmod(bs_tod(o->otag[n].v), 1.0) * pow(10, prec);
               n = snprintf(buf, len, "%ld", (long) round(v));
               break;

            default:
               log_msg(LOG_ERR, "error in format string");
               return EFMT_FMT;
         }
      }
      else
      {
         *buf = *fmt;
         n = 1;
      }

      cnt += n;
      buf += n;
      len -= n;
   }
   *buf = '\0';

   return cnt;
}


int act_strfmt_ini(smrule_t *r)
{
   struct fmt_info fi;

   if ((fi.addtag = get_param("addtag", NULL, r->act)) == NULL)
   {
      log_msg(LOG_WARN, "parameter 'addtag' missing");
      return 1;
   }

   if ((fi.addtag = strdup(fi.addtag)) == NULL)
   {
      log_msg(LOG_ERR, "strdup() failed in strfmt_ini(): %s", strerror(errno));
      return -1;
   }

   if ((fi.fmt = get_param("format", NULL, r->act)) == NULL)
   {
      log_msg(LOG_WARN, "parameter 'format' missing");
      return 1;
   }

   if ((r->data = malloc(sizeof(fi))) == NULL)
   {
      log_msg(LOG_ERR, "malloc failed in act_strfmt_ini(): %s", strerror(errno));
      return -1;
   }

   memcpy(r->data, &fi, sizeof(fi));
   return 0;
}


int act_strfmt_main(smrule_t *r, osm_obj_t *o)
{
   struct otag *ot;
   char buf[2048], *s;
   int len, n;

   if ((len = mk_fmt_str(buf, sizeof(buf), ((struct fmt_info*) r->data)->fmt, r->act->fp, o)) <= 0)
   {
      log_debug("mk_fmt_str() failed: %d", len);
      return len;
   }

   // check if desired tag already exists
   if ((n = match_attr(o, (char*) ((struct fmt_info*) r->data)->addtag, NULL)) == -1)
   {
      log_debug("adding tag '%s'='%s' to object %"PRId64,
            (char*) ((struct fmt_info*) r->data)->addtag, buf, o->id);
      if ((ot = realloc(o->otag, sizeof(*o->otag) * (o->tag_cnt + 1))) == NULL)
      {
         log_msg(LOG_ERR, "realloc() failed in strfmt(): %s", strerror(errno));
         return -1;
      }
      o->otag = ot;
      n = o->tag_cnt;
      o->tag_cnt++;
      o->otag[n].k.buf = (char*) ((struct fmt_info*) r->data)->addtag;
      o->otag[n].k.len = strlen(o->otag[n].k.buf);
   }
   else
   {
      log_debug("reusing tag '%s'=*", (char*) ((struct fmt_info*) r->data)->addtag);
   }


   if ((s = strdup(buf)) == NULL)
   {
      log_msg(LOG_ERR, "strdup() failed in strfmt(): %s", strerror(errno));
      return -1;
   }
   o->otag[n].v.buf = s;
   o->otag[n].v.len = len;

   return 0;
}


int act_strfmt_fini(smrule_t *r)
{
   free(r->data);
   r->data = NULL;
   return 0;
}


static int apply_subrules_way(smrule_t *r, osm_way_t *w)
{
   osm_obj_t *o;
   int i;

   for (i = 0; i < w->ref_cnt; i++)
   {
      if ((o = get_object(OSM_NODE, w->ref[i])) == NULL)
      {
         log_msg(LOG_ERR, "node %ld of way %ld does not exist", (long) w->obj.id, (long) w->ref[i]);
         continue;
      }
      apply_rule(o, r, NULL);
   }
   return 0;
}


static int apply_subrules(smrule_t *r, struct sub_handler *sh)
{
   static char *name = NULL;
   int e = 0;

   if (r == NULL)
   {
      log_msg(LOG_EMERG, "NULL pointer to rule, ignoring");
      return 1;
   }

   if (!r->oo->vis)
   {
      log_msg(LOG_INFO, "ignoring invisible rule %016lx", (long) r->oo->id);
      return 0;
   }

   if (sh == NULL || r->oo->ver != sh->version)
      return 0;

   if (name != r->act->func_name)
   {
      name = r->act->func_name;
      log_msg(LOG_INFO, "applying rule id 0x%"PRIx64" '%s'", r->oo->id, r->act->func_name);
   }

   if (r->act->main.func != NULL && !sh->finish)
   {
      if (sh->parent->type == OSM_WAY)
      {
         apply_subrules_way(r, (osm_way_t*) sh->parent);
      }
   }

   if (sh->finish)
      call_fini(r);

   return e;
}


int act_sub_ini(smrule_t *r)
{
   struct sub_handler *sh;

   if ((sh = calloc(1, sizeof(*sh))) == NULL)
   {
      log_msg(LOG_ERR, "failed to calloc() in sub(): %s", strerror(errno));
      return 1;
   }

   if (get_parami("version", &sh->version, r->act) == NULL)
   {
      log_msg(LOG_WARN, "parameter 'version' missing");
      return 1;
   }

   sh->rules = get_rdata()->rules;
   r->data = sh;

   return 0;
}


int act_sub_main(smrule_t *r, osm_obj_t *o)
{
   struct sub_handler *sh = r->data;

   if (o->type != OSM_WAY)
   {
      log_msg(LOG_WARN, "sub() is only available for ways yert");
      return 1;
   }

   sh->parent = o;
   //traverse(sh->rules, 0, IDX_REL, (tree_func_t) apply_subrules, (void*) (long) sh->version);
   //traverse(sh->rules, 0, IDX_WAY, (tree_func_t) apply_subrules, (void*) (long) sh->version);
   traverse(sh->rules, 0, IDX_NODE, (tree_func_t) apply_subrules, sh);

   return 0;
}


int act_sub_fini(smrule_t *r)
{
   struct sub_handler *sh = r->data;

   sh->parent = NULL;
   sh->finish = 1;
   traverse(sh->rules, 0, IDX_NODE, (tree_func_t) apply_subrules, sh);
   free(r->data);
   r->data = NULL;
   return 0;
}


int act_inherit_tags_ini(smrule_t *r)
{
   struct inherit_data *id;
   char *type;

   if ((id = calloc(1, sizeof(*id))) == NULL)
   {
      log_msg(LOG_ERR, "failed to calloc() in inherit_tags(): %s", strerror(errno));
      return -1;
   }

   id->rdata = get_rdata();
   id->force = get_param_bool("force", r->act);

   if ((type = get_param("object", NULL, r->act)) != NULL)
   {
      if (!strcasecmp(type, "way"))
         id->type = OSM_WAY;
      else if (!strcasecmp(type, "relation"))
         id->type = OSM_REL;
      else if (!strcasecmp(type, "node"))
         id->type = OSM_NODE;
      else
         log_msg(LOG_WARN, "unknown object type '%s'", type);
   }

   if ((type = get_param("direction", NULL, r->act)) != NULL)
   {
      if (!strcasecmp(type, "up"))
         id->dir = UP;
      else if (!strcasecmp(type, "down"))
         id->dir = DOWN;
      else
         log_msg(LOG_WARN, "unknown direction '%s', defaulting to UP", type);
   }

   if (id->type == OSM_NODE && id->dir == UP)
   {
      log_msg(LOG_WARN, "object type 'NODE' doesn't make sense together with direction 'UP'. Ignoring 'object'");
      id->type = 0;
   }

   if (id->dir == DOWN)
   {
      if (r->oo->type == OSM_NODE)
      {
         log_msg(LOG_WARN, "direction DOWN doesn't make sense on NODE rules. Ignoring rule.");
         free(id);
         return 1;
      }
      if (r->oo->type == OSM_WAY && id->type && id->type != OSM_NODE)
      {
         log_msg(LOG_WARN, "ways always have just nodes as parents. Ignoring 'object'");
         id->type = 0;
      }
   }

   // force Smrender to create reverse pointers
   id->rdata->need_index++;
   r->data = id;
   return 0;
}


/*! Copy a specific tag from OSM object src to object dst. It is tested if a
 * tag with the key as specificed by the index si in the list of tags of src
 * exists in dst. If not, it is added to dst, thereby increasing its tag count
 * (tag_cnt) by 1. If there is already such a tag it will be overwritten if
 * parameter force is not 0.
 *  @param src Pointer to source OSM object.
 *  @param dst Pointer to destination object.
 *  @param si Index of tag in source.
 *  @param force If force is not 0, already existing tags will be overwritten.
 *  @return The function returns 1 if a tag was added to dst, 2 if a tag in dst
 *  was overwritten, 0 if dst already has such a tag and it was not overwritten
 *  (because force == 0), or -1 in case of error (realloc(3)).
 */
static int copy_tag_cond(const osm_obj_t *src, osm_obj_t *dst, int si, int force)
{
   char tag[src->otag[si].k.len + 1];
   struct otag *ot;
   int m;

   memcpy(tag, src->otag[si].k.buf, src->otag[si].k.len);
   tag[src->otag[si].k.len] = '\0';
   // test if destination object has no such a key
   if ((m = match_attr(dst, tag, NULL)) < 0)
   {
      if ((ot = realloc(dst->otag, sizeof(*dst->otag) * (dst->tag_cnt + 1))) == NULL)
      {
         log_msg(LOG_ERR, "failed to realloc() in copy_tag_cond(): %s", strerror(errno));
         return -1;
      }
      dst->otag = ot;
      // copy tag
      dst->otag[dst->tag_cnt] = src->otag[si];
      dst->tag_cnt++;
      log_debug("adding tag %s to object(%d).id = %ld", tag, dst->type, (long) dst->id);
      return 1;
   }
   // otherwise overwrite if 'force' is set
   else if (force)
   {
      log_debug("overwriting tag %s to object(%d).id = %ld", tag, dst->type, (long) dst->id);
      dst->otag[m].v = src->otag[si].v;
      return 2;
   }
   return 0;
}
 

int act_inherit_tags_main(smrule_t *r, osm_obj_t *o)
{
   struct inherit_data *id = r->data;
   osm_obj_t *dummy = NULL, **optr = &dummy, *dst;
   //struct otag *ot;
   fparam_t **fp;
   int n, m;

   // reverse pointers are not use for upwards inheritance
   if (id->dir == UP)
     if ((optr = get_object0(id->rdata->index, o->id, o->type - 1)) == NULL)
      return 0;

   // safety check
   if (r->act->fp == NULL)
      return -1;

   // loop over parameter list of rule
   for (fp = r->act->fp; *fp != NULL; fp++)
   {
      // test if there is a 'key' parameter
      if (strcasecmp((*fp)->attr, "key"))
         continue;

      // test if source object has such a key
      if ((n = match_attr(o, (*fp)->val, NULL)) < 0)
         continue;

      if (id->dir == UP)
      {
         // loop over all reverse pointers
         for (; *optr != NULL; optr++)
         {
            // test if if tags should be copied to ways/relations only
            if (id->type && id->type != (*optr)->type)
               continue;

            copy_tag_cond(o, *optr, n, id->force);
#if 0
            // test if reverse object (destination) already has no such a key
            if ((m = match_attr(*optr, (*fp)->val, NULL)) < 0)
            {
               if ((ot = realloc((*optr)->otag, sizeof(*(*optr)->otag) * ((*optr)->tag_cnt + 1))) == NULL)
               {
                  log_msg(LOG_ERR, "failed to realloc() in inherit_tags(): %s", strerror(errno));
                  return -1;
               }
               (*optr)->otag = ot;
               // copy tag
               (*optr)->otag[(*optr)->tag_cnt] = o->otag[n];
               (*optr)->tag_cnt++;
               log_debug("adding tag %s to object(%d).id = %ld", (*fp)->val, (*optr)->type, (long) (*optr)->id);
            }
            // otherwise overwrite if 'force' is set
            else if (id->force)
            {
               log_debug("overwriting tag %s to object(%d).id = %ld", (*fp)->val, (*optr)->type, (long) (*optr)->id);
               (*optr)->otag[(*optr)->tag_cnt].v = o->otag[n].v;
            }
#endif
         } //for (; *optr != NULL; optr++)
      } //if (id->dir == UP)
      else // if (id->dir == DOWN)
      {
         if (o->type == OSM_REL)
         {
            for (m = 0; m < ((osm_rel_t*) o)->mem_cnt; m++)
            {
               // use only objects of specific type, if specified (object=*)
               if (id->type && id->type != ((osm_rel_t*) o)->mem[m].type)
                  continue;
               
               if ((dst = get_object(((osm_rel_t*) o)->mem[m].type, ((osm_rel_t*) o)->mem[m].id)) == NULL)
               {
                  log_debug("no such object");
                  continue;
               }

               copy_tag_cond(o, dst, n, id->force);
            }
         }
         else if (o->type == OSM_WAY)
         {
            for (m = 0; m < ((osm_way_t*) o)->ref_cnt; m++)
            {
               if ((dst = get_object(OSM_NODE, ((osm_way_t*) o)->ref[m])) == NULL)
               {
                  log_debug("no such object");
                  continue;
               }

               copy_tag_cond(o, dst, n, id->force);
            }
         }
      }
   }
   return 0;
}


int act_inherit_tags_fini(smrule_t *r)
{
   free(r->data);
   return 0;
}


int act_zeroway_ini(smrule_t *r)
{
   // force Smrender to create reverse pointers
   get_rdata()->need_index++;
   r->data = &get_rdata()->index;
   return 0;
}


int act_zeroway_fini(smrule_t *r)
{
   r->data = NULL;
   return 0;
}


/*! Check if id is first or last node of way.
 * @param w Pointer to way.
 * @param id Node id to check for.
 * @return 0 If id is first node. If id is the last node the positive index
 * number is returned (ref_cnt - 1). In case of error -1 is returned.
 */
static int first_or_last(osm_way_t *w, int64_t id)
{
   if (w->ref[0] == id)
      return 0;
   if (w->ref[w->ref_cnt - 1])
      return w->ref_cnt - 1;
   return -1;
}


/*! Find first way in list of rev pointers who has a the specified node at the
 * beginning or end.
 * @param optr List of rev pointers.
 * @param id Node id.
 * @param rev Pointer to integer which will receive the position of the id
 * within the way (see function first_or_last()).
 * @return Return the index number within the list of rev pointers which points
 * to the way. If no way matches the criteria -1 is returned.
 */
static int next_rev_way(osm_obj_t **optr, int64_t id, int *rev)
{
   int r;

   // find first way rev pointer in list of rev pointers.
   for (int i = 0; *optr != NULL; i++)
   {
      // ignore non-way rev pointers 
      if (optr[i]->type != OSM_WAY)
         continue;

      if ((r = first_or_last((osm_way_t*) optr[i], id)) != -1)
      {
         if (rev != NULL)
            *rev = r;
         return i;
      }
   }
   return -1;
}


/*! This function insert a way of length zero between two ways at the node
 * which connects those two ways. The new way will inherit all tags from the
 * node. This operation is carried out only if the node which was matched in
 * advance for this function to be called is an intermediate node as explained
 * before.
 */
int act_zeroway_main(smrule_t *r, osm_node_t *n)
{
   bx_node_t **idx_root = r->data;
   osm_obj_t **optr, **nptr, **pptr = NULL;
   int i, j, k, cnt, rev;
   osm_node_t *node;
   osm_way_t *w;

   log_debug("zeroway(%"PRId64")", n->obj.id);
   if ((optr = get_object0(*idx_root, n->obj.id, IDX_NODE)) == NULL)
   {
      log_debug("no rev pointers for node %"PRId64, n->obj.id);
      return 0;
   }

   if ((cnt = next_rev_way(optr, n->obj.id, NULL)) == -1)
   {
      log_debug("node %"PRId64" has no suitable way", n->obj.id);
      return 0;
   }

   for (node = NULL, j = 1, k = 0, nptr = NULL; (i = next_rev_way(optr + cnt + j, n->obj.id, &rev)) != -1; j++)
   {
      j += i;
      // In the first run of the loop create the new way (+node). If the node
      // connects several ways (and not just two), the same way is reused for
      // every other way.
      if (node == NULL)
      {
         // create new blind node
         node = malloc_node(1);
         osm_node_default(node);
         node->lat = n->lat;
         node->lon = n->lon;
         put_object((osm_obj_t*) node);

         // create new zero length way
         w = malloc_way(n->obj.tag_cnt + 1, 2);
         osm_way_default(w);
         memcpy(&w->obj.otag[1], &n->obj.otag[0], sizeof(*w->obj.otag) * n->obj.tag_cnt);
         w->ref[0] = n->obj.id;
         w->ref[1] = node->obj.id;
         put_object((osm_obj_t*) w);
         log_debug("new zeroway %"PRId64" created", w->obj.id);

         // create new reverse pointer list for origin node n
         // get number of elements and reserve new memory
         int ni = get_rev_index(optr, NULL);
         if ((pptr = malloc(sizeof(*pptr) * (ni + 2))) == NULL)
         {
            log_msg(LOG_ERR, "realloc() failed: %s", strerror(errno));
            return -1;
         }
         // copy reverse pointers
         memcpy(pptr, optr, sizeof(*pptr) * ni);
         // add reverse pointer to new way
         pptr[ni] = (osm_obj_t*) w;
         pptr[ni + 1] = NULL;
         // store list into index
         put_object0(idx_root, n->obj.id, pptr, IDX_NODE);

         // create reverse pointer list for new node
         if ((nptr = malloc(sizeof(*nptr) * (k + 2))) == NULL)
         {
            log_msg(LOG_ERR, "malloc() failed: %s", strerror(errno));
            return -1;
         }
         // add reverse pointer to way and store list into index
         nptr[k++] = (osm_obj_t*) w;
         nptr[k] = NULL;
         put_object0(idx_root, node->obj.id, nptr, IDX_NODE);
      }

      // modify way to to have new node as connecting node to new way
      ((osm_way_t*) optr[j])->ref[rev] = node->obj.id;
      log_debug("way %"PRId64" modified", ((osm_way_t*) optr[j])->obj.id);

      // update reverse pointer of new node
      if ((nptr = realloc(nptr, sizeof(*nptr) * (k + 2))) == NULL)
      {
         log_msg(LOG_ERR, "malloc() failed: %s", strerror(errno));
         return -1;
      }
      nptr[k] = optr[j];
      nptr[k + 1] = NULL;
      // store list into index (necassary because realloc() may change address)
      put_object0(idx_root, node->obj.id, nptr, IDX_NODE);
 
      // FIXME: if ways are member(s) of relations, new way is not added to
      // same relation(s)
   }

   // free old list of reverse pointers of origin node if new list was created
   if (pptr != NULL)
      free(optr);

   return 0;
}


int act_split_ini(smrule_t *r)
{
   // force Smrender to create reverse pointers
   get_rdata()->need_index++;
   r->data = get_rdata();
   return 0;
}


/*! Test if the way w has a reference to id.
 *  @param w Pointer to the way.
 *  @param id ID which is tested.
 *  @return Returns to the index of the reference (0 <= index < w->ref_cnt) or
 *  -1 if there is no reference.
 */
static int has_id(const osm_way_t *w, int64_t id)
{
   int i;

   for (i = 0; i < w->ref_cnt; i++)
      if (w->ref[i] == id)
         return i;
   return -1;
}


/*! This function updates the reverse pointers of the nodes after a way was
 *  split.
 *  @param idx_root Pointer to the root of the rev ptr object tree.
 *  @param org Pointer to the original part of the way after splitting.
 *  @param new Pointer to the new part of the way.
 *  @return Greater or equal 0 on success and -1 in case of error. If the
 *  return value is greater than 0 at list one rev ptr list was realloc'ed.
 */
static int update_rev_ptr(bx_node_t **idx_root, const osm_way_t *org, const osm_way_t *new)
{
   osm_obj_t **optr;
   int i, n, ret = 0;

   // safety check
   if (org == NULL || new == NULL)
   {
      log_msg(LOG_ERR, "NULL pointer caught in update_rev_ptr()");
      return -1;
   }

   for (i = 0; i < new->ref_cnt; i++)
   {
      // get rev ptr list for
      if ((optr = get_object0(*idx_root, new->ref[i], IDX_NODE)) == NULL)
      {
         log_msg(LOG_EMERG, "there is no reverse pointer, this may indicate a bug somewhere");
         if ((optr = malloc(sizeof(*optr))) == NULL)
         {
            log_msg(LOG_ERR, "malloc() failed: %s", strerror(errno));
            return -1;
         }
         *optr = NULL;
         put_object0(idx_root, new->ref[i], optr, IDX_NODE);
         ret++;
      }
 
      // check if node of way /new/ does not exist in way /org/
      if (has_id(org, new->ref[i]) == -1)
      {
         // get index to rev ptr of org
         n = get_rev_index(optr, &org->obj);
      }
      else
      {
         // get index to last ptr
         n = get_rev_index(optr, NULL);
         // add element to list
         if ((optr = realloc(optr, sizeof(*optr) * (n + 2))) == NULL)
         {
            log_msg(LOG_ERR, "realloc() failed: %s", strerror(errno));
            return -1;
         }
         optr[n + 1] = NULL;
         put_object0(idx_root, new->ref[i], optr, IDX_NODE);
         ret++;
      }
      // add rev ptr to way /new/
      optr[n] = (osm_obj_t*) new;
   }
   log_debug("ret = %d", ret);
   return ret;
}


int act_split_main(smrule_t *r, osm_node_t *n)
{
   osm_obj_t **optr;
   osm_way_t *w;
   int i;

   if (n->obj.type != OSM_NODE)
   {
      log_msg(LOG_WARN, "split() is only applicable to nodes");
      return 1;
   }

   if ((optr = get_object0(((struct rdata*) r->data)->index, n->obj.id, n->obj.type - 1)) == NULL)
      return 0;

   // loop over all reverse pointers
   for (; *optr != NULL; optr++)
   {
      // skip those which are not ways
      if ((*optr)->type != OSM_WAY)
         continue;

      // find index of node in way
      for (i = 0; i < ((osm_way_t*) (*optr))->ref_cnt; i++)
         if (((osm_way_t*) (*optr))->ref[i] == n->obj.id)
            break;

      // safety check
      if (i >= ((osm_way_t*) (*optr))->ref_cnt)
      {
         // ...SW bug somewhere...
         log_msg(LOG_EMERG, "node not found in reverse pointer to way. This should not happen!");
         continue;
      }

      // continue to next rev ptr if node is first/last
      if (!i || i == ((osm_way_t*) (*optr))->ref_cnt - 1)
      {
         log_msg(LOG_INFO, "way cannot be split at first/last node");
         continue;
      }

      /* split way */
      log_debug("splitting way %ld at ref index %d", (long) (*optr)->id, i);
      i++;
      // allocate new way
      w = malloc_way((*optr)->tag_cnt, ((osm_way_t*) (*optr))->ref_cnt - i + 1);
      osm_way_default(w);
      // copy all tags
      memcpy(w->obj.otag, (*optr)->otag, (*optr)->tag_cnt * sizeof(*(*optr)->otag));
      // copy all refs from this node up to the last node
      memcpy(w->ref, ((osm_way_t*) (*optr))->ref + i - 1, (((osm_way_t*) (*optr))->ref_cnt - i + 1) * sizeof(*((osm_way_t*) (*optr))->ref));
      // store new way
      put_object(&w->obj);
      // short original way
      ((osm_way_t*) (*optr))->ref_cnt = i;

      // update rev ptrs of nodes of new way
      switch (update_rev_ptr(&((struct rdata*) r->data)->index, (osm_way_t*) *optr, w))
      {
         case -1:
            return -1;
         case 0:
            break;
         default:
            log_debug("reloading optr");
            if ((optr = get_object0(((struct rdata*) r->data)->index, n->obj.id, n->obj.type - 1)) == NULL)
            {
               log_msg(LOG_EMERG, "something fatally went wrong...");
               return -1;
            }
      }
   }

   return 0;
}


int act_split_fini(smrule_t *r)
{
   r->data = NULL;
   return 0;
}


int act_incomplete_ini(smrule_t *r)
{
   char *name;

   if ((name = get_param("file", NULL, r->act)) == NULL)
   {
      log_msg(LOG_WARN, "incomplete() requires parameter 'file'");
      return 1;
   }

   if ((r->data = fopen(name, "w")) == NULL)
   {
      log_msg(LOG_WARN, "cannot open file %s: %s", name, strerror(errno));
      return 1;
   }

   return 0;
}


int act_incomplete_main(smrule_t *r, osm_rel_t *rel)
{
   if (rel->obj.type != OSM_REL)
   {
      log_msg(LOG_WARN, "incomplete() is only appicaple to relations");
      return 1;
   }

   for (int i = 0; i < rel->mem_cnt; i++)
      if (get_object(rel->mem[i].type, rel->mem[i].id) == NULL)
         fprintf(r->data, "%s/%"PRId64"\n", type_str(rel->mem[i].type), rel->mem[i].id);

   return 0;
}


int act_incomplete_fini(smrule_t *r)
{
   fclose(r->data);
   r->data = NULL;
   return 0;
}


int act_add_ini(smrule_t *r)
{
   struct rdata *rd = get_rdata();
   double latref, lonref;
#define UNITS_MM 1
#define UNITS_CM 10
   int units = 0;
   int pos;
   char *s;

   if (r->oo->type != OSM_NODE)
   {
      log_msg(LOG_WARN, "function add() only implemented for nodes, yet.");
      return 1;
   }

   if ((s = get_param("units", NULL, r->act)) != NULL)
   {
      if (!strcasecmp(s, "mm"))
         units = UNITS_MM;
      else if (!strcasecmp(s, "cm"))
         units = UNITS_CM;
      else if (strcasecmp(s, "degrees"))
         log_msg(LOG_WARN, "unknown unit '%s', defaulting to degrees", s);
   }

   pos = parse_alignment(r->act);
   switch (pos & 0x03)
   {
      case POS_M:
         latref = (rd->bb.ll.lat + rd->bb.ru.lat) / 2;
         break;
      case POS_N:
         latref = rd->bb.ru.lat;
         break;
      case POS_S:
         latref = rd->bb.ll.lat;
         break;
      default:
         log_msg(LOG_EMERG, "pos = 0x%02x this should never happen!", pos);
         return -1;
   }
   switch (pos & 0x0c)
   {
      case POS_C:
         lonref = (rd->bb.ll.lon + rd->bb.ru.lon) / 2;
         break;
      case POS_E:
         lonref = rd->bb.ru.lon;
         break;
      case POS_W:
         lonref = rd->bb.ll.lon;
         break;
      default:
         log_msg(LOG_EMERG, "pos = 0x%02x this should never happen!", pos);
         return -1;
   }

   if ((s = get_param("reference", NULL, r->act)) != NULL)
   {
      if (!strcasecmp(s, "relative"))
      {
         if (!units)
         {
            ((osm_node_t*) r->oo)->lat = latref + ((osm_node_t*) r->oo)->lat;
            ((osm_node_t*) r->oo)->lon = lonref + ((osm_node_t*) r->oo)->lon;
         }
         else
         {
            ((osm_node_t*) r->oo)->lat = latref + MM2LAT(((osm_node_t*) r->oo)->lat * units);
            ((osm_node_t*) r->oo)->lon = lonref + MM2LON(((osm_node_t*) r->oo)->lon * units);
         }
      }
      else if (strcasecmp(s, "absolute"))
         log_msg(LOG_WARN, "unknown reference '%s', defaulting to 'absolute'", s);
   }

   return 0;
}


int act_add_main(smrule_t * UNUSED(r), osm_obj_t *UNUSED(o))
{
   return 0;
}


int act_add_fini(smrule_t *r)
{

   osm_node_t *n = malloc_node(r->oo->tag_cnt + 1);
   osm_node_default(n);
   memcpy(&n->obj.otag[1], &r->oo->otag[0], r->oo->tag_cnt * sizeof(*r->oo->otag));
   n->lat = ((osm_node_t*) r->oo)->lat;
   n->lon = ((osm_node_t*) r->oo)->lon;
   put_object((osm_obj_t*) n);

   log_msg(LOG_INFO, "placing node to lat = %f, lon = %f", n->lat, n->lon);
   return 0;
}


int act_translate_ini(smrule_t *r)
{
   struct trans_data *td;
   fparam_t **fp;
   smrule_t *or;
   int64_t id;
   int i, j;
   char *s;

   // loop over parameter list of rule
   for (i = 0, j = 0, fp = r->act->fp; *fp != NULL; fp++)
      // test if there is a 'key' parameter
      if (!strcasecmp((*fp)->attr, "key"))
      {
         i++;
         j += strlen((*fp)->val);
      }

   if (!i)
   {
      log_msg(LOG_ERR, "mandatory param 'key' missing");
      return 1;
   }

   if ((s = get_param("id", NULL, r->act)) == NULL)
   {
      log_msg(LOG_ERR, "mandatory param 'id' missing");
      return 1;
   }

   errno = 0;
   id = strtol(s, NULL, 0);
   if (errno)
   {
      log_msg(LOG_ERR, "conversion failed: %s", strerror(errno));
      return 1;
   }

   if ((or = get_object0(get_rdata()->rules, id, r->oo->type - 1)) == NULL)
   {
      log_msg(LOG_WARN, "no template with id = %"PRId64, id);
      return 1;
   }

   if ((td = malloc(sizeof(*td) + i * (sizeof(*td->st) + sizeof(*td->ot)) + j + 1)) == NULL)
   {
      log_msg(LOG_ERR, "malloc() failed: %s", strerror(errno));
      return -1;
   }

   td->newtag = get_param_bool("newtag", r->act);
   td->o = or->oo;
   td->tag_cnt = i;
   td->st = (struct stag*) (td + 1);
   td->ot = (struct otag*) ((char*) (td + 1) + i * sizeof(*td->st));

   s = (char*) (td + 1) + i * (sizeof(*td->st) + sizeof(*td->ot));

   // loop over parameter list of rule
   for (i = 0, fp = r->act->fp; *fp != NULL; fp++)
   {
      // test if there is a 'key' parameter
      if (strcasecmp((*fp)->attr, "key"))
         continue;

      log_debug("parsing key value '%s'", (*fp)->val);
      strcpy(s, (*fp)->val);
      td->ot[i].k.buf = s;
      td->ot[i].k.len = strlen(s);
      td->ot[i].v.len = 0;
      s += td->ot[i].k.len;
      
      parse_matchtag(&td->ot[i], &td->st[i]);

      log_debug("key = '%.*s', type = %d", td->ot[i].k.len, td->ot[i].k.buf, td->st[i].stk.type);

      i++;
   }

   r->data = td;

   log_debug("found %d keys", i);

   return 0;
}


int act_translate_main(smrule_t *r, osm_obj_t *o)
{
   struct trans_data *td = r->data;
   struct stag st;
   struct otag ot, *tmp_ot;
   int i, n, m, ocnt;

   memset(&st, 0, sizeof(st));
   memset(&ot, 0, sizeof(ot));

   ocnt = td->tag_cnt;
   for (i = 0; i < ocnt; i++)
   {
      // test if object has such a key
      if ((n = bs_match_attr(o, &td->ot[i], &td->st[i])) < 0)
         continue;

      log_debug("match in tag(%d)@object(%"PRId64")", n, o->id);
      // copy value to temporary tag 'ot' as key
      ot.k = o->otag[n].v;
      // lookup if translation table object (in r->data) contains such key
      if ((m = bs_match_attr(td->o, &ot, &st)) < 0)
         continue;

      // add new tag if newtag is true
      if (td->newtag)
      {
         if ((tmp_ot = realloc(o->otag, sizeof(struct otag) * (o->tag_cnt + 1))) == NULL)
         {
            log_msg(LOG_DEBUG, "could not realloc tag list: %s", strerror(errno));
            return 0;
         }
         o->otag = tmp_ot;
         //o->otag[o->tag_cnt].v = td->o->otag[m].v;
         o->otag[o->tag_cnt].k.len = o->otag[n].k.len + 6;
         if ((o->otag[o->tag_cnt].k.buf = malloc(o->otag[o->tag_cnt].k.len)) == NULL)
         {
            log_msg(LOG_ERR, "malloc() failed: %s", strerror(errno));
            return -1;
         }
         memcpy(o->otag[o->tag_cnt].k.buf, o->otag[n].k.buf, o->otag[n].k.len);
         memcpy(o->otag[o->tag_cnt].k.buf + o->otag[n].k.len, ":local", 6);
         n = o->tag_cnt;
         o->tag_cnt++;
      }

      // translate, i.e. set object value to value of translation object
      o->otag[n].v = td->o->otag[m].v;
   }
   
   return 0;
}


int act_translate_fini(smrule_t *r)
{
   free(r->data);
   r->data = NULL;
   return 0;
}


/*! This function adds the node n to the node list nl. The memory within nl is
 * increased dynamically with realloc().
 * @param n Pointer to OSM node.
 * @param nl Pointer to valid node_list_t structure. If the list is empty the
 * contents of nl must be initialized to 0 before calling gather_node().
 * @return The function returns the number of nodes within nl after adding n.
 * On error, -1 is returned.
 */
static int gather_node(osm_node_t *n, node_list_t *nl)
{
   if (nl->node_cnt >= nl->max_cnt)
   {
      osm_node_t **tnl = realloc(nl->node, (nl->max_cnt + INC_MAX_NL) * sizeof(*nl->node));
      if (tnl == NULL)
      {
         log_msg(LOG_ERR, "realloc() failed: %s", strerror(errno));
         return -1;
      }
      nl->node = tnl;
      nl->max_cnt = nl->max_cnt + INC_MAX_NL;
   }

   nl->node[nl->node_cnt++] = n;
   return nl->node_cnt;
}


static struct pcoord *node_mtx(node_list_t *nl)
{
   struct pcoord *nmx;
   struct coord src, dst;

   log_debug("calculating node distance matrix");
   if ((nmx = malloc(sizeof(*nmx) * (nl->node_cnt * nl->node_cnt))) == NULL)
   {
      log_msg(LOG_ERR, "malloc(%ld kB) for matrix failed: %s", sizeof(*nmx) * nl->node_cnt * nl->node_cnt >> 10, strerror(errno));
      return NULL;
   }

   for (int i = 0; i < nl->node_cnt; i++)
   {
      src.lon = nl->node[i]->lon;
      src.lat = nl->node[i]->lat;
      memset(&nmx[i * nl->node_cnt], 0, sizeof(*nmx));
      for (int j = i + 1; j < nl->node_cnt; j++)
      {
         dst.lon = nl->node[j]->lon;
         dst.lat = nl->node[j]->lat;
         nmx[i * nl->node_cnt + j] = coord_diff(&src, &dst);
      }
   }

   return nmx;
}


static void rate_nmx(struct pcoord *nmx, int n, double dist)
{
   for (int i = 0; i < n; i++)
      for (int j = i + 1; j < n; j++)
      {
         // ignore nodes which have been masked
         if (nmx[i * n].dist < 0)
            continue;

         if (nmx[i * n + j].dist < dist)
            nmx[j * n].dist = MASK_NODE;
      }
}


static void mod_node(struct pcoord *nmx, node_list_t *nl)
{
   struct otag *ot;

   for (int i = 0; i < nl->node_cnt; i++)
   {
      if (nmx[i * nl->node_cnt].dist < 0)
      {
         if ((ot = realloc(nl->node[i]->obj.otag, sizeof(*nl->node[i]->obj.otag) * (nl->node[i]->obj.tag_cnt + 1))) == NULL)
         {
            log_msg(LOG_ERR, "realloc() failed: %s", strerror(errno));
            continue;
         }
         nl->node[i]->obj.otag = ot;
         set_const_tag(&nl->node[i]->obj.otag[nl->node[i]->obj.tag_cnt], "smrender:mask", "yes");
         nl->node[i]->obj.tag_cnt++;
      }
   }
}


int act_mask_ini(smrule_t *r)
{
   node_list_t *nl;
   
   if (r->oo->type != OSM_NODE)
   {
      log_msg(LOG_NOTICE, "mask() is implemented for nodes only, yet.");
      return 1;
   }

   if ((nl = malloc(sizeof(*nl))) == NULL)
   {
      log_msg(LOG_ERR, "malloc() failed: %s", strerror(errno));
      return -1;
   }
   memset(nl, 0, sizeof(*nl));

   nl->min_dist = NODE_MIN_DIST;
   if (get_param("distance", &nl->min_dist, r->act) != NULL)
   {
      if (nl->min_dist <= 0)
      {
         log_msg(LOG_NOTICE, "Distance must be positive value. Setting to default.");
         nl->min_dist = NODE_MIN_DIST;
      }
      else
         nl->min_dist /= 60;
   }

   r->data = nl;
   return 0;
}


int act_mask_main(smrule_t *r, osm_obj_t *o)
{
   gather_node((osm_node_t*) o, r->data);
   return 0;
}


void output_nmx(const struct pcoord *nmx, int n)
{
   for (int i = 0; i < n; i++)
   {
      fprintf(stderr, "%d: ", i);
      for (int j = 0; j < n; j++)
      {
         if (j && j < i)
            fprintf(stderr, "-/-, ");
         else
            fprintf(stderr, "%.1f/%.1f, ", nmx[i * n + j].dist * 60, nmx[i * n + j].bearing);
      }
      fprintf(stderr, "\n");
   }
}


int act_mask_fini(smrule_t *r)
{
   struct pcoord *nmx;
   node_list_t *nl = r->data;

   log_debug("gathered %d nodes", nl->node_cnt);
   if ((nmx = node_mtx(nl)) != NULL)
   {
      rate_nmx(nmx, nl->node_cnt, nl->min_dist);
      //output_nmx(nmx, nl->node_cnt);
      mod_node(nmx, nl);
      free(nmx);
   }

   free(nl->node);
   free(nl);
   r->data = NULL;
   return 0;
}


int act_del_match_tags_ini(smrule_t *r)
{
   r->data = 0;
   return 0;
}


int act_del_match_tags_main(smrule_t *r, osm_obj_t *o)
{
   int i, n;

   for (i = 0; i < r->oo->tag_cnt; i++)
   {
      if ((n = bs_match_attr(o, &r->oo->otag[i], &r->act->stag[i])) == -1)
         continue;
      memmove(&o->otag[n], &o->otag[n + 1], sizeof(o->otag[n]) * (o->tag_cnt - n - 1));
      o->tag_cnt--;
      r->data++;
   }

   return 0;
}


int act_del_match_tags_fini(smrule_t *r)
{
   log_debug("%ld tags deleted", (long) r->data);
   r->data = 0;
   return 0;
}


osm_node_t *next_available_node(const osm_way_t *w, int *ref)
{
   osm_node_t *n = NULL;

   // safety check
   if (w == NULL || ref == NULL)
   {
      log_msg(LOG_EMERG, "NULL pointer caught");
      return NULL;
   }

   // find first valid point (usually this is ref[0])
   for (; *ref < w->ref_cnt && n == NULL; (*ref)++)
      n = get_object(OSM_NODE, w->ref[*ref]);

   return n;
}


double course_diff(double a, double b)
{
   double y = b - a;

   if (y > 180)
      y -= 360;
   if (y < -180)
      y += 360;

   return y;
}


int act_bearings_ini(smrule_t *UNUSED(r))
{
   return 0;
}


int act_bearings_main(smrule_t *UNUSED(r), osm_way_t *w)
{
   struct pcoord pc[2];
   struct coord sc, dc;
   osm_node_t *n[3];
   double cd, pk;
   char buf[32];
   int i;

   if (w->obj.type != OSM_WAY)
   {
      log_msg(LOG_WARN, "may be applied to ways only!");
      return 1;
   }

   // find first valid point
   i = 0;
   if ((n[1] = next_available_node(w, &i)) == NULL)
   {
      log_msg(LOG_WARN, "no nodes of way available");
      return 1;
   }

   // find first valid point
   if ((n[2] = next_available_node(w, &i)) == NULL)
   {
      log_msg(LOG_WARN, "no nodes of way available");
      return 1;
   }

   for (; i < w->ref_cnt;)
   {
      n[0] = n[1];
      n[1] = n[2];

      // find first valid point
      if ((n[2] = next_available_node(w, &i)) == NULL)
      {
         log_msg(LOG_WARN, "no nodes of way available");
         return 1;
      }

      log_debug("allocating %d at %p for node %"PRId64, n[1]->obj.tag_cnt + 3, n[1]->obj.otag, n[1]->obj.id);
      if (realloc_tags(&n[1]->obj, n[1]->obj.tag_cnt + 3) == -1)
      {
         log_errno(LOG_ERR, "realloc_tags()");
         return -1;
      }

      sc.lat = n[0]->lat;
      sc.lon = n[0]->lon;
      dc.lat = n[1]->lat;
      dc.lon = n[1]->lon;
      pc[0] = coord_diff(&sc, &dc);
      sc = dc;
      dc.lat = n[2]->lat;
      dc.lon = n[2]->lon;
      pc[1] = coord_diff(&sc, &dc);

      cd = course_diff(pc[0].bearing, pc[1].bearing);
      pk = fmod2(pc[0].bearing - (180 - cd) / 2 + (cd < 0) * 180, 360);

      snprintf(buf, sizeof(buf), "%.1f", pc[1].bearing);
      set_const_tag(&n[1]->obj.otag[n[1]->obj.tag_cnt - 1], "smrender:bearing", strdup(buf));

      snprintf(buf, sizeof(buf), "%.1f", cd);
      set_const_tag(&n[1]->obj.otag[n[1]->obj.tag_cnt - 2], "smrender:coursedev", strdup(buf));

      snprintf(buf, sizeof(buf), "%.1f", pk);
      set_const_tag(&n[1]->obj.otag[n[1]->obj.tag_cnt - 3], "smrender:peakdir", strdup(buf));
   }

   return 0;
}


int act_bearings_fini(smrule_t *UNUSED(r))
{
   return 0;
}


int act_random_ini(smrule_t *r)
{
   struct random rnd;
   int n;

   if ((r->data = calloc(1, sizeof(rnd))) == NULL)
   {
      log_errno(LOG_ERR, "failed to allocate struct random");
      return -1;
   }

   // set random type, defaults to int
   rnd.type = get_param_bool("type", r->act);
   if (!rnd.type)
   {
      rnd.lo = 0;
      rnd.hi = RAND_MAX;

      // set low value, defaults to 0
      if (get_parami("lo", &n, r->act) != NULL)
         rnd.lo = n;
      if (get_parami("hi", &n, r->act) != NULL)
         rnd.hi = n;
   }
   else
   {
      // set low value, defaults to 0
      if (get_param("lo", &rnd.lod, r->act) == NULL)
         rnd.lod = 0;
      if (get_param("hi", &rnd.hid, r->act) == NULL)
         rnd.hid = 1;
   }

   if ((rnd.key = get_param("key", NULL, r->act)) == NULL)
      rnd.key = "smrender:random";

   log_debug("random params: type = %d, lo = %ld/%f, hi = %ld/%f, key = '%s'", rnd.type, rnd.lo, rnd.lod, rnd.hi, rnd.hid, rnd.key);

   *((struct random*) r->data) = rnd;
   return 0;
}


int act_random_main(smrule_t *r, osm_obj_t *o)
{
   struct random *rnd = r->data;
   char buf[32];
   int n;

   if (!rnd->type)
      snprintf(buf, sizeof(buf), "%ld", rnd->lo + random() % (rnd->hi - rnd->lo));
   else
      snprintf(buf, sizeof(buf), "%f", rnd->lod + (double) random() / RAND_MAX * (rnd->hid - rnd->lod));

   if ((n = match_attr(o, rnd->key, NULL)) < 0)
   {
      log_debug("key '%s' not found, allocating space", rnd->key);
      if (realloc_tags(o, o->tag_cnt + 1) == -1)
      {
         log_errno(LOG_ERR, "realloc_tags()");
         return -1;
      }
      n = o->tag_cnt - 1;
   }

   log_debug("setting key '%s' to '%s'", rnd->key, buf);
   set_const_tag(&o->otag[n], rnd->key, strdup(buf));
   return 0;
}


int act_random_fini(smrule_t *r)
{
   free(r->data);
   return 0;
}

