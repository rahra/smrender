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

#include "osm_inplace.h"
#include "bstring.h"
#include "libhpxml.h"
//#include "seamark.h"
#include "smlog.h"
#include "bxtree.h"


struct otag
{
   bstring_t k;
   bstring_t v;
};

struct onode
{
   struct osm_node nd;
   int ref_cnt;
   int64_t *ref;
   int tag_cnt;
   struct otag otag[];
};

int oline_ = 0;


int match_node(const hpx_tree_t *t, bstring_t *b)
{
   int i, j;

   for (i = 0; i < t->nsub; i++)
   {
      for (j = 0; j < t->subtag[i]->tag->nattr; j++)
      {
         if (!bs_cmp(t->subtag[i]->tag->attr[j].name, "k"))
         {
            if (!bs_cmp(t->subtag[i]->tag->attr[j].value, "seamark:type"))
            {
               get_v(t->subtag[i]->tag, b);
               return 1;
            }
         }
      }
   }
   return 0;
}


void usage(const char *s)
{
   printf("Seamark renderer V1.0, (c) 2011, Bernhard R. Fischer, <bf@abenteuerland.at>.\n\n");
}


void traverse_ntree(const bx_node_t *nt, int d)
{
   int i;
   struct onode *nd;

   if (d == 8)
   {
      if ((nd = nt->next[0]) != NULL)
      {
         switch (nd->nd.type)
         {
            case OSM_NODE:
               printf("<node version='%d' id='%ld' lat='%f' lon='%f'",  nd->nd.ver, nd->nd.id, nd->nd.lat, nd->nd.lon);
               if (!nd->tag_cnt)
                  printf("/>\n");
               else
               {
                  printf(">\n");
                  for (i = 0; i < nd->tag_cnt; i++)
                     printf(" <tag k='%.*s' v='%.*s'/>\n",
                           nd->otag[i].k.len, nd->otag[i].k.buf, nd->otag[i].v.len, nd->otag[i].v.buf);
                  printf("</node>\n");
               }

               break;

            case OSM_WAY:
               printf("<way version='%d' id='%ld'",  nd->nd.ver, nd->nd.id);

               if (!nd->tag_cnt && !nd->ref_cnt)
                  printf("/>\n");
               else
               {
                  printf(">\n");
                  for (i = 0; i < nd->tag_cnt; i++)
                     printf(" <tag k='%.*s' v='%.*s'/>\n",
                           nd->otag[i].k.len, nd->otag[i].k.buf, nd->otag[i].v.len, nd->otag[i].v.buf);
                  for (i = 0; i < nd->ref_cnt; i++)
                     printf(" <nd ref='%ld'/>\n", nd->ref[i]);
 
                  printf("</way>\n");
               }

               break;

            default:
               printf("<!-- node type %d unknown -->\n", nd->nd.type);
               return;
         }
      }
      else
         printf("<!-- this should not happen: NULL pointer catched -->\n");

      return;
   }

   for (i = 0; i < 256; i++)
      if (nt->next[i])
         traverse_ntree(nt->next[i], d + 1);
}


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
//         hpx_fprintf_tag(stdout, tag);
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


int main(int argc, char *argv[])
{
   hpx_ctrl_t *ctl;
   bx_node_t *ntree = NULL;
   bx_node_t *wtree = NULL;
   int fd = 0;
   struct stat st;

   if ((argc >= 2) && ((fd = open(argv[1], O_RDONLY)) == -1))
         perror("open"), exit(EXIT_FAILURE);

   if (fstat(fd, &st) == -1)
      perror("stat"), exit(EXIT_FAILURE);

   if ((ctl = hpx_init(fd, st.st_size)) == NULL)
      perror("hpx_init_simple"), exit(EXIT_FAILURE);

   (void) read_osm_file(ctl, &ntree, &wtree);

   (void) close(fd);

   traverse_ntree(ntree, 0);
   traverse_ntree(wtree, 0);

   hpx_free(ctl);

   exit(EXIT_SUCCESS);
}

