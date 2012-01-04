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
   struct onode *nd;
};


struct poly
{
   struct poly *next, *prev;
   struct onode *w;
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


/*! This finds open polygons with tag natural=coastline and adds
 *  the node references to the wlist structure.
 */
int gather_poly(struct onode *nd, struct rdata *rd, struct wlist **wl)
{
   // check if it is an open polygon
   if (nd->ref_cnt < 2)
      return 0;
   if (nd->ref[0] == nd->ref[nd->ref_cnt - 1])
      return 0;

   // check if it is a coastline
   if (match_attr(nd, "natural", "coastline") == -1)
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
   (*wl)->ref[(*wl)->ref_cnt].w = nd;
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


struct pdef *poly_get_brg(struct rdata *rd, struct wlist *wl, int ocnt)
{
   struct pdef *pd;
   struct coord center = {rd->mean_lat, (rd->x1c + rd->x2c) / 2};
   int i, j;

   if ((pd = calloc(ocnt * 2, sizeof(struct pdef))) == NULL)
   {
      log_msg(LOG_EMERG, "poly_get_brg(): %s", strerror(errno));
      exit(EXIT_FAILURE);
   }

   for (i = 0, j = 0; (i < wl->ref_cnt) && ( j < ocnt); i++)
   {
      if (!wl->ref[i].open)
         continue;

      node_brg(&pd[j].pc, &center, wl->ref[i].w->ref[0]);
      pd[j].wl_index = i;
      pd[j].pn = 0;
      node_brg(&pd[j + ocnt].pc, &center, wl->ref[i].w->ref[wl->ref[i].w->ref_cnt - 1]);
      pd[j + ocnt].wl_index = i;
      pd[j + ocnt].pn = wl->ref[i].w->ref_cnt - 1;
      j++;
   }

   return pd;
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


struct onode *create_new_coastline(int ref_cnt)
{
   struct onode *nd;

   nd = malloc_object(2, ref_cnt);
   nd->nd.type = OSM_WAY;
   nd->nd.id = unique_way_id();
   nd->nd.ver = 1;
   nd->nd.tim = time(NULL);
   set_const_tag(&nd->otag[0], "natural", "coastline");
   set_const_tag(&nd->otag[1], "generator", "smrender");

   return nd;
}


int join_open_poly(struct poly *pl, struct onode *nd)
{
   int pos, wcnt = 0;
   struct poly *list;

   for (list = pl, pos = 0; list != NULL; list = list->next, wcnt++)
   {
      // copy all node refs...
      memcpy(&nd->ref[pos], list->w->ref, list->w->ref_cnt * sizeof(int64_t));
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
   struct onode *nd;
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

      // check if way is intermediate way
      if (!ret && (wl->ref[i].prev != NULL))
      {
         //log_debug("way on wl_index %d is intermediate way", i);
         continue;
      }

      log_debug("waylist: wl_index %d (start = %p, cnt = %d, loop = %d)", i, &wl->ref[i], cnt, ret);
      nd = create_new_coastline(cnt);
      cnt = join_open_poly(&wl->ref[i], nd);
      put_object(nd);
      log_debug("%d ways joined", cnt);

      // if it is not a loop, than it is a starting open way
      if (!ret)
      {
         wl->ref[i].open = 1;
         wl->ref[i].w = nd;
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
   struct coord corner_coord[4] = {{rd->y1c, rd->x2c}, {rd->y2c, rd->x2c}, {rd->y1c, rd->x2c}, {rd->y1c, rd->x1c}};
   int i;

   for (i = 0; i < 4; i++)
   {
      co_pt[i].pc = coord_diff(src, &corner_coord[i]);
      co_pt[i].nd = malloc_object(2, 0);
      co_pt[i].nd->nd.id = unique_node_id();
      co_pt[i].nd->nd.type = OSM_NODE;
      co_pt[i].nd->nd.ver = 1;
      co_pt[i].nd->nd.tim = time(NULL);
      co_pt[i].nd->nd.lat = corner_coord[i].lat;
      co_pt[i].nd->nd.lon = corner_coord[i].lon;
      set_const_tag(&co_pt[i].nd->otag[0], "grid", "pagecorner");
      set_const_tag(&co_pt[i].nd->otag[1], "generator", "smrender");
      put_object(co_pt[i].nd);
   }
}


void node_brg(struct pcoord *pc, struct coord *src, int64_t nid)
{
   struct onode *nd;
   struct coord dst;

   if ((nd = get_object(OSM_NODE, nid)) == NULL)
      return;
   dst.lat = nd->nd.lat;
   dst.lon = nd->nd.lon;
   *pc = coord_diff(src, &dst);
}


void connect_open(struct rdata *rd, struct pdef *pd, struct wlist *wl, int ocnt)
{
   int i, j, k, l;
   int64_t *ref;
   struct corner_point co_pt[4];
   struct coord center = {rd->mean_lat, (rd->x1c + rd->x2c) / 2};

   init_corner_brg(rd, &center, co_pt);

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

         if (pd[i].wl_index == pd[j % ocnt].wl_index)
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
               if ((ref = realloc(wl->ref[pd[i].wl_index].w->ref, sizeof(int64_t) * (wl->ref[pd[i].wl_index].w->ref_cnt + 1))) == NULL)
                  log_msg(LOG_ERR, "realloc() failed: %s", strerror(errno)), exit(EXIT_FAILURE);

               ref[wl->ref[pd[i].wl_index].w->ref_cnt] = co_pt[k % 4].nd->nd.id;
               wl->ref[pd[i].wl_index].w->ref = ref;
               wl->ref[pd[i].wl_index].w->ref_cnt++;
               log_debug("added corner point %d", k % 4);
            }

            if ((ref = realloc(wl->ref[pd[i].wl_index].w->ref, sizeof(int64_t) * (wl->ref[pd[i].wl_index].w->ref_cnt + 1))) == NULL)
               log_msg(LOG_ERR, "realloc() failed: %s", strerror(errno)), exit(EXIT_FAILURE);

            ref[wl->ref[pd[i].wl_index].w->ref_cnt] = ref[0];
            wl->ref[pd[i].wl_index].w->ref = ref;
            wl->ref[pd[i].wl_index].w->ref_cnt++;
            wl->ref[pd[i].wl_index].open = 0;
            log_debug("way %ld (wl_index = %d) is now closed", wl->ref[pd[i].wl_index].w->nd.id, pd[i].wl_index);
            break;
         }
      }
   }
}


int cat_poly(struct rdata *rd)
{
   int i, ocnt;
   struct wlist *wl;
   struct pdef *pd;

   if ((wl = malloc(sizeof(*wl) + INIT_MAX_REF * sizeof(struct poly))) == NULL)
      perror("malloc"), exit(EXIT_FAILURE);

   wl->ref_cnt = 0;
   wl->max_ref = INIT_MAX_REF;

   log_debug("collecting open coastline polygons");
   traverse(rd->obj, 0, IDX_WAY, (tree_func_t) gather_poly, rd, &wl);

   pd = poly_get_node_ids(wl);
   qsort(pd, wl->ref_cnt * 2, sizeof(struct pdef), (int(*)(const void *, const void *)) compare_pdef_nid);
   poly_find_adj2(wl, pd);
   ocnt = loop_detect(wl);
   free(pd);

   pd = poly_get_brg(rd, wl, ocnt);
   ocnt *= 2;
   qsort(pd, ocnt, sizeof(struct pdef), (int(*)(const void *, const void *)) compare_pdef);

   for (i = 0; i < ocnt; i++)
      log_debug("%d: wl_index = %d, pn = %d, brg = %f", i, pd[i].wl_index, pd[i].pn, pd[i].pc.bearing);

   connect_open(rd, pd, wl, ocnt);
   free(pd);

   free(wl);
   return 0;
}
