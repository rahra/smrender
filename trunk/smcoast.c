#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

#include "smrender.h"
#include "smlog.h"
#include "smath.h"
#include "bxtree.h"


#define OUTPUT_COASTLINE

// initial number of ref array
#define INIT_MAX_REF 20
#define MAX_OPEN_POLY 32


struct poly
{
   struct poly *next, *prev;
   struct onode *w;
   short new;           // 1 if node is virtual and was newly created
   short del;           // 1 if element should be removed from list
   short open;          // 1 if element is connected but still open way
};

struct wlist
{
   //struct wlist *next;
   int64_t id;
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


int poly_bearing(struct rdata*, struct wlist*, int, struct pcoord*, const struct coord*);
int64_t add_dummy_node(struct rdata*, const struct coord*);


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
   //(*wl)->ref[(*wl)->ref_cnt].next = NULL;
   //(*wl)->ref[(*wl)->ref_cnt].new = 0;
   //(*wl)->ref[(*wl)->ref_cnt].del = 0;
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
   struct onode *nd;

   if ((pd = calloc(wl->ref_cnt * 2, sizeof(struct pdef))) == NULL)
   {
      log_msg(LOG_EMERG, "poly_sort_by_node(): %s", strerror(errno));
      exit(EXIT_FAILURE);
   }

   for (i = 0; i < wl->ref_cnt; i++)
   {
      if ((nd = get_object(OSM_WAY, wl->ref[i].w->nd.id)) == NULL)
      {
         log_msg(LOG_WARNING, "poly_get_node_ids(): way %ld does not exist", wl->ref[i]);
         continue;
      }
      if (nd->ref_cnt < 2)
      {
         log_msg(LOG_WARNING, "poly_get_node_ids(): way %ld has less than 2 nodes", wl->ref[i]);
         continue;
      }
      pd[i].wl_index = i;
      pd[i].pn = 0;
      pd[i].nid = nd->ref[0];
      pd[i + wl->ref_cnt].wl_index = i;
      pd[i + wl->ref_cnt].pn = nd->ref_cnt - 1;
      pd[i + wl->ref_cnt].nid = nd->ref[nd->ref_cnt - 1];
   }

   return pd;
}


int poly_find_adj2(struct wlist *wl, struct pdef *pd, int pd_cnt)
{
   int i, n;

   log_debug("%d unconnected ends", wl->ref_cnt * 2);
   //for (i = 0; i < pd_cnt; i++)
   //   log_debug("pd[%d].wl_index = %d, .pn = %d, .nid = %ld", i, pd[i].wl_index, pd[i].pn, pd[i].nid);

   for (i = 0, n = 0; i < pd_cnt - 1; i++)
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
   nd->otag[0].k.buf = "natural";
   nd->otag[0].k.len = 7;
   nd->otag[0].v.buf = "coastline";
   nd->otag[0].v.len = 9;
   nd->otag[1].k.buf = "generator";
   nd->otag[1].k.len = 9;
   nd->otag[1].v.buf = "smrender";
   nd->otag[1].v.len = 8;


   return nd;
}


int join_open_poly(struct poly *pl, struct onode *nd)
{
   int pos, wcnt;
   struct poly *list;

   for (list = pl, pos = 0, wcnt = 0; list != NULL; list = list->next, wcnt++)
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
   // copy last element
   //nd->ref[pos] = list->w->ref[pl->w->ref_cnt - 1];

   return wcnt;
}


int loop_detect(struct wlist *wl)
{
   struct onode *nd;
   int i, cnt, ret;
#ifdef OUTPUT_COASTLINE
   int _i;
   FILE *f;

   if ((f = fopen("coastline.osm", "w")) == NULL)
      log_msg(LOG_ERR, "cannot open coastline file: %s", strerror(errno)),
         exit(EXIT_FAILURE);
   fprintf(f, "<?xml version='1.0' encoding='UTF-8'?>\n<osm version='0.6' generator='smrender'>\n");
#endif

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

      if (!ret)
         wl->ref[i].open = 1;

      log_debug("waylist: wl_index %d (start = %p, cnt = %d, loop = %d)", i, &wl->ref[i], cnt, ret);
      nd = create_new_coastline(cnt);
      cnt = join_open_poly(&wl->ref[i], nd);
      put_object(nd);
      log_debug("%d ways joined", cnt);
 
#ifdef OUTPUT_COASTLINE
      if (nd != NULL)
      {
         fprintf(f, "<way id=\"%ld\" version=\"1\">\n", nd->nd.id);
         for (_i = 0; _i < nd->ref_cnt; _i++)
            fprintf(f, "   <nd ref=\"%ld\"/>\n", nd->ref[_i]);
         fprintf(f, "</way>\n");
         for (_i = 0; _i < nd->ref_cnt; _i++)
            print_onode(f, get_object(OSM_NODE, nd->ref[_i]));
      }
#endif
   }

#ifdef OUTPUT_COASTLINE
   fprintf(f, "</osm>\n");
   fclose(f);
#endif

   return 0;
}


/*! poly_find_adj returns a list of nodes which are all connected together
 *  with open polygons.
 *  @param rd Pointer to struct rdata.
 *  @param wl Pointer to list of ways (open polygons). The contents of this
 *  lists are modified by poly_find_adj(). Ways are removed as soon as they are
 *  found to be connected to other ways within this list.
 *  @return Returns a pointer to a struct wlist which contains a list of all
 *  points which are connected. The points are listed in the right order. The
 *  structure must be freed again with free() after usage.
 */
struct wlist *poly_find_adj(struct rdata *rd, struct wlist *wl)
{
   int i, j;
   struct wlist *nl;
   struct onode *nd;

   if (!wl->ref_cnt)
      return NULL;

   if ((nd = get_object(OSM_WAY, wl->ref[0].w->nd.id)) == NULL)
      return NULL;

   if ((nl = malloc(sizeof(struct poly) * nd->ref_cnt + sizeof(struct wlist))) == NULL)
      perror("malloc"), exit(EXIT_FAILURE);

   // copy all node ids to temporary node list nl
   memcpy(nl->ref, nd->ref, sizeof(struct poly) * nd->ref_cnt);
   nl->ref_cnt = nl->max_ref = nd->ref_cnt;
   nl->id = nd->nd.id;

   // remove way from way list
   memmove(wl->ref, &wl->ref[1], sizeof(int64_t) * (wl->ref_cnt - 1));
   wl->ref_cnt--;

   // FIXME: outer loop may run less than ref_cnt
   for (j = 0; j < wl->ref_cnt; j++)
   for (i = 0; i < wl->ref_cnt; i++)
   {
      if ((nd = get_object(OSM_WAY, wl->ref[i].w->nd.id)) == NULL)
         return NULL;

      if (nd->ref_cnt < 2)
      {
         log_warn("ignoring way id = %ld, ref_cnt = %d", nd->nd.id, nd->ref_cnt);
         continue;
      }

      //check if they are connected
      if (nd->ref[0] == nl->ref[nl->ref_cnt - 1].w->nd.id)
      {
         // nodes are connected to the end of the node list
         if ((nl = realloc(nl, sizeof(struct poly) * (nl->max_ref + nd->ref_cnt - 1) + sizeof(struct wlist))) == NULL)
            perror("realloc"), exit(EXIT_FAILURE);
         memcpy(&nl->ref[nl->ref_cnt], &nd->ref[1], (nd->ref_cnt - 1) * sizeof(int64_t));
         nl->ref_cnt += nd->ref_cnt - 1;
         nl->max_ref = nl->ref_cnt;

         // remove way from way list
         memmove(&wl->ref[i], &wl->ref[i + 1], sizeof(int64_t) * (wl->ref_cnt - 1 - i));
         wl->ref_cnt--;
         i--;
      }
      else if (nl->ref[0].w->nd.id == nd->ref[nd->ref_cnt - 1])
      {
         // nodes are connected to the beginning of the node list
         if ((nl = realloc(nl, sizeof(struct poly) * (nl->max_ref + nd->ref_cnt - 1) + sizeof(struct wlist))) == NULL)
            perror("realloc"), exit(EXIT_FAILURE);
         memmove(&nl->ref[nd->ref_cnt - 1], &nl->ref[0], nl->ref_cnt * sizeof(int64_t));
         memcpy(&nl->ref[0], &nd->ref[0], (nd->ref_cnt - 1) * sizeof(int64_t));
         nl->ref_cnt += nd->ref_cnt - 1;
         nl->max_ref = nl->ref_cnt;
      
         // remove way from way list
         memmove(&wl->ref[i], &wl->ref[i + 1], sizeof(int64_t) * (wl->ref_cnt - 1 - i));
         wl->ref_cnt--;
         i--;
      }
   }

   return nl;
}


#if 0
void poly_node_to_border(struct rdata *rd, struct wlist *nl)
{
   int i;
   bx_node_t *bn;
   struct onode *nd;

   for (i = 0; i < nl->ref_cnt; i++)
   {
      if ((bn = bx_get_node(rd->nodes, nl->ref[i])) == NULL)
      {
         fprintf(stderr, "*** null pointer catched\n");
         continue;
      }
      if ((nd = bn->next[0]) == NULL)
      {
         fprintf(stderr, "*** null pointer catched\n");
         continue;
      }

      // correct coordinates to border of page
      if (nd->nd.lat > rd->y1c) nd->nd.lat = rd->y1c;
      if (nd->nd.lat < rd->y2c) nd->nd.lat = rd->y2c;
      if (nd->nd.lon < rd->x1c) nd->nd.lon = rd->x1c;
      if (nd->nd.lon > rd->x2c) nd->nd.lon = rd->x2c;
   }
}
#endif


#ifdef OUTPUT_COASTLINE
int poly_out(FILE *f, struct wlist *nl, struct rdata *rd)
{
   int i;

   // FIXME: with this print_onode(), something is wrong...
   //print_onode(f, get_object(OSM_WAY, nl->id));

   fprintf(f, "<way id=\"%ld\" version=\"1\">\n", nl->id);
   for (i = 0; i < nl->ref_cnt; i++)
      fprintf(f, "   <nd ref=\"%ld\"/>\n", nl->ref[i].w->nd.id);
   fprintf(f, "</way>\n");

   for (i = 0; i < nl->ref_cnt; i++)
      print_onode(f, get_object(OSM_NODE, nl->ref[i].w->nd.id));

   return 0;
}
#endif


int compare_pdef_nid(const struct pdef *p1, const struct pdef *p2)
{
   //if (p1->wl_index < p2->wl_index) return -1;
   //if (p1->wl_index > p2->wl_index) return 1;
   //
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


int enlarge_ref(struct wlist **wl, int n)
{
   if ((*wl = realloc(*wl, sizeof(struct wlist) + sizeof(struct poly) * ((*wl)->max_ref + n))) == NULL)
      perror("realloc"), exit(EXIT_FAILURE);
   (*wl)->max_ref++;

   return 0;
}


int connect_open_poly(struct rdata *rd, struct wlist **wl, int n)
{
   const gdPoint ic[] = {{gdImageSX(rd->img), -1}, {gdImageSX(rd->img), gdImageSY(rd->img)}, {-1, gdImageSY(rd->img)}, {-1, -1}};
   struct pdef pd[n * 2 + 1];
   // center coord
   struct coord c, d;
   // corner points
   int64_t cp[5];
   struct pcoord pc[5];
   int i, j, k;

   // center coordinates
   c.lat = rd->mean_lat;
   c.lon = (rd->x1c + rd->x2c) / 2;

   // generate corner points
   for (i = 0; i < 4; i++)
   {
      // FIXME: coords could be taken directly from rd
      mk_chart_coords(ic[i].x, ic[i].y, rd, &d.lat, &d.lon);
      pc[i] = coord_diff(&c, &d);
      cp[i] = add_dummy_node(rd, &d);
   }
   cp[4] = cp[0];
   pc[4] = pc[0];

   // calculate bearing and distance to each start and end point of wl.
   for (i = 0; i < n; i++)
   {
      if ((pd[i].pn = poly_bearing(rd, wl[i], 0, &pd[i].pc, &c)) == -1)
         return -1;
      if ((pd[i + n].pn = poly_bearing(rd, wl[i], wl[i]->ref_cnt - 1, &pd[i + n].pc, &c)) == -1)
         return -1;
      pd[i].wl_index = pd[i + n].wl_index = i;
   }

   qsort(pd, n * 2, sizeof(struct pdef), (int(*)(const void *, const void *)) compare_pdef);

   // if first point in list is not start point move it to the end
   // FIXME: this is a qick n dirty solution. 
   if (pd[0].pn)
   {
      memcpy(&pd[n * 2], &pd[0], sizeof(struct pdef));
      memcpy(&pd[0], &pd[1], sizeof(struct pdef) * n * 2);
   }


   for (i = 0; i < n * 2 - 1; i++)
   {
      // FIXME: there should be a check if i is an end point and i + 1 is a
      // start point

      log_msg(LOG_DEBUG, "pd[%d].wl_index = %d, pd[%d].wl_index = %d", i,
            pd[i].wl_index, i + 1, pd[i + 1].wl_index);
      // check if endpoint is of same list as next start point
      if (pd[i].wl_index == pd[i + 1].wl_index)
      {
         // find next corner point for i
         for (j = 0; j < 4; j++)
            if (pd[i].pc.bearing < pc[j].bearing)
               break;
         // find next corner point for j
         for (k = 0; k < 4; k++)
            if (pd[i + 1].pc.bearing < pc[k].bearing)
               break;

         // add corner points to way
         for (; j < k; j++)
         {
            if (wl[pd[i].wl_index]->max_ref <= wl[pd[i].wl_index]->ref_cnt)
               enlarge_ref(&wl[pd[i].wl_index], 1);
            wl[pd[i].wl_index]->ref[wl[pd[i].wl_index]->ref_cnt].w->nd.id = cp[j];
            wl[pd[i].wl_index]->ref_cnt++;
         }

         // check if list has enough entries, otherwise reserve memory
         if (wl[pd[i].wl_index]->max_ref <= wl[pd[i].wl_index]->ref_cnt)
            enlarge_ref(&wl[pd[i].wl_index], 1);

         // set end point to start point
         wl[pd[i].wl_index]->ref[wl[pd[i].wl_index]->ref_cnt] = wl[pd[i].wl_index]->ref[0];
         wl[pd[i].wl_index]->ref_cnt++;
         i++;
      }
      else
      {
      }
   }

   return 0;
}


/*! Calculate bearing/dist to node n of wlist. The result is stored into pc.
 *  @param rd Pointer to struct rdata.
 *  @param nl Pointer to wlist.
 *  @param n Index number of node of wlist.
 *  @param pc Pointer to struct pcoord which receives the result.
 *  @param c Pointer struct coord which contains the source coordinates from
 *  which the bearing and distance is calculated.
 *  @return The function returns n if everything is ok. On error -1 is returned.
 */
int poly_bearing(struct rdata *rd, struct wlist *nl, int n, struct pcoord *pc, const struct coord *c)
{
   //bx_node_t *bn;
   struct onode *nd;
   struct coord dst;

   if ((n >= nl->ref_cnt) || (n < 0))
      return -1;

   if ((nd = get_object(OSM_NODE, nl->ref[n].w->nd.id)) == NULL)
      return -1;

   dst.lat = nd->nd.lat;
   dst.lon = nd->nd.lon;

   *pc = coord_diff(c, &dst);
   return n;
}


int64_t add_dummy_node(struct rdata *rd, const struct coord *c)
{
   struct onode *ond;

   ond = malloc_object(0, 0);
   ond->nd.id = unique_node_id();
   ond->nd.type = OSM_NODE;
   ond->nd.ver = 1;
   ond->nd.lat = c->lat;
   ond->nd.lon = c->lon;
   put_object(ond);

   return ond->nd.id;

}


int64_t add_coast_way(struct rdata *rd, const struct wlist *nl)
{
   struct onode *ond;

   ond = malloc_object(1, nl->ref_cnt);
   memcpy(ond->ref, nl->ref, nl->ref_cnt * sizeof(int64_t));

   ond->nd.id = unique_way_id();
   ond->tag_cnt = 1;
   ond->nd.type = OSM_WAY;
   ond->nd.ver = 1;

   ond->otag[0].k.buf = "natural";
   ond->otag[0].k.len = 7;
   ond->otag[0].v.buf = "coastline";
   ond->otag[0].v.len = 9;

   put_object(ond);

   return ond->nd.id;
}


int cat_poly(struct rdata *rd)
{
   int i, nl_cnt, pd_cnt;
   struct wlist *wl, *nl[MAX_OPEN_POLY];
   struct pdef *pd;
#ifdef OUTPUT_COASTLINE
   FILE *f;
#endif

   if ((wl = malloc(sizeof(*wl) + INIT_MAX_REF * sizeof(struct poly))) == NULL)
      perror("malloc"), exit(EXIT_FAILURE);

   wl->ref_cnt = 0;
   wl->max_ref = INIT_MAX_REF;

   log_debug("collecting open coastline polygons");
   traverse(rd->obj, 0, IDX_WAY, (tree_func_t) gather_poly, rd, &wl);
   //for (i = 0; i < wl->ref_cnt; i++)
   //   log_debug("open coastline %ld", wl->ref[i].w->nd.id);

   pd = poly_get_node_ids(wl);

   for (pd_cnt = wl->ref_cnt * 2; pd_cnt;)
   {
      qsort(pd, pd_cnt, sizeof(struct pdef), (int(*)(const void *, const void *)) compare_pdef_nid);
      poly_find_adj2(wl, pd, pd_cnt);
      loop_detect(wl);

      pd_cnt = 0;
   }

#if 0
#ifdef OUTPUT_COASTLINE
   if ((f = fopen("open_coastline.osm", "w")) == NULL)
      perror("fopen"), exit(EXIT_FAILURE);
   fprintf(f, "<?xml version='1.0' encoding='UTF-8'?>\n<osm version='0.6' generator='smrender'>\n");
#endif

   for (i = 0; (nl[i] = poly_find_adj(rd, wl)) != NULL; i++)
   {
      log_debug("connected way, ref_cnt = %d, ref[0] = %ld, ref[%d] = %ld",
            nl[i]->ref_cnt, nl[i]->ref[0], nl[i]->ref_cnt - 1, nl[i]->ref[nl[i]->ref_cnt - 1]);

      // check if wlist is closed
      if (nl[i]->ref[0].id == nl[i]->ref[nl[i]->ref_cnt - 1].id)
      {
         (void) add_coast_way(rd, nl[i]);
#ifdef OUTPUT_COASTLINE
         poly_out(f, nl[i], rd);
#endif
         free(nl[i]);
         i--;
      }
#ifdef OUTPUT_COASTLINE
      else
         poly_out(f, nl[i], rd);
#endif
 
   }
   nl_cnt = i;

   connect_open_poly(rd, nl, nl_cnt);

   for (i = 0; i < nl_cnt; i++)
   {
      if (nl[i]->ref[0].id == nl[i]->ref[nl[i]->ref_cnt - 1].id)
      {
         log_debug("now connected way");
         add_coast_way(rd, nl[i]);
#ifdef OUTPUT_COASTLINE
         poly_out(f, nl[i], rd);
#endif
      }
      free(nl[i]);
   }

#ifdef OUTPUT_COASTLINE
   fprintf(f, "</osm>\n");
   fclose(f);
#endif
#endif

   free(wl);
   return 0;
}

