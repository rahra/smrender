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

/*! 
 * 
 *
 *  @author Bernhard R. Fischer
 */
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "smrender_dev.h"


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


/*! Calculate the area and the centroid of a closed polygon.
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


int act_poly_area_main(smrule_t *r, osm_way_t *w)
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


int act_poly_centroid_main(smrule_t *r, osm_way_t *w)
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

   snprintf(buf, sizeof(buf), "%ld", (long) w->obj.id);
   if ((s = strdup(buf)) == NULL)
   {
      free_obj((osm_obj_t*) n);
      log_msg(LOG_DEBUG, "could not strdup: %s", strerror(errno));
      return 0;
   }
   set_const_tag(&n->obj.otag[0], "smrender:id:way", s);
   memcpy(&n->obj.otag[1], &w->obj.otag[0], sizeof(struct otag) * w->obj.tag_cnt);
   put_object((osm_obj_t*) n);
 
   //log_debug("centroid %.3f/%.3f, ar = %f, way = %ld, node %ld", n->lat, n->lon, ar, w->obj.id, n->obj.id);

   return 0;
}


int act_reverse_way_main(smrule_t *r, osm_way_t *w)
{
   int i;
   int64_t ref;

   if (!is_closed_poly(w))
      return 0;

   for (i = 1; i <= w->ref_cnt / 2 - 1; i++)
   {
      ref = w->ref[i];
      w->ref[i] = w->ref[w->ref_cnt - i - 1];
      w->ref[w->ref_cnt - i - 1] = ref;
   }
   return 0;
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
      return act_reverse_way_main(NULL, w);

   return 0;
}


int act_set_ccw_main(smrule_t *r, osm_way_t *w)
{
   return set_way_direction(w, DIR_CCW);
}


int act_set_cw_main(smrule_t *r, osm_way_t *w)
{
   return set_way_direction(w, DIR_CW);
}


int act_set_tags_ini(smrule_t *r)
{
   smrule_t *rule;
   int64_t templ_id;
   char *s;

   if ((s = get_param("id", NULL, r->act)) == NULL)
   {
      log_msg(LOG_WARN, "set_tags requires parameter 'id'");
      return -1;
   }

   errno = 0;
   templ_id = strtol(s, NULL, 0);
   if (errno)
   {
      log_msg(LOG_WARN, "cannot convert id: %s", strerror(errno));
      return -1;
   }

   if ((rule = get_object0(get_rdata()->rules, templ_id, r->oo->type - 1)) == NULL)
   {
      log_msg(LOG_WARN, "there is no rule of type %d with id 0x%016lx", r->oo->type, templ_id);
      return 1;
   }

   if ((r->data = rule->oo) == NULL)
   {
      log_msg(LOG_CRIT, "rule has no object");
      return 1;
   }

   return 0;
}


int act_set_tags_main(smrule_t *r, osm_obj_t *o)
{
   osm_obj_t *templ_o = r->data;
   struct otag *ot;

   if (templ_o == NULL)
   {
      log_msg(LOG_CRIT, "NULL pointer to template object");
      return -1;
   }

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


int act_shape_ini(smrule_t *r)
{
   struct act_shape *as;
   double pcount = 0.0;
   char *s = "";

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

   log_debug("nodes = %d, radius = %.2f, angle = %.2f, key = '%s'", as->pcount, as->size, as->angle, as->key != NULL ? as->key : "(NULL)");

   r->data = as;
   return 0;
}


void shape_node(const struct act_shape *as, const osm_node_t *n)
{
   struct rdata *rd = get_rdata();
   osm_node_t *nd[as->pcount];
   double radius, angle, angle_step;
   osm_way_t *w;
   int i;

   angle = 0.0;
   if (as->key != NULL)
   {
      if ((i = match_attr((osm_obj_t*) n, as->key, NULL)) >= 0)
      {
         angle = DEG2RAD(90.0 - bs_tod(n->obj.otag[i].v));
         log_debug("shape bearing %.1f", 90 - RAD2DEG(angle));
      }
      else
      {
         log_msg(LOG_INFO, "node %ld has no tag '%s=*'", (long) n->obj.id, as->key);
      }
   }

   radius = MM2LAT(as->size);
   angle += DEG2RAD(as->angle);
   angle_step = 2 * M_PI / as->pcount;

   w = malloc_way(n->obj.tag_cnt + 1, as->pcount + 1);
   osm_way_default(w);
   memcpy(&w->obj.otag[1], n->obj.otag, sizeof(struct otag) * n->obj.tag_cnt);

   log_debug("generating shape way %ld with %d nodes", (long) w->obj.id, as->pcount);

   for (i = 0; i < as->pcount; i++)
   {
         nd[i] = malloc_node(1);
         osm_node_default(nd[i]);
         nd[i]->lat = n->lat + radius * cos(angle + angle_step * i);
         nd[i]->lon = n->lon - radius * sin(angle + angle_step * i) / cos(DEG2RAD(n->lat)); 
         w->ref[i] = nd[i]->obj.id;
         put_object((osm_obj_t*) nd[i]);
   }
   w->ref[i] = nd[0]->obj.id;
   put_object((osm_obj_t*) w);
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
#define DEFAULT_DISTANCE 2.0
   double *dist;

   if ((dist = malloc(sizeof(*dist))) == NULL)
   {
      log_msg(LOG_ERR, "malloc() failed in act_ins_eqdist_ini(): %s", strerror(errno));
      return -1;
   }

   r->data = dist;

   if (get_param("distance", dist, r->act) == NULL)
   {
      *dist = DEFAULT_DISTANCE;
   }
   else if (*dist <= 0)
   {
      *dist = DEFAULT_DISTANCE;
   }

   *dist /= 60.0;
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
   for (i = 0; i < w->ref_cnt - 1; i++)
   {
      if ((s = get_object(OSM_NODE, w->ref[i])) != NULL)
         break;
      log_msg(LOG_WARN, "node %ld of way %ld does not exist", (long) w->ref[i], (long) w->obj.id);
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


int act_dist_median_main(smrule_t *r, osm_way_t *w)
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


int obj_exists(osm_obj_t *o, struct rdata *rd, struct out_handle *oh)
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
   traverse(ioh->itree, 0, IDX_NODE, (tree_func_t) obj_exists, NULL, ioh->oh);
   log_debug("traversing ways");
   traverse(ioh->itree, 0, IDX_WAY, (tree_func_t) obj_exists, NULL, ioh->oh);
   log_debug("traversing relations");
   traverse(ioh->itree, 0, IDX_REL, (tree_func_t) obj_exists, NULL, ioh->oh);

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


int act_poly_len_main(smrule_t *r, osm_way_t *w)
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
int act_sync_threads_ini(smrule_t *r)
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
int act_disable_main(smrule_t *r, osm_obj_t *o)
{
   o->vis = 0;
   return 0;
}


/*! Enable object, i.e. set visibility to 1.
 */
int act_enable_main(smrule_t *r, osm_obj_t *o)
{
   o->vis = 1;
   return 0;
}


int act_enable_rule_ini(smrule_t *r)
{
   return parse_id(r);
}


int act_enable_rule_main(smrule_t *r, osm_obj_t *o)
{
   return act_enable_main(r, r->data);
}


int act_disable_rule_ini(smrule_t *r)
{
   return parse_id(r);
}


int act_disable_rule_main(smrule_t *r, osm_obj_t *o)
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

