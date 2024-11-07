/* Copyright 2011-2021 Bernhard R. Fischer, 4096R/8E24F29D <bf@abenteuerland.at>
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

#define DODECANT
#ifdef DODECANT
//! number of "corner" points
#define NUM_CO 8
enum {I_NE, I_E, I_SE, I_S, I_SW, I_W, I_NW, I_N};
#else
//! number of "corner" points
#define NUM_CO 4
enum {I_NE, I_SE, I_SW, I_NW};
#endif


struct refine
{
   double deviation;
   int iteration;
};


static int node_brg(struct pcoord*, const struct coord*, int64_t);


static struct corner_point co_pt_[NUM_CO];
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
 *
 *   NW |  N  | N|1 | NE
 *  ---------------------
 *  W|1 |           | E
 *  -----           -----
 *    W |           | E|1
 *  ---------------------
 *   SW | S|1 |  S  | SE
 *
 *
 * @param crd Pointer to position.
 * @return Returns Octant as a combination of the logical or'ed flags  POS_N,
 * POS_S, POS_E, POS_W. If the position is inside the rendering window 0 is
 * returned.
 */
static int octant(const struct coord *crd)
{
   int pos = 0;

   if (crd->lat > co_pt_[I_NE].n->lat)
      pos |= POS_N;
   else if (crd->lat < co_pt_[I_SE].n->lat)
      pos |= POS_S;

   if (crd->lon > co_pt_[I_NE].n->lon)
      pos |= POS_E;
   else if (crd->lon < co_pt_[I_NW].n->lon)
      pos |= POS_W;

   return pos;
}


#ifdef DODECANT
/*! This function returns the position of the coordinates given by crd as being
 * in the location of one of 12 areas. This is similar to the function octant()
 * but with 12 instead of just 8 areas. These areas are NE, SE, SW, and NW, and
 * the for main directions N, E, S, and W are split into half. This is the
 * difference to octant(). If the coordinates lie in the higher range in
 * respect to the bearing, the flag POS_1 is set additionally. E.g. if the
 * coordinates are located above the page on the right half, the flags POS_N |
 * POS_1 are set. On the left half just POS_N would be set.
 * @param crd Pointer to the coordinates to test.
 * @return the function returns a valid combination of the flags POS_N, POS_E,
 * POS_S, POS_W, and POS_1. If the coordinates are within the page range, 0 is
 * returned.
 */
static int dodecant(const struct coord *crd)
{
   int pos = 0;

   if (!(pos = octant(crd)))
      return 0;

   switch (pos)
   {
      case POS_N:
         if (crd->lon > co_pt_[I_N].n->lon)
            pos |= POS_1;
         break;

      case POS_E:
         if (crd->lat < co_pt_[I_E].n->lat)
            pos |= POS_1;
         break;

      case POS_S:
         if (crd->lon < co_pt_[I_S].n->lon)
            pos |= POS_1;
         break;

      case POS_W:
         if (crd->lat > co_pt_[I_W].n->lat)
            pos |= POS_1;
         break;

      case POS_NE:
      case POS_SE:
      case POS_SW:
      case POS_NW:
         return pos;

      default:
         log_msg(LOG_EMERG, "this should never happen");
   }

   return pos;
}
#endif


/*! This function tests of all bits in tst are set in pos. It returns 1 if they
 * are set, otherwise 0 is returned.
 */
int check_bits(int pos, int tst)
{
   return (pos & tst) == tst;
}


/*! This function returns the nearest edgepoint to the coordinates crd, the
 * outside position are pos, and the inside node given by nid. If the area
 * location is one of POS_NE, POS_SE, POS_SW, or POS_NW, the global corner
 * point of the page is returned.
 * In all other cases a new node is created with approx. positions exactly at
 * the edge of the page to the desired area (octant or dodecant).
 * @param crd
 * @param pos
 * @param nid
 * @return The function returns a suitable node id.
 */
static int64_t edge_point(struct coord crd, int pos, int64_t nid)
{
   osm_node_t *n;

   //log_debug("trimming way %ld, %d - %d out of page, octant = 0x%02x", (long) w->obj.id, 0, i - 1, p[0]);
   // FIXME: inserting corner points is not really correct
   // Bearing between inner point and out pointer should be compare to
   // the bearing from the inner point to the corner point.
   if (check_bits(pos, POS_NE))
      return co_pt_[I_NE].n->obj.id;
   if (check_bits(pos, POS_SE))
      return co_pt_[I_SE].n->obj.id;
   if (check_bits(pos, POS_SW))
      return co_pt_[I_SW].n->obj.id;
   if (check_bits(pos, POS_NW))
      return co_pt_[I_NW].n->obj.id;

   // FIXME: coordinates of new edge points are not correct. They deviate from
   // their intended location.
   n = get_object(OSM_NODE, nid);
   switch (pos & POS_DIR_MSK)
   {
      case POS_N:
         crd.lon += (n->lon - crd.lon) * (n->lat - co_pt_[I_NE].n->lat) / (n->lat - crd.lat);
         crd.lat = co_pt_[I_NE].n->lat;
         break;
      case POS_S:
         crd.lon += (n->lon - crd.lon) * (n->lat - co_pt_[I_SE].n->lat) / (n->lat - crd.lat);
         crd.lat = co_pt_[I_SW].n->lat;
         break;
      case POS_E:
         crd.lat += (n->lat - crd.lat) * (n->lon - co_pt_[I_NE].n->lon) / (n->lon - crd.lon);
         crd.lon = co_pt_[I_NE].n->lon;
         break;
      case POS_W:
         crd.lat += (n->lat - crd.lat) * (n->lon - co_pt_[I_NW].n->lon) / (n->lon - crd.lon);
         crd.lon = co_pt_[I_NW].n->lon;
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
 * @return It returns the index of the first node within the original reference
 * list of the way which was inside. This node is at index 1 after modification
 * and at index 0 if a newly generated edge point was inserted. If 0 is
 * returned the way was not modified, i.e. even the first point is inside. -1
 * is returned in case of error which occures if all nodes are outside.
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
#ifndef DODECANT
      if (!(p[1] = octant(&crd)))
#else
      if (!(p[1] = dodecant(&crd)))
#endif
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


/*! This function finds adjacent ways. If two different ways share the same 1st
 * or last node, they are considered to be adjacent (and can be connected in a
 * further step). The list of nodes defined by pd has to be sorted ascendingly
 * by their node ids for poly_find_adj2() to work properly.
 * @param wl Pointer to the way list.
 * @param pd Pointer to the node list, which obviously has to have twice as
 * much entries as the way list.
 * @return The function returnes to number of nodes that are found to be used
 * by the different ways. I.e. if 1 is returned, there are two ways which share
 * one node.
 */
static int poly_find_adj2(struct wlist *wl, struct pdef *pd)
{
   int i, j, n;

   log_debug("%d unconnected ends", wl->ref_cnt * 2);
   for (i = 0, n = 0; i < wl->ref_cnt * 2 - 1; i++)
   {
      if (pd[i].nid == pd[i + 1].nid)
      {
         // safety check for more than 2 ways sharing the same 1st/last node
         for (j = 2; j + i < wl->ref_cnt * 2 && pd[i].nid == pd[i + j].nid; j++);
         if (j > 2)
            log_msg(LOG_WARN, "possible data error: end node %"PRId64 " is shared by %d ways", pd[i].nid, j);

         // detect improper way direction
         if ((!pd[i].pn && !pd[i + 1].pn) || (pd[i].pn && pd[i + 1].pn))
            log_msg(LOG_WARN, "possible data error: either way %"PRId64" or %"PRId64" has wrong direction",
                  wl->ref[pd[i].wl_index].w->obj.id, wl->ref[pd[i + 1].wl_index].w->obj.id);

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
         log_msg(LOG_WARN, "possible data error: loop error in ways %"PRId64" and %"PRId64", overlapping?", list->w->obj.id, list->next->prev->w->obj.id);
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
         //log_msg(LOG_WARN, "something went wrong in count_poly_refs()");
         continue;
      }

      // check if way is intermediate way and continue in that case
      if (!ret && (wl->ref[i].prev != NULL))
      {
         //log_debug("way on wl_index %d is intermediate way", i);
         continue;
      }

      log_debug("waylist: wl_index %d (cnt = %d, loop = %d)", i, cnt, ret);
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
#ifndef DODECANT
   struct coord corner_coord[NUM_CO] = {rd->bb.ru, {rd->bb.ll.lat, rd->bb.ru.lon}, rd->bb.ll, {rd->bb.ru.lat, rd->bb.ll.lon}};
#else
   struct coord corner_coord[NUM_CO] = {rd->bb.ru, {0, rd->bb.ru.lon}, {rd->bb.ll.lat, rd->bb.ru.lon}, {rd->bb.ll.lat, 0}, rd->bb.ll, {0, rd->bb.ll.lon}, {rd->bb.ru.lat, rd->bb.ll.lon}, {rd->bb.ru.lat, 0}};
   corner_coord[I_E].lat = corner_coord[I_W].lat = (rd->bb.ru.lat + rd->bb.ll.lat) / 2;
   corner_coord[I_S].lon = corner_coord[I_N].lon = (rd->bb.ru.lon + rd->bb.ll.lon) / 2;
#endif
   osm_way_t *w;
   int i;


   w = malloc_way(2, NUM_CO + 1);
   osm_way_default(w);
   for (i = 0; i < NUM_CO; i++)
   {
      co_pt[i].pc = coord_diff(src, &corner_coord[i]);
      co_pt[i].n = malloc_node(2);
      osm_node_default(co_pt[i].n);
      co_pt[i].n->lat = corner_coord[i].lat;
      co_pt[i].n->lon = corner_coord[i].lon;
      set_const_tag(&co_pt[i].n->obj.otag[1], "grid", "pagecorner");
      put_object((osm_obj_t*) co_pt[i].n);
      log_msg(LOG_DEBUG, "corner_point[%d].bearing = %f (id = %"PRId64")", i, co_pt[i].pc.bearing, co_pt[i].n->obj.id);

      w->ref[NUM_CO - 1 - i] = co_pt[i].n->obj.id;
   }

   w->ref[NUM_CO] = w->ref[0];
   w->ref_cnt = NUM_CO + 1;
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
 *  @return The function returns the number of ways which have been connected.
 *  If no open ways remain, 0 is returned.
 */
static int connect_open(struct pdef *pd, struct wlist *wl, int ocnt, short no_corner)
{
   int i, j, k, l, cnt;
   int64_t *ref;
   const struct corner_point *co_pt = co_pt_;

   for (i = 0, cnt = 0; i < ocnt; i++)
   {
      // skip end points and loops
      if (pd[i].pn || !wl->ref[pd[i].wl_index].open)
      {
         //log_debug("skipping i = %d", i);
         continue;
      }

      for (j = i + 1; j < i + ocnt; j++)
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
            for (k = 0; k < NUM_CO; k++)
               if (pd[i].pc.bearing < co_pt[k].pc.bearing)
                  break;
            // find next corner point for j
            for (l = 0; l < NUM_CO; l++)
               if (pd[j % ocnt].pc.bearing < co_pt[l].pc.bearing)
                  break;
            // if the 2nd corner point is before the first or if the last point
            // is the first in the list, wrap around "360 degrees".
            if (l < k || j >= ocnt)
               l += NUM_CO;
            // add corner points to way
            for (; k < l; k++)
            {
               // FIXME: realloc() and memmove() should be done outside of the loop
               if ((ref = realloc(wl->ref[pd[i].wl_index].nw->ref, sizeof(int64_t) * (wl->ref[pd[i].wl_index].nw->ref_cnt + 1))) == NULL)
                  log_msg(LOG_ERR, "realloc() failed: %s", strerror(errno)), exit(EXIT_FAILURE);

               memmove(&ref[1], &ref[0], sizeof(int64_t) * wl->ref[pd[i].wl_index].nw->ref_cnt); 
               ref[0] = co_pt[k % NUM_CO].n->obj.id;
               wl->ref[pd[i].wl_index].nw->ref = ref;
               wl->ref[pd[i].wl_index].nw->ref_cnt++;
               log_debug("added corner point %d (id = %"PRId64")", k % NUM_CO, co_pt[k % NUM_CO].n->obj.id);
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
            cnt++;
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
            cnt++;
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
            goto co_exit;
         }
         break;
      } // for (j = i + 1; j <= ocnt; j++)
   } // for (i = 0; i < ocnt; i++)

co_exit:
   log_debug("%d ways connected", cnt);
   return cnt;
}


double node_diff(const osm_node_t *n0, const osm_node_t *n1, struct pcoord *pc)
{
   struct coord sc, dc;
   struct pcoord pc0;

   sc.lat = n0->lat;
   sc.lon = n0->lon;
   dc.lat = n1->lat;
   dc.lon = n1->lon;

   pc0 = coord_diff(&sc, &dc);
   if (pc != NULL)
      *pc = pc0;

   return pc0.dist;
}


/*! Calculate the distance between the end nodes of a way.
 * @param w Pointer to the way.
 * @return Returns the distance in degrees between the ways. The distance is
 * always >= 0. In case of error -1 is returned.
 */
double end_node_dist(const osm_way_t *w)
{
   osm_node_t *n[2];
   struct pcoord pc;

   if ((n[0] = get_object(OSM_NODE, w->ref[0])) == NULL)
   {
      log_msg(LOG_WARN, "first node %"PRId64" of way %"PRId64" does not exist", w->ref[0], w->obj.id);
      return -1;
   }

   if ((n[1] = get_object(OSM_NODE, w->ref[w->ref_cnt - 1])) == NULL)
   {
      log_msg(LOG_WARN, "last node %"PRId64" of way %"PRId64" does not exist", w->ref[w->ref_cnt - 1], w->obj.id);
      return -1;
   }

   node_diff(n[0], n[1], &pc);
   return pc.dist;
}


/*! This function calculates of the distance of the end nodes of the way w are
 * nearer than max_dist. In this case the way will be closed, i.e. the first
 * node is inserted at the end.
 * @param w Pointer to the way.
 * @param max_dist Maximum distance between the end nodes up to which the way
 * will be closed.
 * @return The function returns 1 of the way was closed. 0 is returned if it
 * was not closed because the end nodes are too far aways (>= max_dist). In
 * case of error, -1 is returned.
 */
int connect_almost_closed_way(osm_way_t *w, double max_dist)
{
   double dist;

   if ((dist = end_node_dist(w)) < 0)
      return -1;

   if (dist < max_dist)
   {
      log_debug("minimum distance in way %"PRId64" (ref_cnt = %d) found between %"PRId64" and %"PRId64,
            w->obj.id, w->ref_cnt, w->ref[0], w->ref[w->ref_cnt - 1]);

      if (realloc_refs(w, w->ref_cnt + 1) == -1)
         return -1;

      w->ref[w->ref_cnt - 1] = w->ref[0];
      return 1;
   }

   return 0;
}


/*! This function closes all "almost" closed ways in the way list wl.
 * @param wl Pointer to a way list.
 * @return The function returns the number of ways which where closed.
 */
int connect_almost_closed(struct wlist *wl, double max_dist)
{
   int i, cnt, e;

   for (i = 0, cnt = 0; i < wl->ref_cnt; i++)
   {
      if (!wl->ref[i].open)
         continue;

      if ((e = connect_almost_closed_way(wl->ref[i].w, max_dist)) == -1 )
      {
         log_msg(LOG_ERR, "connect_almost_closed_way() failed");
         continue;
      }

      if (!e)
         continue;

      wl->ref[i].open = 0;
      cnt++;
   }

   log_debug("closed %d ways", cnt);
   return cnt;
}


/*! Initialization function for center and corner points.
 */
void init_cat_poly(struct rdata *rd)
{
   static int _called = 0;

   if (_called)
      return;
   _called++;

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
      goto cpi_exit;
   }
   memcpy(cp->obj.otag, r->oo->otag, sizeof(*cp->obj.otag) * r->oo->tag_cnt);
   cp->obj.tag_cnt = r->oo->tag_cnt;

   // read 'copy' parameters
   if (cat_poly_ini_copy(r->act->fp, &cp->obj) == -1)
         goto cpi_exit;

   //FIXME: parse_length_def() should be used
   if (get_param("vcdist", &cp->vcdist, r->act) != NULL)
   {
      cp->vcdist /= 60;
      if (cp->vcdist < 0)
      {
         log_msg(LOG_ERR, "vcdist must be >= 0 (dist = %f)", cp->vcdist);
         goto cpi_exit;
      }
   }
   else
      //cp->vcdist = VC_DIST; //FIXME: that does not work always, so set to 0 again
      cp->vcdist = 0;

   log_msg(LOG_DEBUG, "ign_incomplete = %d, no_corner = %d, vcdist = %f", cp->ign_incomplete, cp->no_corner, cp->vcdist * 60);
   r->data = cp;
   return 0;

cpi_exit:
   free(cp);
   return -1;
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

   return 0;
}


/*! This function does some data checks on a way's references. If the first or
 * last node appears multiple times at the beginning or end, the nodes are
 * eliminated. If a way consists only of nodes with the same id it actually has
 * 0 length. In this case 1 is returned and the way may be removed.
 * @param w Pointer to the way to check.
 * @return The function returns 1 if it is a 0-length way, meaning all nodes
 * have the same id. Otherwise 0 is returned. Nevertheless, if 0 is returned
 * some nodes may still have been removed at the beginning and/or the end. A
 * different ref_cnt before and after the call indicates this.
 */
int check_way(osm_way_t *w)
{
   int i;

#if 1
   // this is the code before 9f46c03 which seems to have worked better
   for (i = 1; i < w->ref_cnt; i++)
      if (w->ref[i] != w->ref[0])
         break;

   // all nodes are the same
   if (i >= w->ref_cnt)
      goto cn_err_exit;

   // first node appears multiple times
   if (i > 1)
   {
      log_debug("eliminating duplicate starting nodes 1 - %d in way %"PRId64, i - 1, w->obj.id);
      memmove(&w->ref[1], &w->ref[i], (w->ref_cnt - i) * sizeof(*w->ref));
      w->ref_cnt -= i - 1;
   }

   // check nodes from the back of the list
   for (i = w->ref_cnt - 2; i >= 0; i--)
      if (w->ref[i] != w->ref[w->ref_cnt - 1])
         break;

   // all nodes are the same
   if (i <= -1)
      goto cn_err_exit;

   if (i < w->ref_cnt - 2)
   {
      log_debug("shortening way %"PRId64" from %d to %d", w->obj.id, w->ref_cnt, i + 2);
      w->ref_cnt = i + 2;
   }

   return 0;

cn_err_exit:
   log_debug("all nodes of way %"PRId64" have the same id", w->obj.id);
   return 1;

#else
   // this is the code since revision 9f46c03 which seems to have a bug
   for (i = 0; i < w->ref_cnt - 1;)
   {
      // check if consecutive nodes are equal
      if (w->ref[i] == w->ref[i + 1])
      {
         log_debug("eliminating duplicate nodes %d/%d in way %"PRId64, i, i + 1, w->obj.id);
         memmove(&w->ref[i], &w->ref[i + 1], (w->ref_cnt - i) * sizeof(*w->ref));
         w->ref_cnt--;
         continue;
      }
      i++;
   }

   // if only 1 node is left, all nodes have been equal
   if (i <= 1)
      w->ref_cnt = 0;

   return w->ref_cnt == 0;
#endif
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

   //safety check, 0-length may ways cause chaos..
   for (int i = 0; i < ((osm_way_t*) o)->ref_cnt; i++)

   if (check_way((osm_way_t*) o))
   {
      log_debug("ignoring 0-length way %"PRId64", ref_cnt = %d", o->id, ((osm_way_t*) o)->ref_cnt);
      return 0;
   }

   return gather_poly0((osm_way_t*) o, &((struct catpoly*)r->data)->wl);

}


static int cat_poly_fini(smrule_t *r)
{
   struct catpoly *cp = r->data;
   struct wlist *wl = cp->wl;
   struct pdef *pd;
   int i, ocnt;

   // init corner points
   init_cat_poly(get_rdata());

   pd = poly_get_node_ids(wl);
   qsort(pd, wl->ref_cnt * 2, sizeof(struct pdef), (int(*)(const void *, const void *)) compare_pdef_nid);
   poly_find_adj2(wl, pd);
   ocnt = loop_detect(wl);
   free(pd);

   poly_join_tags(wl, r);

   log_debug("closing almost closed ways, ocnt = %d", ocnt);
   ocnt -= connect_almost_closed(wl, cp->vcdist);

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

