#include <unistd.h>
#include <string.h>
#include <stdio.h>

#include "smrender.h"
#include "smath.h"
#include "bxtree.h"


// initial number of ref array
#define INIT_MAX_REF 20
#define MAX_OPEN_POLY 32


struct wlist
{
   int64_t id;
   int ref_cnt, max_ref;
   struct pcoord start, end;
   int64_t ref[];
};

struct pdef
{
   struct wlist *wl;
   int pn;           // index number of destined point
   struct pcoord pc; // bearing to pointer
};


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
   bx_node_t *bn;
   struct wlist *nl;
   struct onode *nd;

   if (!wl->ref_cnt)
      return NULL;

   if ((bn = bx_get_node(rd->ways, wl->ref[0])) == NULL)
      return NULL;

   if ((nd = bn->next[0]) == NULL)
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
      if ((bn = bx_get_node(rd->ways, wl->ref[i])) == NULL)
         return NULL;

      if ((nd = bn->next[0]) == NULL)
         return NULL;

      if (nd->ref_cnt < 2)
      {
         fprintf(stderr, "way id = %ld, ref_cnt = %d. Ignoring.\n", nd->nd.id, nd->ref_cnt);
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
   struct onode *nd;
   int i;

   fprintf(f, "<way id=\"%ld\" version=\"1\">\n", nl->id);
   for (i = 0; i < nl->ref_cnt; i++)
      fprintf(f, "   <nd ref=\"%ld\"/>\n", nl->ref[i]);
   fprintf(f, "</way>\n");

   for (i = 0; i < nl->ref_cnt; i++)
   {
      // FIXME: return code should be tested
      bn = bx_get_node(rd->nodes, nl->ref[i]);
      nd = bn->next[0];
      print_onode(f, nd);
      //fprintf(f, "<node id=\"%ld\" lat=\"%f\" lon=\"%f\" version=\"1\"/>\n", nd->nd.id, nd->nd.lat, nd->nd.lon);
   }

   return 0;
}


int poly_bearing(struct rdata *rd, struct wlist *nl, int n, struct pcoord *pc, const struct coord *c)
{
   bx_node_t *bn;
   struct onode *nd;
   struct coord dst;

   if (nl->ref_cnt < n) return -1;
   if ((bn = bx_get_node(rd->nodes, nl->ref[n])) == NULL) return -1;
   if ((nd = bn->next[0]) == NULL) return -1;

   dst.lat = nd->nd.lat;
   dst.lon = nd->nd.lon;

   *pc = coord_diff(c, &dst);
   return 0;
}


int poly_ends(struct rdata *rd, struct wlist *nl, const struct coord *c)
{
   bx_node_t *bn;
   struct onode *nd;
   struct coord dst;

   if (nl->ref_cnt < 2) return -1;
   if ((bn = bx_get_node(rd->nodes, nl->ref[0])) == NULL) return -1;
   if ((nd = bn->next[0]) == NULL) return -1;

   dst.lat = nd->nd.lat;
   dst.lon = nd->nd.lon;
   nl->start = coord_diff(c, &dst);

   if ((bn = bx_get_node(rd->nodes, nl->ref[nl->ref_cnt - 1])) == NULL) return -1;
   if ((nd = bn->next[0]) == NULL) return -1;

   dst.lat = nd->nd.lat;
   dst.lon = nd->nd.lon;
   nl->end = coord_diff(c, &dst);

   fprintf(stderr, "start/end angle: %f/%f\n", nl->start.bearing, nl->end.bearing);
   return 0;
}


int cat_poly(struct rdata *rd)
{
   int i, nl_cnt;
   struct wlist *wl, *nl[MAX_OPEN_POLY];
   FILE *f;
   struct coord center;
   struct onode *ond;
   bx_node_t *bn;

   if ((wl = malloc(sizeof(*wl) + INIT_MAX_REF * sizeof(int64_t))) == NULL)
      perror("malloc"), exit(EXIT_FAILURE);

   wl->ref_cnt = 0;
   wl->max_ref = INIT_MAX_REF;

   traverse(rd->ways, 0, (void (*)(struct onode *, struct rdata *, void *)) gather_poly, rd, &wl);

   fprintf(stderr, "open coastline: \n");
   for (i = 0; i < wl->ref_cnt; i++)
      fprintf(stderr, "%ld,", wl->ref[i]);
   fprintf(stderr, "\n");

   if ((f = fopen("open_coastline.osm", "w")) == NULL)
      perror("fopen"), exit(EXIT_FAILURE);
   fprintf(f, "<?xml version='1.0' encoding='UTF-8'?>\n<osm version='0.6' generator='smrender'>\n");

   center.lat = rd->mean_lat;
   center.lon = (rd->x1c + rd->x2c) / 2;
   i = 0;
   while ((nl[i] = poly_find_adj(rd, wl)) != NULL)
   {
      fprintf(stderr, "connected way, ref_cnt = %d, ref[0] = %ld, ref[%d] = %ld\n",
            nl[i]->ref_cnt, nl[i]->ref[0], nl[i]->ref_cnt - 1, nl[i]->ref[nl[i]->ref_cnt - 1]);
      //poly_node_to_border(rd, nl);
      poly_out(f, nl[i], rd);

      // check if wlist is closed
      if (nl[i]->ref[0] == nl[i]->ref[nl[i]->ref_cnt - 1])
      {
         if ((ond = malloc(sizeof(struct onode) + sizeof(struct otag))) == NULL)
            perror("malloc"), exit(EXIT_FAILURE);
         memset(ond, 0, sizeof(struct onode) + sizeof(struct otag));
         if ((ond->ref = malloc(nl[i]->ref_cnt * sizeof(int64_t))) == NULL)
            perror("malloc"), exit(EXIT_FAILURE);
         ond->ref_cnt = nl[i]->ref_cnt;
         memcpy(ond->ref, nl[i]->ref, nl[i]->ref_cnt * sizeof(int64_t));

         rd->ds.min_wid = rd->ds.min_wid < 0 ? rd->ds.min_wid - 1 : -1;
         ond->nd.id = rd->ds.min_wid;
         ond->tag_cnt = 1;
         ond->nd.type = OSM_WAY;
         ond->nd.ver = 1;

         ond->otag[0].k.buf = "natural";
         ond->otag[0].k.len = 7;
         ond->otag[0].v.buf = "coastline";
         ond->otag[0].v.len = 9;

         bn = bx_add_node(&rd->ways, ond->nd.id);
         bn->next[0] = ond;

         free(nl[i]);
         i--;
      }
      else if (poly_ends(rd, nl[i], &center) == -1)
         fprintf(stderr, "*** error in poly_ends()\n");

      i++;
   }
   nl_cnt = i;

   for (i = 0; i < nl_cnt; i++)
   {
     free(nl[i]);
   }

   fprintf(f, "</osm>\n");
   fclose(f);

   free(wl);
   return 0;
}

