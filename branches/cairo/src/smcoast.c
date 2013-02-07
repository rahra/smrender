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

/*! This file contains the code which is used to close open polygons. Open
 * polygons obviously cannot be filled, thus the must be closed before. Open
 * polygons occure at the edges of the boundbox which is used to select data out
 * of the OSM database. This is one of the most difficult parts at all.
 *
 *  @author Bernhard R. Fischer
 */

/*
 * 1) gather all open polygons
 * 2) Create pdef list which contains all end points of open polygons (pdef_cnt = open_poly_cnt * 2)
 * 3) Retrieve node ids from those points (each start and end point)
 * 4) Sort pdef list by node id.
 * 5) Set prev/next pointers in points of pdef list at neighboring points, i.e. if one start point has the same node id as the neighboring end point.
 * 6) loop over all ways (loop_detect() returns number of open ways)
 * 6.1) count nodes of "connected" (pointered) ways and detect if there is a loop (count_poly_ref() = 1 if loop, 0 if unclosed)
 * 6.2) create new way with the according number of nodes (node count = sum of all connected ways) (create_new_coastline())
 * 6.3) copy node ids of all connected ways to newly created way (join_open_poly()). Mark ways which have been processed as deleteable (from list). Mark those which are still open (i.e. not already looped) as open.
 * 6.4) put new way to way pool.
 * 7) Free list of pdef
 * 8) Trim open ways to edges of page
 * 9) create new pdef list with number of still open ways.
 * 10) add all end points to pdef list and calculate bearing from center point (poly_get_brg() returns number of open ways)
 * 11) sort points by bearing
 * 12) connect_open()
 * 12.1)
 *
 */
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <math.h>

#include "smrender_dev.h"
#include "smcoast.h"

struct refine
{
   double deviation;
   int iteration;
};


static void node_brg(struct pcoord*, struct coord*, int64_t);


static struct corner_point co_pt_[4];
static struct coord center_;



/*! Check if way is a closed polygon and is an area (i.e. it has at least 4 points)
 *  @param w Pointer to way.
 *  @return 1 if it is a closed area, 0 otherwise.
 */
int is_closed_poly(const osm_way_t *w)
{
   // check if it is an open polygon
   if (w->ref_cnt < 4)
      return 0;

   if (w->ref[0] != w->ref[w->ref_cnt - 1])
      return 0;

   return 1;
}


/*! This collects open polygons and adds the node references to the wlist
 * structure.
 */
int gather_poly0(osm_way_t *w, struct wlist **wl)
{
   // check if there's enough memory
   if ((*wl)->ref_cnt >= (*wl)->max_ref)
   {
      if ((*wl = realloc(*wl, sizeof(struct wlist) + ((*wl)->max_ref + INIT_MAX_REF) * sizeof(struct poly))) == NULL)
         perror("realloc"), exit(EXIT_FAILURE);
      (*wl)->max_ref += INIT_MAX_REF;
   }

   // add way to list
   memset(&(*wl)->ref[(*wl)->ref_cnt], 0, sizeof(struct poly));
   (*wl)->ref[(*wl)->ref_cnt].w = w;
   (*wl)->ref_cnt++;
   return 0;
}


/*! This function retrieves the node ids from the start/end nodes of all ways
 * and stores it into a list of pdef structures. The list must be freed again
 * later.
 *
 * @param wl Pointer to wlist containing all open polygons.
 * @return Pointer to pdef containg all nodes. Obviously it is of length wl->ref_cnt * 2.
 */
static struct pdef *poly_get_node_ids(const struct wlist *wl)
{
   int i;
   struct pdef *pd;

   if ((pd = calloc(wl->ref_cnt * 2, sizeof(struct pdef))) == NULL)
   {
      log_msg(LOG_EMERG, "poly_sort_by_node(): %s", strerror(errno));
      exit(EXIT_FAILURE);
   }

   for (i = 0; i < wl->ref_cnt; i++)
   {
      pd[i].wl_index = i;
      pd[i].pn = 0;
      pd[i].nid = wl->ref[i].w->ref[0];
      pd[i + wl->ref_cnt].wl_index = i;
      pd[i + wl->ref_cnt].pn = wl->ref[i].w->ref_cnt - 1;
      pd[i + wl->ref_cnt].nid = wl->ref[i].w->ref[wl->ref[i].w->ref_cnt - 1];
   }

   return pd;
}


static int poly_get_brg(struct pdef *pd, struct wlist *wl, int ocnt)
{
   int i, j;

   for (i = 0, j = 0; (i < wl->ref_cnt) && (j < ocnt); i++)
   {
      if (!wl->ref[i].open)
         continue;

      node_brg(&pd[j].pc, &center_, wl->ref[i].w->ref[0]);
      pd[j].wl_index = i;
      pd[j].pn = 0;
      node_brg(&pd[j + ocnt].pc, &center_, wl->ref[i].w->ref[wl->ref[i].w->ref_cnt - 1]);
      pd[j + ocnt].wl_index = i;
      pd[j + ocnt].pn = wl->ref[i].w->ref_cnt - 1;
      j++;
   }

   return j;
}


static int octant(const struct coord *crd)
{
   int pos = 0;

   if (crd->lat > co_pt_[0].n->lat)
      pos |= POS_N;
   else if (crd->lat < co_pt_[1].n->lat)
      pos |= POS_S;

   if (crd->lon > co_pt_[0].n->lon)
      pos |= POS_E;
   else if (crd->lon < co_pt_[3].n->lon)
      pos |= POS_W;

   return pos;
}


static int64_t edge_point(struct coord crd, int pos, int64_t nid)
{
   osm_node_t *n;

   //log_debug("trimming way %ld, %d - %d out of page, octant = 0x%02x", (long) w->obj.id, 0, i - 1, p[0]);
   // FIXME: inserting corner points is not really correct
   // Bearing between inner point and out pointer should be compare to
   // the bearing from the inner point to the corner point.
   if ((pos & POS_N) && (pos & POS_E))
   {
      return co_pt_[0].n->obj.id;
   }
   else if ((pos & POS_S) && (pos & POS_E))
   {
      return co_pt_[1].n->obj.id;
   }
   else if ((pos & POS_S) && (pos & POS_W))
   {
      return co_pt_[2].n->obj.id;
   }
   else if ((pos & POS_N) && (pos & POS_W))
   {
      return co_pt_[3].n->obj.id;
   }
   else 
   {
      // FIXME: coordinates of new edge points are not correct. They deviate from
      // their intended location.
      n = get_object(OSM_NODE, nid);
      switch (pos)
      {
         case POS_N:
            crd.lon += (n->lon - crd.lon) * (n->lat - co_pt_[0].n->lat) / (n->lat - crd.lat);
            crd.lat = co_pt_[0].n->lat;
            break;
         case POS_S:
            crd.lon += (n->lon - crd.lon) * (n->lat - co_pt_[1].n->lat) / (n->lat - crd.lat);
            crd.lat = co_pt_[1].n->lat;
            break;
         case POS_E:
            crd.lat += (n->lat - crd.lat) * (n->lon - co_pt_[0].n->lon) / (n->lon - crd.lon);
            crd.lon = co_pt_[0].n->lon;
            break;
         case POS_W:
            crd.lat += (n->lat - crd.lat) * (n->lon - co_pt_[3].n->lon) / (n->lon - crd.lon);
            crd.lon = co_pt_[3].n->lon;
            break;
         default:
            log_msg(LOG_EMERG, "octant not allowed: 0x%02x", pos);
            return 0;
      }
      n = malloc_node(1);
      osm_node_default(n);
      n->lat = crd.lat;
      n->lon = crd.lon;
      put_object((osm_obj_t*) n);
//        w->ref[0] = n->obj.id;
//         log_debug("added new edge point %ld at lat = %f, lon = %f", (long) n->obj.id, n->lat, n->lon);
      return n->obj.id;
   }
}


static int trim_way_rev(osm_way_t *w)
{
   struct coord crd;
   osm_node_t *n;
   int i, p[2] = {0, 0};
   int64_t nid;

   for (i = w->ref_cnt - 1; i >= 0; i--)
   {
      if ((n = get_object(OSM_NODE, w->ref[i])) == NULL)
      {
         log_msg(LOG_ERR, "node %ld in way %ld does not exist", (long) w->ref[i], (long) w->obj.id);
         return -1;
      }

      crd.lat = n->lat;
      crd.lon = n->lon;
      p[0] = p[1];
      if (!(p[1] = octant(&crd)))
         break;
   }

   if (i < 0)
   {
      log_msg(LOG_ERR, "unhandled error: all nodes of way %ld are outside the page", (long) w->obj.id);
      return -1;
   }

   if (i < w->ref_cnt - 1)
   {
      log_debug("trimming way %ld, %d - %d out of page, octant = 0x%02x", (long) w->obj.id, w->ref_cnt - 1, i + 1, p[0]);

      if (!(nid = edge_point(crd, p[0], w->ref[i + 1])))
         return -1;

      w->ref[i + 1] = nid;
      log_debug("added new edge point %ld", (long) nid);

      //memmove(&w->ref[1], &w->ref[i], sizeof(int64_t) * (w->ref_cnt - i));
      w->ref_cnt = i + 2;
   }

   return w->ref_cnt - 1 - i;
}


static int trim_way(osm_way_t *w)
{
   struct coord crd;
   osm_node_t *n;
   int i, p[2] = {0, 0};
   int64_t nid;

   for (i = 0; i < w->ref_cnt; i++)
   {
      if ((n = get_object(OSM_NODE, w->ref[i])) == NULL)
      {
         log_msg(LOG_ERR, "node %ld in way %ld does not exist", (long) w->ref[i], (long) w->obj.id);
         return -1;
      }

      crd.lat = n->lat;
      crd.lon = n->lon;
      p[0] = p[1];
      if (!(p[1] = octant(&crd)))
         break;
   }

   if (i >= w->ref_cnt)
   {
      log_msg(LOG_ERR, "unhandled error: all nodes of way %ld are outside the page", (long) w->obj.id);
      return -1;
   }

   if (i)
   {
      log_debug("trimming way %ld, %d - %d out of page, octant = 0x%02x", (long) w->obj.id, 0, i - 1, p[0]);

      if (!(nid = edge_point(crd, p[0], w->ref[i - 1])))
         return -1;

      w->ref[0] = nid;
      log_debug("added new edge point %ld", (long) nid);

      memmove(&w->ref[1], &w->ref[i], sizeof(int64_t) * (w->ref_cnt - i));
      w->ref_cnt -= i - 1;
   }

   return i;
}


static void trim_ways(struct wlist *wl, int ocnt)
{
   int i, j;

   for (i = 0, j = 0; (i < wl->ref_cnt) && (j < ocnt); i++)
   {
      if (!wl->ref[i].open)
         continue;

      if (trim_way(wl->ref[i].w) > 0)
         log_debug("wl_index = %d", i);

      if (trim_way_rev(wl->ref[i].w) > 0)
         log_debug("wl_index = %d", i);
   }
}


static int poly_find_adj2(struct wlist *wl, struct pdef *pd)
{
   int i, n;

   log_debug("%d unconnected ends", wl->ref_cnt * 2);
   for (i = 0, n = 0; i < wl->ref_cnt * 2 - 1; i++)
   {
      if (pd[i].nid == pd[i + 1].nid)
      {
         //log_debug("wl_index %d to %d belong together", i, i + 1);
         wl->ref[pd[i + 1].wl_index].next = &wl->ref[pd[i].wl_index];
         wl->ref[pd[i].wl_index].prev = &wl->ref[pd[i + 1].wl_index];
         n++;
      }
   }

   return n;
}


/*! This function detects if a way is already closed (loop) and determines the
 * total number of nodes.
 * @param pl Pointer to beginning of ways.
 * @param cnt Pointer to integer which will reveive the number of nodes.
 * @return The function returns 1 of it is a loop, 0 if it is unclosed, and -1 on error.
 */
static int count_poly_refs(struct poly *pl, int *cnt)
{
   struct poly *list;

   if (pl == NULL)
   {
      log_msg(LOG_WARN, "count_poly_refs() called with NULL pointer");
      return -1;
   }

   for (list = pl, *cnt = 0; list != NULL; list = list->next)
   {
      *cnt += list->w->ref_cnt - 1;
      //log_debug("%p %ld", list, list->w->nd.id);
      if (list->next == pl)
         break;
   }

   (*cnt)++;
   return list != NULL;
}


static osm_way_t *create_new_coastline(const osm_obj_t *o, int ref_cnt)
{
   osm_way_t *w;

   if (o == NULL)
   {
      w = malloc_way(1, ref_cnt);
   }
   else
   {
      w = malloc_way(o->tag_cnt + 1, ref_cnt);
      memcpy(&w->obj.otag[1], &o->otag[0], sizeof(struct otag) * o->tag_cnt);
   }

   osm_way_default(w);

   return w;
}


static int join_open_poly(struct poly *pl, osm_way_t *w)
{
   int pos, wcnt = 0;
   struct poly *list;

   for (list = pl, pos = 0; list != NULL; list = list->next, wcnt++)
   {
      // copy all node refs...
      memcpy(&w->ref[pos], list->w->ref, list->w->ref_cnt * sizeof(int64_t));
      // ...but increase position by one less to overwrite last element with next
      pos += list->w->ref_cnt - 1;

      if (list->del)
         log_debug("%p is already past from other way!", list);

      list->del = 1;

      if (list->next == pl)
      {
         wcnt++;
         break;
      }
   }

   return wcnt;
}


static int loop_detect(const osm_obj_t *o, struct wlist *wl)
{
   osm_way_t *w;
   int i, cnt, ret, ocnt = 0;

   for (i = 0; i < wl->ref_cnt; i++)
   {
      if (wl->ref[i].del)
         continue;

      if ((ret = count_poly_refs(&wl->ref[i], &cnt)) == -1)
      {
         log_msg(LOG_WARN, "something went wrong in count_poly_refs()");
         continue;
      }

      // check if way is intermediate way and continue in that case
      if (!ret && (wl->ref[i].prev != NULL))
      {
         //log_debug("way on wl_index %d is intermediate way", i);
         continue;
      }

      log_debug("waylist: wl_index %d (start = %p, cnt = %d, loop = %d)", i, &wl->ref[i], cnt, ret);
      w = create_new_coastline(o, cnt);
      cnt = join_open_poly(&wl->ref[i], w);
      put_object((osm_obj_t*) w);
      log_debug("%d ways joined", cnt);

      // if it is not a loop, than it is a starting open way
      if (!ret)
      {
         wl->ref[i].open = 1;
         wl->ref[i].w = w;
         ocnt++;
      }
   }

   return ocnt;
}


static int compare_pdef_nid(const struct pdef *p1, const struct pdef *p2)
{
   if (p1->nid < p2->nid) return -1;
   if (p1->nid > p2->nid) return 1;

   if (p1->pn < p2->pn) return -1;
   if (p1->pn > p2->pn) return 1;

   return 0;
}


static int compare_pdef(const struct pdef *p1, const struct pdef *p2)
{
   if (p1->pc.bearing < p2->pc.bearing) return -1;
   if (p1->pc.bearing > p2->pc.bearing) return 1;
   return 0;
}


static void init_corner_brg(const struct rdata *rd, const struct coord *src, struct corner_point *co_pt)
{
   struct coord corner_coord[4] = {rd->bb.ru, {rd->bb.ll.lat, rd->bb.ru.lon}, rd->bb.ll, {rd->bb.ru.lat, rd->bb.ll.lon}};
   osm_way_t *w;
   int i;

   w = malloc_way(2, 5);
   osm_way_default(w);
   for (i = 0; i < 4; i++)
   {
      co_pt[i].pc = coord_diff(src, &corner_coord[i]);
      co_pt[i].n = malloc_node(2);
      osm_node_default(co_pt[i].n);
      co_pt[i].n->lat = corner_coord[i].lat;
      co_pt[i].n->lon = corner_coord[i].lon;
      set_const_tag(&co_pt[i].n->obj.otag[1], "grid", "pagecorner");
      put_object((osm_obj_t*) co_pt[i].n);
      log_msg(LOG_DEBUG, "corner_point[%d].bearing = %f (id = %ld)", i, co_pt[i].pc.bearing, co_pt[i].n->obj.id);

      w->ref[3 - i] = co_pt[i].n->obj.id;
   }

   w->ref[4] = w->ref[0];
   w->ref_cnt = 5;
   set_const_tag(&w->obj.otag[1], "border", "page");
   put_object((osm_obj_t*) w);
}


static void node_brg(struct pcoord *pc, struct coord *src, int64_t nid)
{
   osm_node_t *n;
   struct coord dst;

   if ((n = get_object(OSM_NODE, nid)) == NULL)
      return;
   dst.lat = n->lat;
   dst.lon = n->lon;
   *pc = coord_diff(src, &dst);
}


/*! Connect still unconnected ways.
 *  @param rd Pointer to struct rdata.
 *  @param pd Pointer to list of end points of type struct pdef.
 *  @param wl Pointer to list of open ways.
 *  @param ocnt number of end points within pd. Obviously, ocnt MUST be an even number.
 *  @param no_corner Set to 1 if no corner points should be inserted, otherwise set to 0.
 *  @return 0 On success, -1 if connect_open() should be recalled with pd being resorted.
 */
static int connect_open(struct pdef *pd, struct wlist *wl, int ocnt, short no_corner)
{
   int i, j, k, l;
   int64_t *ref;
   struct corner_point *co_pt = co_pt_;

   for (i = 0; i < ocnt; i++)
   {
      // skip end points and loops
      if (pd[i].pn || !wl->ref[pd[i].wl_index].open)
      {
         log_debug("skipping i = %d", i);
         continue;
      }

      for (j = i + 1; j <= ocnt; j++)
      {
         // skip start points
         if (!pd[j % ocnt].pn || !wl->ref[pd[j % ocnt].wl_index].open)
         {
            //log_debug("skipping j = %d", j);
            continue;
         }

         if (!no_corner)
         {
            // find next corner point for i
            for (k = 0; k < 4; k++)
               if (pd[i].pc.bearing < co_pt[k].pc.bearing)
                  break;
            // find next corner point for j
            for (l = 0; l < 4; l++)
               if (pd[j % ocnt].pc.bearing < co_pt[l].pc.bearing)
                  break;
            if (l < k)
               l += 4;
            // add corner points to way
            for (; k < l; k++)
            {
               // FIXME: realloc() and memmove() should be done outside of the loop
               if ((ref = realloc(wl->ref[pd[i].wl_index].w->ref, sizeof(int64_t) * (wl->ref[pd[i].wl_index].w->ref_cnt + 1))) == NULL)
                  log_msg(LOG_ERR, "realloc() failed: %s", strerror(errno)), exit(EXIT_FAILURE);

               memmove(&ref[1], &ref[0], sizeof(int64_t) * wl->ref[pd[i].wl_index].w->ref_cnt); 
               ref[0] = co_pt[k % 4].n->obj.id;
               wl->ref[pd[i].wl_index].w->ref = ref;
               wl->ref[pd[i].wl_index].w->ref_cnt++;
               log_debug("added corner point %d (id = %ld)", k % 4, co_pt[k % 4].n->obj.id);
            }
         } //if (!no_corner)

         // if start and end point belong to same way close
         if (pd[i].wl_index == pd[j % ocnt].wl_index)
         {
            if ((ref = realloc(wl->ref[pd[i].wl_index].w->ref, sizeof(int64_t) * (wl->ref[pd[i].wl_index].w->ref_cnt + 1))) == NULL)
               log_msg(LOG_ERR, "realloc() failed: %s", strerror(errno)), exit(EXIT_FAILURE);

            ref[wl->ref[pd[i].wl_index].w->ref_cnt] = ref[0];
            wl->ref[pd[i].wl_index].w->ref = ref;
            wl->ref[pd[i].wl_index].w->ref_cnt++;
            wl->ref[pd[i].wl_index].open = 0;
            log_debug("way %ld (wl_index = %d) is now closed", wl->ref[pd[i].wl_index].w->obj.id, pd[i].wl_index);
         }
         else
         {
            log_debug("pd[%d].wl_index(%d) != pd[%d].wl_index(%d)", i, pd[i].wl_index, j % ocnt, pd[j % ocnt].wl_index);
            if ((ref = realloc(wl->ref[pd[i].wl_index].w->ref, sizeof(int64_t) * (wl->ref[pd[i].wl_index].w->ref_cnt + wl->ref[pd[j % ocnt].wl_index].w->ref_cnt))) == NULL)
               log_msg(LOG_ERR, "realloc() failed: %s", strerror(errno)), exit(EXIT_FAILURE);

            // move refs from i^th way back
            memmove(&ref[wl->ref[pd[j % ocnt].wl_index].w->ref_cnt], &ref[0], sizeof(int64_t) * wl->ref[pd[i].wl_index].w->ref_cnt); 
            // copy refs from j^th way to the beginning of i^th way
            memcpy(&ref[0], wl->ref[pd[j % ocnt].wl_index].w->ref, sizeof(int64_t) * wl->ref[pd[j % ocnt].wl_index].w->ref_cnt);
            wl->ref[pd[i].wl_index].w->ref = ref;
            wl->ref[pd[i].wl_index].w->ref_cnt += wl->ref[pd[j % ocnt].wl_index].w->ref_cnt;
            // (pseudo) close j^th way
            // FIXME: onode and its refs should be free()'d and removed from tree
            wl->ref[pd[j % ocnt].wl_index].open = 0;
            // find end-point of i^th way
            for (k = 0; k < ocnt; k++)
               if ((pd[i].wl_index == pd[k].wl_index) && pd[k].pn)
               {
                  // set point index of new end point of i^th way
                  pd[k % ocnt].pn = wl->ref[pd[i].wl_index].w->ref_cnt - 1;
                  break;
               }
            // find start-point of j^th way
            for (k = 0; k < ocnt; k++)
               if ((pd[j % ocnt].wl_index == pd[k].wl_index) && !pd[k].pn)
               {
                  // set new start-point of i to start-point of j
                  pd[i].pc = pd[k].pc;
                  break;
               }
            log_debug("way %ld (wl_index = %d) marked as closed, resorting pdef", wl->ref[pd[j % ocnt].wl_index].w->obj.id, pd[j % ocnt].wl_index);
            return -1;
         }
         break;
      }
   }
   return 0;
}


void init_cat_poly(struct rdata *rd)
{
   center_.lat = rd->mean_lat;
   center_.lon = rd->mean_lon;
   init_corner_brg(rd, &center_, co_pt_);
}


struct wlist *init_wlist(void)
{
   struct wlist *wl;

   if ((wl = malloc(sizeof(*wl) + INIT_MAX_REF * sizeof(struct poly))) == NULL)
      perror("malloc"), exit(EXIT_FAILURE);

   wl->ref_cnt = 0;
   wl->max_ref = INIT_MAX_REF;

   //init_cat_poly();
   return wl;
}


static int cat_poly_ini(smrule_t *r)
{
   double d;

   if ((r->data = calloc(1, sizeof(struct catpoly))) == NULL)
   {
      log_msg(LOG_ERR, "calloc failed in act_cat_poly_ini(): %s", strerror(errno));
      return -1;
   }

   if (get_param("ign_incomplete", &d, r->act) != NULL)
      if (d != 0)
         ((struct catpoly*) r->data)->ign_incomplete = 1;
   if (get_param("no_corner", &d, r->act) != NULL)
      if (d != 0)
         ((struct catpoly*) r->data)->no_corner = 1;

   log_msg(LOG_DEBUG, "ign_incomplete = %d, no_corner = %d", 
         ((struct catpoly*) r->data)->ign_incomplete,
         ((struct catpoly*) r->data)->no_corner);

   return 0;
}


int act_cat_poly_ini(smrule_t *r)
{
   // just to be on the safe side
   if ((r->oo->type != OSM_WAY) && (r->oo->type != OSM_REL))
   {
      log_msg(LOG_ERR, "cat_poly() is only allowed on ways and relations");
      return -1;
   }

   cat_poly_ini(r);
   if (r->oo->type == OSM_WAY)
      ((struct catpoly*) r->data)->wl = init_wlist();

   sm_threaded(r);
   return 0;
}


static int cat_poly(smrule_t *r, osm_obj_t *o)
{
   // check if it is an open polygon
   if (((osm_way_t*) o)->ref_cnt < 2)
      return 0;
   if (((osm_way_t*) o)->ref[0] == ((osm_way_t*) o)->ref[((osm_way_t*) o)->ref_cnt - 1])
      return 0;

   return gather_poly0((osm_way_t*) o, &((struct catpoly*)r->data)->wl);

}


static int cat_poly_fini(smrule_t *r)
{
   struct catpoly *cp = r->data;
   struct wlist *wl = cp->wl;
   struct pdef *pd;
   int i, ocnt;

   pd = poly_get_node_ids(wl);
   qsort(pd, wl->ref_cnt * 2, sizeof(struct pdef), (int(*)(const void *, const void *)) compare_pdef_nid);
   poly_find_adj2(wl, pd);
   ocnt = loop_detect(r->oo, wl);
   free(pd);

   log_debug("trimming ways");
   trim_ways(wl, ocnt);

   if (!cp->ign_incomplete)
   {
      log_debug("connecting incomplete polygons loops");
      if ((pd = calloc(ocnt << 1, sizeof(struct pdef))) == NULL)
         log_msg(LOG_EMERG, "cat_poly()/calloc(): %s", strerror(errno)), exit(EXIT_FAILURE);

      poly_get_brg(pd, wl, ocnt);

      do
      {
         log_msg(LOG_DEBUG, "sorting pdef, ocnt = %d", ocnt << 1);
         qsort(pd, ocnt << 1, sizeof(struct pdef), (int(*)(const void *, const void *)) compare_pdef);

         for (i = 0; i < ocnt << 1; i++)
            if (wl->ref[pd[i].wl_index].open)
               log_debug("%d: wl_index = %d, pn = %d, wid = %ld, brg = %f", i, pd[i].wl_index, pd[i].pn, wl->ref[pd[i].wl_index].w->obj.id, pd[i].pc.bearing);
      }
      while (connect_open(pd, wl, ocnt << 1, cp->no_corner));

      free(pd);
   }

   free(wl);
   return 0;
}


static int cat_relways(smrule_t *r, osm_obj_t *o)
{
   osm_way_t *w;
   smrule_t tr;
   int i;

   log_msg(LOG_DEBUG, "cat_relways(id = %ld)", (long) o->id);
   ((struct catpoly*) r->data)->wl = init_wlist();
   for (i = 0; i < ((osm_rel_t*) o)->mem_cnt; i++)
   {
      if (((osm_rel_t*) o)->mem[i].type != OSM_WAY)
         continue;
      if ((w = get_object(OSM_WAY, ((osm_rel_t*) o)->mem[i].id)) == NULL)
      {
         log_msg(LOG_ERR, "way %ld of relation %ld does not exist", (long) ((osm_rel_t*) o)->mem[i].id, (long) o->id);
         continue;
      }
      cat_poly(r, (osm_obj_t*) w);
   }
   // create temporary rule for copying tags of the relation object to new ways.
   memcpy(&tr, r, sizeof(tr));
   tr.oo = o;
   cat_poly_fini(&tr);
   return 0;
}


int act_cat_poly_main(smrule_t *r, osm_obj_t *o)
{
   switch (r->oo->type)
   {
      case OSM_WAY:
         return cat_poly(r, o);

      case OSM_REL:
         return cat_relways(r, o);
   }
   return -1;
}



int act_cat_poly_fini(smrule_t *r)
{
   if (r->oo->type == OSM_WAY)
      cat_poly_fini(r);
   free(r->data);
   r->data = NULL;
   return 0;
}


int compare_poly_area(const struct poly *p1, const struct poly *p2)
{
   if (p1->area > p2->area)
      return -1;
   if (p1->area < p2->area)
      return 1;
   return 0;
}


static void add_blind_node(const struct coord *c)
{
   osm_node_t *n = malloc_node(0);

   n->obj.id = unique_node_id();
   n->lat = c->lat;
   n->lon = c->lon;
   put_object((osm_obj_t*) n);
}


#define SQR(a) ((a) * (a))
#define MAX_DEVIATION 50
#define MAX_ITERATION 3
#define MAX_CFAC 2.0


#if 0
osm_node_t *split_line(osm_node_t **s)
{
   osm_node_t *n = malloc_node(0);
   n->obj.id = unique_node_id();
   n->lat = (s[0]->lat + s[1]->lat) / 2;
   n->lon = (s[0]->lon + s[1]->lon) / 2;
   return n;
}

// FIXME: this does not work yet
int norm_adj_line_len(osm_way_t *w)
{
   osm_node_t *n[3];
   osm_node_t *m;
   int64_t *ref;
   double l[2];
   int i, j;

   log_msg(LOG_WARN, "norm_adj_line_len broken");
   return 0;

   for (i = 0; i < w->ref_cnt - 2; i++)
   {
      for (j = 0; j < 3; j++)
         if ((n[j] = get_object(OSM_NODE, w->ref[i + j])) == NULL)
            return -1;

      l[0] = HYPOT(n[0]->lon - n[1]->lon, n[0]->lat - n[1]->lat);
      l[1] = HYPOT(n[1]->lon - n[2]->lon, n[1]->lat - n[2]->lat);

      if (l[0] > MAX_CFAC * l[1])
         j = 0;
      else if (l[1] > MAX_CFAC * l[0])
         j = 1;
      else
         continue;
      
      m = split_line(&n[j]);
      if ((ref = realloc(w->ref, sizeof(int64_t) * (w->ref_cnt + 1))) == NULL)
      {
         log_msg(LOG_ERR, "could not enlarge reflist of way %ld: %s", (long) w->obj.id, strerror(errno));
         return -1;
      }
      memmove(&ref[i + j + 1], &ref[i + j], sizeof(int64_t) * (w->ref_cnt - i - j));
      ref[i + j] = m->obj.id;
      put_object((osm_obj_t*) m);
      w->ref = ref;
      w->ref_cnt++;

      //if (j) i--;
   }
   return 0;
}
#endif


/*! 
 *  @param sgn Sign; this may be 1 or -1.
 */
static void node_to_circle(osm_node_t *n, const struct coord *c, double r, double k, int sgn)
{
   double e;

   e = sgn * sqrt(SQR(r) / (1 + SQR(k)));

   if (n->obj.ver)
   {
      n->lon = ((c->lon - e) + n->lon) / 2;
      n->lat = ((c->lat - k * e) + n->lat) / 2;
   }
   else
   {
      n->lon = c->lon - e;
      n->lat = c->lat - k * e;
      n->obj.ver++;
   }
}


static void avg_point(osm_node_t *n, const struct coord *p)
{
   if (n->obj.ver)
   {
      n->lat = (p->lat + n->lat) / 2;
      n->lon = (p->lon + n->lon) / 2;
   }
   else
   {
      n->lat = p->lat;
      n->lon = p->lon;
      n->obj.ver++;
   }
}


/*! Calculate two nodes n which are on the same circle as three nodes s.
 *  @param n Array of two node pointers which will receive the result.
 *  @param s Array of three nodes which are the source of calculation.
 */
static void circle_calc(osm_node_t **n, osm_node_t **s, double deviation)
{
   int i;
   double k[2], d[2], r, t;
   struct coord c, p[2];

   for (i = 0; i < 2; i++)
   {
      p[i].lat = (s[i]->lat + s[i + 1]->lat) / 2;
      p[i].lon = (s[i]->lon + s[i + 1]->lon) / 2;

      // prevent DIV0
      if (s[i + 1]->lat == s[i]->lat)
         k[i] = 0.0;
      else
         k[i] = -(s[i + 1]->lon - s[i]->lon) / (s[i + 1]->lat - s[i]->lat);

      d[i] = p[i].lat - k[i] * p[i].lon;
   }

   // center point
   c.lon = (d[1] - d[0]) / (k[0] - k[1]);
   if (k[0] != 0.0)
      c.lat = k[0] * c.lon + d[0];
   else
      c.lat = k[1] * c.lon + d[1];
    // radius
   r = hypot(s[0]->lon - c.lon, s[0]->lat - c.lat);
   
   for (i = 0; i < 2; i++)
   {
      if (isnormal(r))
      {
         t = hypot(p[i].lon - c.lon, p[i].lat - c.lat);
         node_to_circle(n[i], &c, r - t > deviation ? t + deviation : r, k[i], c.lon < p[i].lon ? -1 : 1);
      }
      else
      {
         avg_point(n[i], &p[i]);
      }
   }
}


static int refine_poly0(osm_way_t *w, double deviation)
{
   osm_node_t *n[w->ref_cnt - 1], *s[w->ref_cnt];
   int64_t *ref;
   int i;

   if (w->ref_cnt < 3)
   {
      log_msg(LOG_DEBUG, "refine_poly needs way with at least 3 nodes");
      return 1;
   }

   // get existing nodes
   for (i = 0; i < w->ref_cnt; i++)
      if ((s[i] = get_object(OSM_NODE, w->ref[i])) == NULL)
      {
         log_msg(LOG_EMERG, "get_object() returned NULL pointer");
         return 1;
      }

   // get new nodes
   for (i = 0; i < w->ref_cnt - 1; i++)
   {
      n[i] = malloc_node(0);
      n[i]->obj.vis = 1;
   }

   for (i = 0; i < w->ref_cnt - 2; i++)
      circle_calc(&n[i], &s[i], deviation);

   if ((ref = malloc(sizeof(int64_t) * (w->ref_cnt * 2 - 1))) == NULL)
   {
      log_msg(LOG_ERR, "malloc for new nodelist failed: %s", strerror(errno));
      return 1;
   }

   for (i = 0; i < w->ref_cnt - 1; i++)
   {
      ref[i * 2] = w->ref[i];
      ref[i * 2 + 1] = n[i]->obj.id = unique_node_id();
      put_object((osm_obj_t*) n[i]);
   }
   ref[i * 2] = w->ref[i];

   free(w->ref);
   w->ref = ref;
   w->ref_cnt = w->ref_cnt * 2 - 1;

   return 0;
}


int act_refine_poly_ini(smrule_t *r)
{
   struct refine *rf;
   double it;

   if ((rf = malloc(sizeof(*rf))) == NULL)
   {
      log_msg(LOG_ERR, "cannot malloc: %s", strerror(errno));
      return -1;
   }

   if (get_param("iteration", &it, r->act) == NULL)
      it = MAX_ITERATION;

   rf->iteration = round(it);

   if (get_param("deviation", &rf->deviation, r->act) == NULL)
      rf->deviation = MAX_DEVIATION;

   rf->deviation /= (1852.0 * 60.0);
   log_msg(LOG_INFO, "refine_poly using iteration = %d, deviation = %.1f", rf->iteration, rf->deviation * 1852.0 * 60.0);

   sm_threaded(r);

   r->data = rf;

   return 0;
}


int act_refine_poly_main(smrule_t *r, osm_way_t *w)
{
   struct refine *rf = r->data;
   int i;

   if (w->obj.type != OSM_WAY)
      return 1;

   for (i = 0; i < rf->iteration; i++)
      if (refine_poly0(w, rf->deviation))
         return 1;

   return 0;
}


int act_refine_poly_fini(smrule_t *r)
{
   if (r->data != NULL)
   {
      free(r->data);
      r->data = NULL;
   }
   return 0;
}

