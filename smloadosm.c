/* Copyright 2011 Bernhard R. Fischer, 2048R/5C5FFD47 <bf@abenteuerland.at>
 *
 * This file is part of smfilter.
 *
 * Smfilter is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3 of the License.
 *
 * Smfilter is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with smfilter. If not, see <http://www.gnu.org/licenses/>.
 */

/*! This program reads an OSM/XML file and parses, filters, and modifies it.
 *  Filter and modification rules are hardcoded.
 *
 *  @author Bernhard R. Fischer
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <math.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <gd.h>

#include "osm_inplace.h"
#include "bstring.h"
#include "libhpxml.h"
//#include "seamark.h"
#include "smlog.h"
#include "bxtree.h"
#include "smrender.h"
//#include "smrules.h"


int read_osm_file(hpx_ctrl_t *ctl, bx_node_t **ntree, bx_node_t **wtree)
{
   hpx_tag_t *tag;
   bstring_t b;
   int n = 0, i, j;
   struct osm_node nd;
   struct onode *ond;
   hpx_tree_t *tlist = NULL;
   bx_node_t *tr;
   int64_t *ref;

   if (hpx_tree_resize(&tlist, 0) == -1)
      perror("hpx_tree_resize"), exit(EXIT_FAILURE);
   if ((tlist->tag = hpx_tm_create(16)) == NULL)
      perror("hpx_tm_create"), exit(EXIT_FAILURE);

   tlist->nsub = 0;
   tag = tlist->tag;
   nd.type = OSM_NA;

   while (hpx_get_elem(ctl, &b, NULL, &tag->line) > 0)
   {
      if (!hpx_process_elem(b, tag))
      {
         oline_++;

         if (!bs_cmp(tag->tag, "node"))
            n = OSM_NODE;
         else if (!bs_cmp(tag->tag, "way"))
            n = OSM_WAY;
         else
            n = 0;

         //if (!bs_cmp(tag->tag, "node"))
         if (n)
         {
            if (tag->type == HPX_OPEN)
            {
               //nd->type = OSM_NODE;
               memset(&nd, 0, sizeof(nd));
               nd.type = n;
               proc_osm_node(tag, &nd);
               if (tlist->nsub >= tlist->msub)
               {
                  if (hpx_tree_resize(&tlist, 1) == -1)
                     perror("hpx_tree_resize"), exit(EXIT_FAILURE);
                  if (hpx_tree_resize(&tlist->subtag[tlist->nsub], 0) == -1)
                     perror("hpx_tree_resize"), exit(EXIT_FAILURE);
                  if ((tlist->subtag[tlist->nsub]->tag = hpx_tm_create(16)) == NULL)
                     perror("hpx_tm_create"), exit(EXIT_FAILURE);
               }
               tlist->subtag[tlist->nsub]->nsub = 0;
               tag = tlist->subtag[tlist->nsub]->tag;
            }
            else if (tag->type == HPX_SINGLE)
            {
               //nd->type = OSM_NODE;
               memset(&nd, 0, sizeof(nd));
               nd.type = n;
               proc_osm_node(tag, &nd);

               tr = bx_add_node(nd.type == OSM_NODE ? ntree : wtree, nd.id);
               if ((ond = malloc(sizeof(*ond))) == NULL)
                  perror("malloc"), exit(EXIT_FAILURE);
               memcpy(&ond->nd, &nd, sizeof(nd));
               memset(((char*) ond) + sizeof(nd), 0, sizeof(*ond) - sizeof(nd));
               tr->next[0] = ond;

               // finally
               tlist->nsub = 0;
               nd.type = OSM_NA;
            }
            else if (tag->type == HPX_CLOSE)
            {
               tr = bx_add_node(nd.type == OSM_NODE ? ntree : wtree, nd.id);
               if ((ond = malloc(sizeof(*ond))) == NULL)
                  perror("malloc"), exit(EXIT_FAILURE);
               memcpy(&ond->nd, &nd, sizeof(nd));
               memset(((char*) ond) + sizeof(nd), 0, sizeof(*ond) - sizeof(nd));

               // count 'nd' and 'tag' tags
               for (i = 0; i < tlist->nsub; i++)
               {
                  if (!bs_cmp(tlist->subtag[i]->tag->tag, "tag"))
                     ond->tag_cnt++;
                  else if (!bs_cmp(tlist->subtag[i]->tag->tag, "nd"))
                     ond->ref_cnt++;
               }
               
               if ((ond = realloc(ond, sizeof(*ond) + ond->tag_cnt * sizeof(struct otag))) == NULL)
                  perror("realloc"), exit(EXIT_FAILURE);

               if ((ond->ref = malloc(ond->ref_cnt * sizeof(int64_t))) == NULL)
                  perror("malloc"), exit(EXIT_FAILURE);

               for (i = 0, ref = ond->ref, j = 0; i < tlist->nsub; i++)
               {
                  if (!bs_cmp(tlist->subtag[i]->tag->tag, "tag"))
                  {
                     if (get_value("k", tlist->subtag[i]->tag, &ond->otag[j].k) == -1)
                        memset(&ond->otag[j].k, 0, sizeof(bstring_t));
                     if (get_value("v", tlist->subtag[i]->tag, &ond->otag[j].v) == -1)
                        memset(&ond->otag[j].v, 0, sizeof(bstring_t));
                     j++;
                  }
                  else if (!bs_cmp(tlist->subtag[i]->tag->tag, "nd"))
                  {
                     if (get_value("ref", tlist->subtag[i]->tag, &b) == -1)
                        *ref = -1;
                     else
                        *ref = bs_tol(b);

                     ref++;
                  }
               }

               tr->next[0] = ond;

               // finally
               tlist->nsub = 0;
               tag = tlist->tag;
               nd.type = OSM_NA;
            }
            continue;
         } //if (!bs_cmp(tag->tag, "node"))

         if ((nd.type != OSM_NODE) && (nd.type != OSM_WAY))
            continue;

         if (!bs_cmp(tag->tag, "tag") || !bs_cmp(tag->tag, "nd"))
         {
            tlist->nsub++;
            if (tlist->nsub >= tlist->msub)
            {
               if (hpx_tree_resize(&tlist, 1) == -1)
                  perror("hpx_tree_resize"), exit(EXIT_FAILURE);
               if (hpx_tree_resize(&tlist->subtag[tlist->nsub], 0) == -1)
                  perror("hpx_tree_resize"), exit(EXIT_FAILURE);
               if ((tlist->subtag[tlist->nsub]->tag = hpx_tm_create(16)) == NULL)
                  perror("hpx_tm_create"), exit(EXIT_FAILURE);
            }
            tlist->subtag[tlist->nsub]->nsub = 0;
            tag = tlist->subtag[tlist->nsub]->tag;
         }
      }
   }

   hpx_tm_free(tag);

   return 0;
}

