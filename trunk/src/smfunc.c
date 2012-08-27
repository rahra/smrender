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

#include "smrender_dev.h"


#define DIR_CW 0
#define DIR_CCW 1


//static FILE *output_handle_;
//static osm_obj_t *o_;


int act_out_ini(smrule_t *r)
{
   char *s;

   if ((s = get_param("file", NULL, r->act)) == NULL)
   {
      log_msg(LOG_WARN, "parameter 'file' missing");
      return 1;
   }

   if ((r->data = fopen(s, "w")) == NULL)
   {
      log_msg(LOG_ERR, "error opening output file %s: %s", s, strerror(errno));
      return 1;
   }
   fprintf(r->data, "<?xml version='1.0' encoding='UTF-8'?>\n<osm version='0.6' generator='smrender'>\n");
   return 0;
}


// FIXME: act_out violates the OSM rule of object order in an OSM file: nodes before ways before relations!
int act_out(smrule_t *r, osm_obj_t *o)
{
   osm_node_t *n;
   int i;

   if (r->data == NULL)
      return -1;

   if (o->type == OSM_WAY)
      for (i = 0; i < ((osm_way_t*) o)->ref_cnt; i++)
      {
         if ((n = get_object(OSM_NODE, ((osm_way_t*) o)->ref[i])) == NULL)
            continue;
         print_onode(r->data, (osm_obj_t*) n);
      }
   print_onode(r->data, o);

   return 0;
}


int act_out_fini(smrule_t *r)
{
   if (r->data == NULL)
      return 1;

   fprintf(r->data, "</osm>\n");
   fclose(r->data);

   r->data = NULL;
   return 0;
}


/*! Calculate the area and the centroid of a closed polygon.
 *  @param w Pointer to closed polygon.
 *  @param c Pointer to struct coord which will receive the coordinates of the
 *  centroid.
 *  @param ar Pointer to variable which will receive the area of the polygon.
 *  If ar is positive, the nodes of the polygon are order counterclockwise, if
 *  ar is negative they are ordered clockwise.
 *  The result is the area measured in nautical square miles.
 *  @return Returns 0 on success, -1 on error.
 */
int poly_area(const osm_way_t *w, struct coord *c, double *ar)
{
   double f, x[2];
   osm_node_t *n[2];
   int i;

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

   *ar = 0;
   c->lat = 0;
   c->lon = 0;

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
      c->lon += (x[0] + x[1]) * f;
      c->lat += (n[0]->lat + n[1]->lat) * f;
      *ar += f;
      //log_debug("%d %f %f %f %f %f %f %f/%f %f/%f", i, f, sx, sy, cx, cy, ar, n[0]->nd.lon, n[0]->nd.lat, n[1]->nd.lon, n[1]->nd.lat);
   }

   c->lat /= 3.0 * *ar;
   c->lon /= 3.0 * *ar * cos(DEG2RAD(c->lat));
   *ar = *ar * 1800.0;

   return 0;
}


int act_poly_area(smrule_t *r, osm_way_t *w)
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
      snprintf(buf, sizeof(buf), "%.8f", ar);
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


int act_poly_centroid(smrule_t *r, osm_way_t *w)
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
   n->obj.id = unique_node_id();
   n->obj.ver = 1;
   n->obj.tim = time(NULL);
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


int act_reverse_way(smrule_t *r, osm_way_t *w)
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
      return act_reverse_way(NULL, w);

   return 0;
}


int act_set_ccw(smrule_t *r, osm_way_t *w)
{
   return set_way_direction(w, DIR_CCW);
}


int act_set_cw(smrule_t *r, osm_way_t *w)
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
      log_msg(LOG_WARN, "there is no rule of type %d with id 0x%016x", r->oo->type, templ_id);
      return 1;
   }

   if ((r->data = rule->oo) == NULL)
   {
      log_msg(LOG_CRIT, "rule has no object");
      return 1;
   }

   return 0;
}


int act_set_tags(smrule_t *r, osm_obj_t *o)
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
         as->shape = SHP_TRIANGLE;
         as->pcount = 3;
      }
      else if (!strcmp(s, "square"))
      {
         as->shape = SHP_SQUARE;
         as->pcount = 4;
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

   log_debug("shape nodes = %d", as->pcount);

   if (get_param("size", &as->size, r->act) == NULL)
   {
      log_msg(LOG_WARN, "action 'shape' requires parameter 'size', defaults to 1.0mm");
      as->size = 1.0;
   }

   (void) get_param("angle", &as->angle, r->act);

   r->data = as;
   return 0;
}


void shape_circle(struct act_shape *as, osm_node_t *n)
{
   rdata_t *rd = get_rdata();
   osm_node_t *nd[as->pcount];
   double radius, angle, angle_step;
   osm_way_t *w;
   int i;

   radius = MM2LAT(as->size);
   angle = DEG2RAD(as->angle);
   angle_step = 2 * M_PI / as->pcount;

   w = malloc_way(n->obj.tag_cnt + 1, as->pcount + 1);
   w->obj.id = unique_way_id();
   w->obj.ver = 1;
   set_const_tag(w->obj.otag, "generator", "smrender");
   memcpy(&w->obj.otag[1], n->obj.otag, sizeof(struct otag) * n->obj.tag_cnt);

   log_debug("generating shape way %ld with %d nodes", (long) w->obj.id, as->pcount);

   for (i = 0; i < as->pcount; i++)
   {
         nd[i] = malloc_node(1);
         nd[i]->lat = n->lat + radius * cos(angle + angle_step * i);
         nd[i]->lon = n->lon - radius * sin(angle + angle_step * i) / cos(DEG2RAD(n->lat)); 
         nd[i]->obj.id = w->ref[i] = unique_node_id();
         nd[i]->obj.ver = 1;
         set_const_tag(nd[i]->obj.otag, "generator", "smrender");
         put_object((osm_obj_t*) nd[i]);
   }
   w->ref[i] = nd[0]->obj.id;
   put_object((osm_obj_t*) w);
}


int act_shape(smrule_t *r, osm_obj_t *o)
{
   if (o->type != OSM_NODE)
   {
      log_msg(LOG_NOTICE, "shape() on objects other then OSM_NODE not supported yet");
      return 1;
   }

   shape_circle(r->data, (osm_node_t*) o);
   return 0;
}


int act_shape_fini(smrule_t *r)
{
   free(r->data);
   r->data = NULL;
   return 0;
}

