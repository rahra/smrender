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
 * 8) create new pdef list with number of still open ways.
 * 9) add all end points to pdef list and calculate bearing from center point (poly_get_brg() returns number of open ways)
 * 10) sort points by bearing
 * 11) connect_open()
 * 11.1)
 *
 */
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

#include "smrender.h"
#include "smlog.h"
#include "smath.h"
#include "bxtree.h"


// initial number of ref array
#define INIT_MAX_REF 20
#define MAX_OPEN_POLY 32


struct corner_point
{
   struct pcoord pc;
   osm_node_t *n;
};


struct poly
{
   struct poly *next, *prev;
   osm_way_t *w;
   short del;           // 1 if element should be removed from list
   short open;          // 1 if element is connected but still open way
};

struct wlist
{
   int ref_cnt, max_ref;
   struct poly ref[];
};

struct pdef
{
   int wl_index;        // index of way within wlist
   int pn;              // index number of destined point within way
   union
   {
      struct pcoord pc; // bearing to pointer
      int64_t nid;      // node id
   };
};


void node_brg(struct pcoord*, struct coord*, int64_t);
void init_corner_brg(const struct rdata*, const struct coord*, struct corner_point*);


static struct corner_point co_pt_[4];
static struct coord center_;
static struct wlist *wl_ = NULL;
static struct orule *rl_ = NULL;


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


/*! This finds open polygons with tag natural=coastline and adds
 *  the node references to the wlist structure.
 */
int gather_poly0(osm_way_t *w, struct wlist **wl)
{
   // check if it is an open polygon
   if (w->ref_cnt < 2)
      return 0;
   if (w->ref[0] == w->ref[w->ref_cnt - 1])
      return 0;

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
struct pdef *poly_get_node_ids(const struct wlist *wl)
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


int poly_get_brg(struct pdef *pd, struct wlist *wl, int ocnt)
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


int poly_find_adj2(struct wlist *wl, struct pdef *pd)
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
int count_poly_refs(struct poly *pl, int *cnt)
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


osm_way_t *create_new_coastline(int ref_cnt)
{
   osm_way_t *w;

   if (rl_ == NULL)
   {
      w = malloc_way(1, ref_cnt);
   }
   else
   {
      w = malloc_way(rl_->oo->tag_cnt + 1, ref_cnt);
      memcpy(&w->obj.otag[1], &rl_->oo->otag[0], sizeof(struct otag) * rl_->oo->tag_cnt);
   }

   w->obj.id = unique_way_id();
   w->obj.ver = 1;
   w->obj.tim = time(NULL);
   set_const_tag(&w->obj.otag[0], "generator", "smrender");
 
   return w;
}


int join_open_poly(struct poly *pl, osm_way_t *w)
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


int loop_detect(struct wlist *wl)
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
      w = create_new_coastline(cnt);
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


int compare_pdef_nid(const struct pdef *p1, const struct pdef *p2)
{
   if (p1->nid < p2->nid) return -1;
   if (p1->nid > p2->nid) return 1;

   if (p1->pn < p2->pn) return -1;
   if (p1->pn > p2->pn) return 1;

   return 0;
}


int compare_pdef(const struct pdef *p1, const struct pdef *p2)
{
   if (p1->pc.bearing < p2->pc.bearing) return -1;
   if (p1->pc.bearing > p2->pc.bearing) return 1;
   return 0;
}


void init_corner_brg(const struct rdata *rd, const struct coord *src, struct corner_point *co_pt)
{
   struct coord corner_coord[4] = {{rd->y1c, rd->x2c}, {rd->y2c, rd->x2c}, {rd->y2c, rd->x1c}, {rd->y1c, rd->x1c}};
   int i;

   for (i = 0; i < 4; i++)
   {
      co_pt[i].pc = coord_diff(src, &corner_coord[i]);
      co_pt[i].n = malloc_node(2);
      co_pt[i].n->obj.id = unique_node_id();
      co_pt[i].n->obj.ver = 1;
      co_pt[i].n->obj.tim = time(NULL);
      co_pt[i].n->lat = corner_coord[i].lat;
      co_pt[i].n->lon = corner_coord[i].lon;
      set_const_tag(&co_pt[i].n->obj.otag[0], "grid", "pagecorner");
      set_const_tag(&co_pt[i].n->obj.otag[1], "generator", "smrender");
      put_object((osm_obj_t*) co_pt[i].n);
      log_msg(LOG_DEBUG, "corner_point[%d].bearing = %f", i, co_pt[i].pc.bearing);
   }
}


void node_brg(struct pcoord *pc, struct coord *src, int64_t nid)
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
 *  @return 0 On success, -1 if connect_open() should be recalled with pd being resorted.
 */
int connect_open(struct pdef *pd, struct wlist *wl, int ocnt)
{
   int i, j, k, l;
   int64_t *ref;
   struct corner_point *co_pt = co_pt_;

   for (i = 0; i < ocnt; i++)
   {
      // skip end points and loops
      if (pd[i].pn || !wl->ref[pd[i].wl_index].open)
      {
         //log_debug("skipping i = %d", i);
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

         //if (pd[i].wl_index == pd[j % ocnt].wl_index)
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
               log_debug("added corner point %d", k % 4);
            }

            // if start and end point belong to same way close
            if (pd[i].wl_index == pd[j % ocnt].wl_index)
            {
               if ((ref = realloc(wl->ref[pd[i].wl_index].w->ref, sizeof(int64_t) * (wl->ref[pd[i].wl_index].w->ref_cnt + 1))) == NULL)
                  log_msg(LOG_ERR, "realloc() failed: %s", strerror(errno)), exit(EXIT_FAILURE);

               //memmove(&ref[1], &ref[0], sizeof(int64_t) * wl->ref[pd[i].wl_index].w->ref_cnt); 
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
         } //if (pd[i].wl_index == pd[j % ocnt].wl_index)
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


int cat_poly_ini(const orule_t *rl)
{
   struct wlist *wl;

   if ((wl = malloc(sizeof(*wl) + INIT_MAX_REF * sizeof(struct poly))) == NULL)
      perror("malloc"), exit(EXIT_FAILURE);

   wl->ref_cnt = 0;
   wl->max_ref = INIT_MAX_REF;
   wl_ = wl;
   rl_ = (orule_t*) rl;

   return 0;
}


int cat_poly(osm_obj_t *o)
{
   return gather_poly0((osm_way_t*) o, &wl_);
}


void cat_poly_fini(void)
{
   int i, ocnt;
   struct wlist *wl = wl_;
   struct pdef *pd;

   pd = poly_get_node_ids(wl);
   qsort(pd, wl->ref_cnt * 2, sizeof(struct pdef), (int(*)(const void *, const void *)) compare_pdef_nid);
   poly_find_adj2(wl, pd);
   ocnt = loop_detect(wl);
   free(pd);

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
   while (connect_open(pd, wl, ocnt << 1));

   free(pd);
   free(wl);
}

