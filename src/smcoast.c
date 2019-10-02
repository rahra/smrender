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

/*! \file smcoast.c
 * This file contains the code which is used to close open polygons. Open
 * polygons obviously cannot be filled, thus the must be closed before. Open
 * polygons occure at the edges of the bounding box which is used to select
 * data out of the OSM database. This is one of the most difficult parts at
 * all.
 *
 * The basic stages of this polygon-closing algorithm are as follows:
 * 1) Gather all open polygons.
 * 2) Create pdef list which contains all end points of open polygons (pdef_cnt
 *    = open_poly_cnt * 2).
 * 3) Retrieve node ids from those points (each start and end point).
 * 4) Sort pdef list by node id.
 * 5) Set prev/next pointers for each point in pdef list at their neighboring
 *    points, i.e. if one start point has the same node id as the neighbor end
 *    point (poly_find_adj2()). Then it is the same point (node) which belongs
 *    to two differrent ways, thus those ways have to be connected.
 * 6) Loop over all ways in the list (loop_detect() returns number of open ways).
 * 6.1) Count nodes of "connected" (next/prev-pointered) ways and detect if
 *    there is a loop, i.e. a circular list of ways (count_poly_ref() = 1 if
 *    loop, 0 if unclosed).
 * 6.2) Create new way with the according number of nodes (node count = sum of
 *    all connected ways).
 * 6.3) Copy node ids of all connected ways to newly created way
 *    (join_open_poly()). Mark ways which have been processed as deleteable
 *    (from list). Mark those which are still open (i.e. not already looped) as
 *    open.
 * 6.4) Put new way to way pool.
 * 7) Free pdef list.
 * 8) Trim open ways to edges of page.
 * 9) Create new pdef list with number of still open ways.
 * 10) Add all end points to pdef list and calculate their bearing from the
 *    center point of the rendering area (poly_get_brg() returns number of open
 *    ways).
 * 11) Sort points by bearing.
 * 12) Iterate over all points in the order of the list (connect_open()): find
 *    first start node and next end node.
 * 12.1) Test if both are on the same edge of the rendering area, otherwise
 *    append additional corner point(s) of the rendering rectangle which are
 *    between the start and end node in clockwise order behind the end node.
 * 12.2) If start and end node belong to the same way, close polygon, i.e.
 *    append start node at the end.
 * 12.3)
 *
 *
 *
 *  @author Bernhard R. Fischer
 *
 */
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <math.h>
#include <inttypes.h>

#include "smrender_dev.h"
#include "smcoast.h"

struct refine
{
   double deviation;
   int iteration;
};


static int node_brg(struct pcoord*, const struct coord*, int64_t);


static struct corner_point co_pt_[4];
static struct coord center_;
static osm_way_t *page_way_;


const osm_way_t *page_way(void)
{
   return page_way_;
}


/*! Check if way is a closed polygon and is an area (i.e. it has at least 4 points).
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


/*! This function adds a way w to the way list wl thereby reallocating the
 * waylist.
 * @param w Pointer to the way.
 * @param wl Pointer to a way list pointer. The way list must be freed again
 * later.
 * @return Always returns 0. FIXME: This function exits the program if
 * realloc() fails.
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


/*! This function calculates the bearing and distance from the center point to
 * the first and last node of all open ways in the way list wl. The result
 * (bearing, distance) is stored into a list of struct pdefs.
 * @param pd Pointer to pdef list. This list must be pre-allocated and must
 * contain at least 2 * ocnt elements which should be greater or equal to
 * wl->ref_cnt * 2.
 * @param wl Pointer to the way list.
 * @param ocnt Number of open ways in wl. (FIXME: Why that? Isn't ref_cnt enough?)
 * @return Returns the number of open ways found in wl which is always less or
 * equal than ocnt.
 */
static int poly_get_brg(struct pdef *pd, const struct wlist *wl, int ocnt)
{
   int i, j;

   for (i = 0, j = 0; (i < wl->ref_cnt) && (j < ocnt); i++)
   {
      if (!wl->ref[i].open)
         continue;

      node_brg(&pd[j].pc, &center_, wl->ref[i].nw->ref[0]);
      pd[j].wl_index = i;
      pd[j].pn = 0;
      node_brg(&pd[j + ocnt].pc, &center_, wl->ref[i].nw->ref[wl->ref[i].nw->ref_cnt - 1]);
      pd[j + ocnt].wl_index = i;
      pd[j + ocnt].pn = wl->ref[i].nw->ref_cnt - 1;
      j++;
   }

   return j;
}


/*! Returns the octant of a position outside the rendering window. This is one
 * of N, NE, E, SE, S, SW, W, NW.
 * @param crd Pointer to position.
 * @return Returns Octant as a combination of the logical or'ed flags  POS_N,
 * POS_S, POS_E, POS_W. If the position is inside the rendering window 0 is
 * returned.
 */
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
      n = malloc_node(2);
      osm_node_default(n);
      set_const_tag(&n->obj.otag[1], "smrender:cat_poly", "edge_point");
      n->lat = crd.lat;
      n->lon = crd.lon;
      put_object((osm_obj_t*) n);
//        w->ref[0] = n->obj.id;
//         log_debug("added new edge point %ld at lat = %f, lon = %f", (long) n->obj.id, n->lat, n->lon);
      return n->obj.id;
   }
}


/*! This function returns the true index within the reference list of a way
 * according to the given index idx and a direction rev.
 * @param w Pointer to way.
 * @param idx Number of reference. This value must be in the range 0 <= idx <
 * w->ref_cnt. This function does no boundary check!
 * @param rev Direction 0 (false) means ascending order (starting at the
 * beginning), != 0 (true) means descending, i.e. starting at the end of the
 * reference list.
 * @return Returns the true index.
 */
static int windex(const osm_way_t *w, int idx, int rev)
{
   return rev ? w->ref_cnt - idx - 1 : idx;
}


/*! This function trims all node refs from the beginning of a way which are
 * outside the rendering window. This means shortening the way by removing
 * those which are outside up to the first node which is inside.
 * @param w Pointer to way which should be trimmed.
 * @return It returns the index of the of the original reference list of the
 * way of the first node which was inside. This node is at index 1 after
 * modification and at index 0 a newly generated edge point was inserted.
 * If 0 is returned the way was not modified, i.e. even the first point is
 * inside. -1 is returned in case of error which occures if all nodes are
 * outside.
 */
static int trim_way(osm_way_t *w, int rev)
{
   struct coord crd;
   osm_node_t *n;
   int i, p[2] = {0, 0};
   int64_t nid;

   // loop over all node refs of way
   for (i = 0; i < w->ref_cnt; i++)
   {
      // get corresponding node
      if ((n = get_object(OSM_NODE, w->ref[windex(w, i, rev)])) == NULL)
      {
         log_msg(LOG_ERR, "node %"PRId64" in way %"PRId64" does not exist", w->ref[windex(w, i, rev)], w->obj.id);
         return -1;
      }

      // calculate octant and break loop if node is inside the page border
      crd.lat = n->lat;
      crd.lon = n->lon;
      p[0] = p[1];
      if (!(p[1] = octant(&crd)))
         break;
   }

   // check if at least one node is on the page
   if (i >= w->ref_cnt)
      return -1;

   // check that the corresponding node is not the 1st one
   if (p[0])
   {
      log_debug("trimming way %"PRId64", %d - %d out of page, octant = 0x%02x",
            w->obj.id, windex(w, 0, rev), windex(w, i - 1, rev), p[0]);

      // create new edge point
      if (!(nid = edge_point(crd, p[0], w->ref[windex(w, i - 1, rev)])))
         return -1;

      log_debug("added new edge point %"PRId64" at ref# %d", nid, windex(w, i - 1, rev));

      // ascending order has to be handled slightly different
      if (!rev)
      {
         // set first ref to new edge point
         w->ref[windex(w, 0, rev)] = nid;
         // move all other refs starting with the first ref which is inside (at
         // position i) directly behind ref 0
         memmove(&w->ref[1], &w->ref[i], sizeof(int64_t) * (w->ref_cnt - i));
      }
      // descending order
      else
         w->ref[windex(w, i - 1, rev)] = nid;

      // decrease ref count accordingly
      w->ref_cnt -= i - 1;
   }

   return i;
}


/*! This function trims all open ways in the way list to the page border. Ways
 * which are completely outside of the page are marked as closed to avoid
 * further processing.
 * @param wl Pointer to the way list.
 * @return Returns the number of open ways which are still on the page (or
 * partially on the page).
 */
static int trim_ways(struct wlist *wl)
{
   int i, j, e;

   for (i = 0, j = 0; i < wl->ref_cnt; i++)
   {
      if (!wl->ref[i].open)
         continue;

      if ((e = trim_way(wl->ref[i].nw, 0)) == -1)
      {
         log_debug("marking %"PRId64" at wl_index = %d out-of-page", wl->ref[i].nw->obj.id, i);
         // pretend way to be closed to avoid further processing
         wl->ref[i].open = 0;
         continue;
      }
      else if (e > 0)
         log_debug("wl_index = %d", i);

      if ((e = trim_way(wl->ref[i].nw, 1)) == -1)
      {
         log_msg(LOG_EMERG, "fatal error, this should not happen!");
         continue;
      }
      else if (e > 0)
         log_debug("wl_index = %d", i);
      j++;
   }

   log_debug("new open_count = %d", j);
   return j;
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


/*! This function detects if a way is already closed (loop, i.e. cyclic list of
 * way segments) and determines the total number of nodes.
 * @param pl Pointer to beginning of ways.
 * @param cnt Pointer to integer which will reveive the number of nodes.
 * @return The function returns 1 of it is a loop, 0 if it is unclosed, and -1
 * on error.
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
      if (list->next == pl)
         break;
      // safety check to detect incorrect pointers in double-linked list
      if (list->next != NULL && list->next->prev != NULL && list != list->next->prev)
      {
         log_msg(LOG_WARN, "loop error! this may indicated broken input data (or an incorrect ruleset)");
         return -1;
      }
   }

   (*cnt)++;
   return list != NULL;
}


/*! This function copies tags from the the source object src to the destination
 * object dst if the keys of those tags are listed in the reference object cp
 * and exist in src.
 * @param cp Pointer to reference object for a list of tag keys which should be
 * honored by this function.
 * @param src Pointer to source object.
 * @param dst Pointer to destination object.
 * @return Returns the number of tags that were copied from src to dst. -1 is
 * returned in case of error, i.e. memory reallocation has failed.
 */
static int collect_tags(const osm_obj_t *cp, const osm_obj_t *src, osm_obj_t *dst)
{
   struct otag ot, *tags;
   struct stag st;
   int i, n, cnt;

   if (src->type == dst->type && src->id == dst->id)
   {
      log_msg(LOG_ERR, "ignoring src == dst, this may indicate a software bug!");
      return 0;
   }

   memset(&st, 0, sizeof(st));
   memset(&ot, 0, sizeof(ot));

   log_debug("collect_tags(src = %"PRId64")", src->id);
   // loop over all tags in cp
   for (i = 0, cnt = 0; i < cp->tag_cnt; i++)
   {
      // check if tag of cp exists in src and continue if not
      ot.k = cp->otag[i].k;
      if ((n = bs_match_attr(src, &ot, &st)) < 0)
         continue;

      // check if tag also exists in dst and continue in that case
      if (bs_match_attr(dst, &ot, &st) >= 0)
      {
         if (bs_match_attr(dst, &src->otag[n], &st) < 0)
            log_msg(LOG_WARN, "value missmatch of key %.*s between ways %"PRId64" and %"PRId64,
                  ot.k.len, ot.k.buf, dst->id, src->id);
         continue;
      }

      // reallocate tag list in dst
      if ((tags = realloc(dst->otag, sizeof(*dst->otag) * (dst->tag_cnt + 1))) == NULL)
      {
         log_msg(LOG_ERR, "realloc() failed in collect_tags(): %s", strerror(errno));
         return -1;
      }
      dst->otag = tags;

      // copy tag
      memcpy(dst->otag + dst->tag_cnt, src->otag + n, sizeof(*dst->otag));
      dst->tag_cnt++;
      cnt++;
   }
   return cnt;
}


/*! This function copies all node refs of the way chain found in pl to the way
 * w. The original ways in the list are marked for deletion.
 * @param pl Starting way of a waylist.
 * @param w Way to which the node refs are joined.
 * @return The function returns the total number finally joined refs.
 */
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
         log_debug("%p is already part from other way!", list);

      list->del = 1;

      if (list->next == pl)
      {
         wcnt++;
         break;
      }
   }

   return wcnt;
}


/*! This function merges all tags from the ways found in the cyclic poly list
 * pl whose tags are also found in the reference object o to the way w.
 * @param pl Pointer to the poly list.
 * @param o Pointer to the reference object.
 * @param w Pointer to the way to which the tags are merged.
 * @return Return 0 on success or -1 in case of error, i.e. collect_tags() fails.
 */
static int join_tags(const struct poly *pl, const osm_obj_t *o, osm_way_t *w)
{
   const struct poly *list;

   log_debug("joining tags from way %"PRId64" to %"PRId64, pl->w->obj.id, w->obj.id);
   for (list = pl; list != NULL; list = list->next)
   {
      // copy all non-existing tags to new way
      if (collect_tags(o, (osm_obj_t*) list->w, (osm_obj_t*) w) == -1)
         return -1;

      if (list->next == pl)
         break;
   }

   return 0;
}


static void poly_join_tags(const struct wlist *wl, const smrule_t *r)
{
   for (int i = 0; i < wl->ref_cnt; i++)
      if (wl->ref[i].nw != NULL)
      {
         log_debug("joining tags to way %"PRId64, wl->ref[i].nw->obj.id);
         (void) join_tags(&wl->ref[i], &((struct catpoly*) r->data)->obj, wl->ref[i].nw);
         if (r->oo->type == OSM_REL)
         {
            log_debug("joining relation tags");
            collect_tags(r->oo, r->oo, &wl->ref[i].nw->obj);
         }
      }
}


static int loop_detect(struct wlist *wl)
{
   osm_way_t *w;
   int i, cnt, ret, ocnt = 0;

   for (i = 0; i < wl->ref_cnt; i++)
   {
      // ignore ways which are marked for removal from the way list
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
      w = malloc_way(1, cnt);
      osm_way_default(w);
      wl->ref[i].nw = w;
      cnt = join_open_poly(&wl->ref[i], w);
      put_object((osm_obj_t*) w);
      log_debug("%d ways joined", cnt);

      // if it is not a loop, than it is a starting open way
      if (!ret)
      {
         wl->ref[i].open = 1;
         ocnt++;
      }
   }

   return ocnt;
}


/*! Helper function for qsort(). This compares node ids and order of occurence
 * within their ways (i.e. first or last node).
 */
static int compare_pdef_nid(const struct pdef *p1, const struct pdef *p2)
{
   if (p1->nid < p2->nid) return -1;
   if (p1->nid > p2->nid) return 1;

   if (p1->pn < p2->pn) return -1;
   if (p1->pn > p2->pn) return 1;

   return 0;
}


/*! Helper function for qsort(). This compares the bearings of to pdef
 * structures.
 * @param p1 Pointer to first pdef structure.
 * @param p2 Pointer to second pdef structure.
 * @return Returns -1 if 1st bearing is less then 2nd, 1 if 1st bearing is
 * greater than 2nd, or 0 if both are equal.
 */
static int compare_pdef(const struct pdef *p1, const struct pdef *p2)
{
   if (p1->pc.bearing < p2->pc.bearing) return -1;
   if (p1->pc.bearing > p2->pc.bearing) return 1;
   return 0;
}


/*! Initialization function for bearings from the center to the corner points.
 */
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
      log_msg(LOG_DEBUG, "corner_point[%d].bearing = %f (id = %"PRId64")", i, co_pt[i].pc.bearing, co_pt[i].n->obj.id);

      w->ref[3 - i] = co_pt[i].n->obj.id;
   }

   w->ref[4] = w->ref[0];
   w->ref_cnt = 5;
   set_const_tag(&w->obj.otag[1], "border", "page");
   put_object((osm_obj_t*) w);
   page_way_ = w;
}


/*! This function calculates the bearing and distance from the position defined
 * by src to a node defined by its node id nid. The result is stored to pc.
 * @param pc Pointer to a pcoord structure for the result (distance, bearing).
 * @param src Source coordinates.
 * @param nid Id of destination node id.
 * @return Returns 0 on success or -1 in case of error, i.e. if the node
 * defined by nid does not exist.
 */
static int node_brg(struct pcoord *pc, const struct coord *src, int64_t nid)
{
   osm_node_t *n;
   struct coord dst;

   if ((n = get_object(OSM_NODE, nid)) == NULL)
   {
      log_msg(LOG_ERR, "node %"PRId64" does not exist", nid);
      return -1;
   }
   dst.lat = n->lat;
   dst.lon = n->lon;
   *pc = coord_diff(src, &dst);
   return 0;
}


/*! Connect still unconnected ways.
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
   const struct corner_point *co_pt = co_pt_;

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
            // if the 2nd corner point is before the first or if the last point
            // is the first in the list, wrap around "360 degrees".
            if (l < k || j == ocnt)
               l += 4;
            // add corner points to way
            for (; k < l; k++)
            {
               // FIXME: realloc() and memmove() should be done outside of the loop
               if ((ref = realloc(wl->ref[pd[i].wl_index].nw->ref, sizeof(int64_t) * (wl->ref[pd[i].wl_index].nw->ref_cnt + 1))) == NULL)
                  log_msg(LOG_ERR, "realloc() failed: %s", strerror(errno)), exit(EXIT_FAILURE);

               memmove(&ref[1], &ref[0], sizeof(int64_t) * wl->ref[pd[i].wl_index].nw->ref_cnt); 
               ref[0] = co_pt[k % 4].n->obj.id;
               wl->ref[pd[i].wl_index].nw->ref = ref;
               wl->ref[pd[i].wl_index].nw->ref_cnt++;
               log_debug("added corner point %d (id = %"PRId64")", k % 4, co_pt[k % 4].n->obj.id);
            }
         } //if (!no_corner)

         // if start and end point belong to same way close
         if (pd[i].wl_index == pd[j % ocnt].wl_index)
         {
            if ((ref = realloc(wl->ref[pd[i].wl_index].nw->ref, sizeof(int64_t) * (wl->ref[pd[i].wl_index].nw->ref_cnt + 1))) == NULL)
               log_msg(LOG_ERR, "realloc() failed: %s", strerror(errno)), exit(EXIT_FAILURE);

            ref[wl->ref[pd[i].wl_index].nw->ref_cnt] = ref[0];
            wl->ref[pd[i].wl_index].nw->ref = ref;
            wl->ref[pd[i].wl_index].nw->ref_cnt++;
            wl->ref[pd[i].wl_index].open = 0;
            log_debug("way %"PRId64" (wl_index = %d) is now closed", wl->ref[pd[i].wl_index].nw->obj.id, pd[i].wl_index);
         }
         else
         {
            log_debug("pd[%d].wl_index(%d) != pd[%d].wl_index(%d)", i, pd[i].wl_index, j % ocnt, pd[j % ocnt].wl_index);
            if ((ref = realloc(wl->ref[pd[i].wl_index].nw->ref, sizeof(int64_t) * (wl->ref[pd[i].wl_index].nw->ref_cnt + wl->ref[pd[j % ocnt].wl_index].nw->ref_cnt))) == NULL)
               log_msg(LOG_ERR, "realloc() failed: %s", strerror(errno)), exit(EXIT_FAILURE);

            // move refs from i^th way back
            memmove(&ref[wl->ref[pd[j % ocnt].wl_index].nw->ref_cnt], &ref[0], sizeof(int64_t) * wl->ref[pd[i].wl_index].nw->ref_cnt); 
            // copy refs from j^th way to the beginning of i^th way
            memcpy(&ref[0], wl->ref[pd[j % ocnt].wl_index].nw->ref, sizeof(int64_t) * wl->ref[pd[j % ocnt].wl_index].nw->ref_cnt);
            wl->ref[pd[i].wl_index].nw->ref = ref;
            wl->ref[pd[i].wl_index].nw->ref_cnt += wl->ref[pd[j % ocnt].wl_index].nw->ref_cnt;
            // (pseudo) close j^th way
            // FIXME: onode and its refs should be free()'d and removed from tree
            wl->ref[pd[j % ocnt].wl_index].open = 0;
            // find end-point of i^th way
            for (k = 0; k < ocnt; k++)
               if ((pd[i].wl_index == pd[k].wl_index) && pd[k].pn)
               {
                  // set point index of new end point of i^th way
                  pd[k % ocnt].pn = wl->ref[pd[i].wl_index].nw->ref_cnt - 1;
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
            log_debug("way %"PRId64" (wl_index = %d) marked as closed, resorting pdef", wl->ref[pd[j % ocnt].wl_index].nw->obj.id, pd[j % ocnt].wl_index);
            return -1;
         }
         break;
      } // for (j = i + 1; j <= ocnt; j++)
   } // for (i = 0; i < ocnt; i++)
   return 0;
}


/*! Initialization function for center and corner points.
 */
void init_cat_poly(struct rdata *rd)
{
   center_.lat = rd->mean_lat;
   center_.lon = rd->mean_lon;
   //FIXME: if cat_poly is called with no_corner, co_pt_ is still used in
   //octant() thus it will segfault if init_corer_brg() is not called
   //if (rd->flags & RD_CORNER_POINTS)
      init_corner_brg(rd, &center_, co_pt_);
}


/*! Initialization function for way list structure.
 */
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


/*! This function creates an artificial tag list within the (temporary) object
 * obj. All tags which are found as parameters 'copy=*' within the parameter
 * list fp are created within obj.
 * @param fp Pointer to the parameter list.
 * @param obj Pointer to the object within which the tag list is created.
 * @return Returns the number of tags found or -1 in case of error.
 */
static int cat_poly_ini_copy(fparam_t * const *fp, osm_obj_t *obj)
{
   struct otag *ot;
   int i;

   if (fp == NULL)
      return 0;

   for (i = 0; fp[i] != NULL; i++)
   {
      if (strcasecmp(fp[i]->attr, "copy"))
         continue;
      if ((ot = realloc(obj->otag, sizeof(*ot) * (obj->tag_cnt + 1))) == NULL)
      {
         log_msg(LOG_ERR, "realloc() failed in cat_poly_ini(): %s", strerror(errno));
         free(obj->otag);
         return -1;
      }
      obj->otag = ot;
      obj->otag[obj->tag_cnt].k.buf = fp[i]->val;
      obj->otag[obj->tag_cnt].k.len = strlen(fp[i]->val);
      obj->tag_cnt++;
   }
   return obj->tag_cnt;
}
 

static int cat_poly_ini(smrule_t *r)
{
   struct catpoly *cp;

   if ((cp = calloc(1, sizeof(*cp))) == NULL)
   {
      log_msg(LOG_ERR, "calloc failed in act_cat_poly_ini(): %s", strerror(errno));
      return -1;
   }

   cp->ign_incomplete = get_param_bool("ign_incomplete", r->act);
   cp->no_corner = get_param_bool("no_corner", r->act);

   if (!cp->no_corner)
      get_rdata()->flags |= RD_CORNER_POINTS;

   if ((cp->obj.otag = malloc(sizeof(*cp->obj.otag) * r->oo->tag_cnt)) == NULL)
   {
      log_msg(LOG_ERR, "malloc() failed in cat_poly_ini(): %s", strerror(errno));
      free(cp);
      return -1;
   }
   memcpy(cp->obj.otag, r->oo->otag, sizeof(*cp->obj.otag) * r->oo->tag_cnt);
   cp->obj.tag_cnt = r->oo->tag_cnt;

   // read 'copy' parameters
   if (cat_poly_ini_copy(r->act->fp, &cp->obj) == -1)
   {
      free(cp);
      return -1;
   }

   log_msg(LOG_DEBUG, "ign_incomplete = %d, no_corner = %d", cp->ign_incomplete, cp->no_corner);
   r->data = cp;
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
   // safety check: ignore illegal ways
   if (((osm_way_t*) o)->ref_cnt < 2)
      return 0;
// FIXME: this is originally NOT commented out but it would hinder to name e.g.
// islands which consist of just a single way but are tagged in a relation.
// FIXME: If commented in it could lead to the case that overlapping ways are
// collected which have been created bevor by another cat_poly() rules. A
// safety check was added to count_poly_refs().
//   if (((osm_way_t*) o)->ref[0] == ((osm_way_t*) o)->ref[((osm_way_t*) o)->ref_cnt - 1])
//      return 0;

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
   ocnt = loop_detect(wl);
   free(pd);

   poly_join_tags(wl, r);

   log_debug("trimming ways, open_count = %d", ocnt);
   ocnt = trim_ways(wl);

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
               log_debug("%d: wl_index = %d, pn = %d, wid = %"PRId64", brg = %f", i, pd[i].wl_index, pd[i].pn, wl->ref[pd[i].wl_index].nw->obj.id, pd[i].pc.bearing);
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
   int i;

   log_msg(LOG_DEBUG, "cat_relways(id = %"PRId64")", o->id);
   ((struct catpoly*) r->data)->wl = init_wlist();
   for (i = 0; i < ((osm_rel_t*) o)->mem_cnt; i++)
   {
      if (((osm_rel_t*) o)->mem[i].type != OSM_WAY)
         continue;
      if ((w = get_object(OSM_WAY, ((osm_rel_t*) o)->mem[i].id)) == NULL)
      {
         log_msg(LOG_ERR, "way %"PRId64" of relation %"PRId64" does not exist", ((osm_rel_t*) o)->mem[i].id, o->id);
         continue;
      }
      cat_poly(r, (osm_obj_t*) w);
   }

   // create temporary rule for copying tags of the relation object to new ways.
   smrule_t tr = *r;
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
   free(((struct catpoly*) r->data)->obj.otag);
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


#define SQR(a) ((a) * (a))
#define MAX_DEVIATION 50
#define MAX_ITERATION 3
#define MAX_CFAC 2.0


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

   log_msg(LOG_WARN, "DEPRECATED. Use draw(curve=1) instead.");
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

