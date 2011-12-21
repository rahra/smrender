#include <unistd.h>
#include <string.h>
#include <stdio.h>

#include "smrender.h"
#include "smlog.h"
#include "smath.h"
#include "bxtree.h"


// initial number of ref array
#define INIT_MAX_REF 20
#define MAX_OPEN_POLY 32


struct wlist
{
   int64_t id;
   int ref_cnt, max_ref;
   //struct pcoord start, end;
   int64_t ref[];
};

struct pdef
{
   //struct wlist *wl;
   int wl_index;
   int pn;           // index number of destined point
   struct pcoord pc; // bearing to pointer
};


int poly_bearing(struct rdata*, struct wlist*, int, struct pcoord*, const struct coord*);
int64_t add_dummy_node(struct rdata*, const struct coord*);


/*! This finds open polygons with tag natural=coastline and adds
 *  the node references to the wlist structure.
 */
void gather_poly(struct onode *nd, struct rdata *rd, struct wlist **wl)
{
   // check if it is an open polygon
   if (nd->ref_cnt < 2)
      return;
   if (nd->ref[0] == nd->ref[nd->ref_cnt - 1])
      return;

   // check if it is a coastline
   if (match_attr(nd, "natural", "coastline") == -1)
      return;

   // check if there's enough memory
   if ((*wl)->ref_cnt >= (*wl)->max_ref)
   {
      if ((*wl = realloc(*wl, sizeof(struct wlist) + ((*wl)->max_ref + INIT_MAX_REF) * sizeof(int64_t))) == NULL)
         perror("realloc"), exit(EXIT_FAILURE);
      (*wl)->max_ref += INIT_MAX_REF;
   }

   // add way to list
   (*wl)->ref[(*wl)->ref_cnt] = nd->nd.id;
   (*wl)->ref_cnt++;
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

   if ((nd = get_object(OSM_WAY, wl->ref[0])) == NULL)
      return NULL;

   if ((nl = malloc(sizeof(int64_t) * nd->ref_cnt + sizeof(struct wlist))) == NULL)
      perror("malloc"), exit(EXIT_FAILURE);

   // copy all node ids to temporary node list nl
   memcpy(nl->ref, nd->ref, sizeof(int64_t) * nd->ref_cnt);
   nl->ref_cnt = nl->max_ref = nd->ref_cnt;
   nl->id = nd->nd.id;

   // remove way from way list
   memmove(wl->ref, &wl->ref[1], sizeof(int64_t) * (wl->ref_cnt - 1));
   wl->ref_cnt--;

   // FIXME: outer loop may run less than ref_cnt
   for (j = 0; j < wl->ref_cnt; j++)
   for (i = 0; i < wl->ref_cnt; i++)
   {
      if ((nd = get_object(OSM_WAY, wl->ref[i])) == NULL)
         return NULL;

      if (nd->ref_cnt < 2)
      {
         log_warn("ignoring way id = %ld, ref_cnt = %d", nd->nd.id, nd->ref_cnt);
         continue;
      }

      //check if they are connected
      if (nd->ref[0] == nl->ref[nl->ref_cnt - 1])
      {
         // nodes are connected to the end of the node list
         if ((nl = realloc(nl, sizeof(int64_t) * (nl->max_ref + nd->ref_cnt - 1) + sizeof(struct wlist))) == NULL)
            perror("realloc"), exit(EXIT_FAILURE);
         memcpy(&nl->ref[nl->ref_cnt], &nd->ref[1], (nd->ref_cnt - 1) * sizeof(int64_t));
         nl->ref_cnt += nd->ref_cnt - 1;
         nl->max_ref = nl->ref_cnt;

         // remove way from way list
         memmove(&wl->ref[i], &wl->ref[i + 1], sizeof(int64_t) * (wl->ref_cnt - 1 - i));
         wl->ref_cnt--;
         i--;
      }
      else if (nl->ref[0] == nd->ref[nd->ref_cnt - 1])
      {
         // nodes are connected to the beginning of the node list
         if ((nl = realloc(nl, sizeof(int64_t) * (nl->max_ref + nd->ref_cnt - 1) + sizeof(struct wlist))) == NULL)
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


int poly_out(FILE *f, struct wlist *nl, struct rdata *rd)
{
   bx_node_t *bn;
   //struct onode *nd;
   int i;

   fprintf(f, "<way id=\"%ld\" version=\"1\">\n", nl->id);
   for (i = 0; i < nl->ref_cnt; i++)
      fprintf(f, "   <nd ref=\"%ld\"/>\n", nl->ref[i]);
   fprintf(f, "</way>\n");

   for (i = 0; i < nl->ref_cnt; i++)
   {
      // FIXME: return code should be tested
      if ((bn = bx_get_node(rd->nodes, nl->ref[i])) == NULL)
      {
         log_warn("NULL pointer catchted in poly_out...continuing");
         continue;
      }
      //nd = bn->next[0];
      //print_onode(f, nd);
      //fprintf(f, "<node id=\"%ld\" lat=\"%f\" lon=\"%f\" version=\"1\"/>\n", nd->nd.id, nd->nd.lat, nd->nd.lon);
      print_onode(f, bn->next[0]);
   }

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
   if ((*wl = realloc(*wl, sizeof(struct wlist) + sizeof(int64_t) * ((*wl)->max_ref + n))) == NULL)
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
            wl[pd[i].wl_index]->ref[wl[pd[i].wl_index]->ref_cnt] = cp[j];
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

   if ((nd = get_object(OSM_NODE, nl->ref[n])) == NULL)
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
   int i, nl_cnt;
   struct wlist *wl, *nl[MAX_OPEN_POLY];
#ifdef OUTPUT_COASTLINE
   FILE *f;
#endif

   if ((wl = malloc(sizeof(*wl) + INIT_MAX_REF * sizeof(int64_t))) == NULL)
      perror("malloc"), exit(EXIT_FAILURE);

   wl->ref_cnt = 0;
   wl->max_ref = INIT_MAX_REF;

   traverse(rd->ways, 0, (void (*)(struct onode *, struct rdata *, void *)) gather_poly, rd, &wl);

   for (i = 0; i < wl->ref_cnt; i++)
      log_debug("open coastline %ld", wl->ref[i]);

#ifdef OUTPUT_COASTLINE
   if ((f = fopen("open_coastline.osm", "w")) == NULL)
      perror("fopen"), exit(EXIT_FAILURE);
   fprintf(f, "<?xml version='1.0' encoding='UTF-8'?>\n<osm version='0.6' generator='smrender'>\n");
#endif

   for (i = 0; (nl[i] = poly_find_adj(rd, wl)) != NULL; i++)
   {
      log_debug("connected way, ref_cnt = %d, ref[0] = %ld, ref[%d] = %ld",
            nl[i]->ref_cnt, nl[i]->ref[0], nl[i]->ref_cnt - 1, nl[i]->ref[nl[i]->ref_cnt - 1]);

#ifdef OUTPUT_COASTLINE
      poly_out(f, nl[i], rd);
#endif

      // check if wlist is closed
      if (nl[i]->ref[0] == nl[i]->ref[nl[i]->ref_cnt - 1])
      {
         (void) add_coast_way(rd, nl[i]);
         free(nl[i]);
         i--;
      }
   }
   nl_cnt = i;

   connect_open_poly(rd, nl, nl_cnt);

   for (i = 0; i < nl_cnt; i++)
   {
      if (nl[i]->ref[0] == nl[i]->ref[nl[i]->ref_cnt - 1])
      {
         log_debug("now connected way");
#ifdef OUTPUT_COASTLINE
         poly_out(f, nl[i], rd);
#endif
         add_coast_way(rd, nl[i]);
      }
      free(nl[i]);
   }

#ifdef OUTPUT_COASTLINE
   fprintf(f, "</osm>\n");
   fclose(f);
#endif

   free(wl);
   return 0;
}

