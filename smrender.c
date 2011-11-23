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


struct onode
{
   struct osm_node nd;
   int attr_cnt;
   hpx_attr_t attr[];
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
   struct osm_node *nd;

   if (d == 8)
   {
      if ((nd = nt->next[0]) != NULL)
         printf("<%s version='%d' id='%ld' lat='%f' lon='%f'/>\n", nd->type == OSM_NODE ? "node" : "way", nd->ver, nd->id, nd->lat, nd->lon);
      else
         printf("<!-- this should not happen... -->\n");
      return;
   }

   for (i = 0; i < 256; i++)
      if (nt->next[i])
         traverse_ntree(nt->next[i], d + 1);
}


int main(int argc, char *argv[])
{
   hpx_ctrl_t *ctl;
   hpx_tag_t *tag;
   bstring_t b;
   int i;
   struct osm_node *nd;
   hpx_tree_t *tlist = NULL;
   bx_node_t *ntree = NULL, *nt;
   bx_node_t *wtree = NULL, *wt;
//#define MAX_SEC 32
   //struct sector sec[MAX_SEC];

   int fd = 0, n = 0;
   struct stat st;

   if ((argc >= 2) && ((fd = open(argv[1], O_RDONLY)) == -1))
         perror("open"), exit(EXIT_FAILURE);

   if (fstat(fd, &st) == -1)
      perror("stat"), exit(EXIT_FAILURE);

   if ((ctl = hpx_init(fd, st.st_size)) == NULL)
      perror("hpx_init_simple"), exit(EXIT_FAILURE);
   if ((nd = malloc_node()) == NULL)
      perror("malloc_node"), exit(EXIT_FAILURE);

   if (hpx_tree_resize(&tlist, 0) == -1)
      perror("hpx_tree_resize"), exit(EXIT_FAILURE);
   if ((tlist->tag = hpx_tm_create(16)) == NULL)
      perror("hpx_tm_create"), exit(EXIT_FAILURE);

   tlist->nsub = 0;
   tag = tlist->tag;
   nd->type = OSM_NA;

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
            if ((tag->type == HPX_OPEN) || (tag->type == HPX_SINGLE))
            {
               //nd->type = OSM_NODE;
               memset(nd, 0, sizeof(*nd));
               nd->type = n;
               proc_osm_node(tag, nd);
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
            if ((tag->type == HPX_CLOSE) || (tag->type == HPX_SINGLE))
            {
               // do something....

               nt = bx_add_node(&ntree, nd->id);
               if ((nt->next[0] = malloc(sizeof(*nd))) == NULL)
                  perror("malloc"), exit(EXIT_FAILURE);
               memcpy(nt->next[0], nd, sizeof(*nd));

               // finally
               tlist->nsub = 0;
               tag = tlist->tag;
               nd->type = OSM_NA;
            }
            continue;
         } //if (!bs_cmp(tag->tag, "node"))

         if ((nd->type != OSM_NODE) && (nd->type != OSM_WAY))
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

   (void) close(fd);

   hpx_tm_free(tag);
   hpx_free(ctl);
   free(nd);

   traverse_ntree(ntree, 0);

   exit(EXIT_SUCCESS);
}

