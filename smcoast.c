#include <unistd.h>
#include <string.h>
#include <stdio.h>

#include "smrender.h"
#include "smath.h"


// initial number of ref array
#define INIT_MAX_REF 20


struct wlist
{
   int64_t id;
   int ref_cnt, max_ref;
   int64_t ref[];
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
      fprintf(f, "<node id=\"%ld\" lat=\"%f\" lon=\"%f\" version=\"1\"/>\n", nd->nd.id, nd->nd.lat, nd->nd.lon);
   }

   return 0;
}


int cat_poly(struct rdata *rd)
{
   int i;
   struct wlist *wl, *nl;
   FILE *f;

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

   while ((nl = poly_find_adj(rd, wl)) != NULL)
   {
      fprintf(stderr, "connected way, ref_cnt = %d\n", nl->ref_cnt);
      poly_node_to_border(rd, nl);
      poly_out(f, nl, rd);
      free(nl);
   }
   
   fprintf(f, "</osm>\n");
   fclose(f);

   free(wl);
   return 0;
}

