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

/*! This program reads an OSM/XML file and parses it into an object tree.
 * Originally it was written for smfilter and was reused and adapted.
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
#include <signal.h>
//#include <gd.h>

#include "smrender.h"
#include "osm_inplace.h"
#include "bstring.h"
#include "libhpxml.h"
#include "smlog.h"
#include "bxtree.h"


static size_t oline_ = 0;
static size_t mem_usage_ = 0;
static volatile sig_atomic_t usr1_ = 0;


size_t onode_mem(void)
{
   return mem_usage_;
}


void osm_read_exit(void)
{
   static short ae = 0;

   switch (ae)
   {
      case 0:
         if (atexit(osm_read_exit))
            log_msg(LOG_ERR, "atexit(osm_read_exit) failed");
         break;

      default:
         log_msg(LOG_INFO, "onode_memory: %ld kByte, line %ld", onode_mem() / 1024, oline_);
   }
   ae++;
}


void usr1_handler(int sig)
{
   usr1_++;
}


void install_sigusr1(void)
{
   struct sigaction sa;

   memset(&sa, 0, sizeof(sa));
   sa.sa_handler = usr1_handler;

   if (sigaction(SIGUSR1, &sa, NULL) == -1)
      log_msg(LOG_WARNING, "SIGUSR1 handler cannot be installed: %s", strerror(errno));
   else
      log_msg(LOG_INFO, "SIGUSR1 installed (pid = %ld)", (long) getpid());
}


int read_osm_file(hpx_ctrl_t *ctl, bx_node_t **tree, struct filter *fi)
{
   hpx_tag_t *tag;
   bstring_t b;
   int n = 0, i, j, rcnt;
   struct osm_node nd;
   struct onode *ond;
   hpx_tree_t *tlist = NULL;
   bx_node_t *tr;
   int64_t *ref;
   int64_t nid = INT64_MIN + 1;

   if (hpx_tree_resize(&tlist, 0) == -1)
      perror("hpx_tree_resize"), exit(EXIT_FAILURE);
   if ((tlist->tag = hpx_tm_create(16)) == NULL)
      perror("hpx_tm_create"), exit(EXIT_FAILURE);

   //oline_ = 0;
   tlist->nsub = 0;
   tag = tlist->tag;
   nd.type = OSM_NA;

   while (hpx_get_elem(ctl, &b, NULL, &tag->line) > 0)
   {
      oline_ = tag->line;
      if (usr1_)
      {
         usr1_ = 0;
         log_msg(LOG_INFO, "onode_memory: %ld kByte, line %ld", onode_mem() / 1024, tag->line);
      }

      if (!hpx_process_elem(b, tag))
      {
         //oline_++;

         if (!bs_cmp(tag->tag, "node"))
            n = OSM_NODE;
         else if (!bs_cmp(tag->tag, "way"))
            n = OSM_WAY;
         else
            n = 0;

         if (n)
         {
            if (tag->type == HPX_OPEN)
            {
               memset(&nd, 0, sizeof(nd));
               proc_osm_node(tag, &nd);
#ifdef READ_FILTER
               if ((fi != NULL) && (n == OSM_NODE))
               {
                  // skip nodes which are outside of bounding box
                  if (fi->use_bbox && ((nd.lat > fi->c1.lat) || (nd.lat < fi->c2.lat) || (nd.lon > fi->c2.lon) || (nd.lon < fi->c1.lon)))
                  {
                     //log_debug("skipping node line %ld", oline_);
                     continue;
                  }
               }
#endif
               nd.type = n;
               if (!nd.id) nd.id = nid++;

               if (tlist->nsub >= tlist->msub)
               {
                  if (hpx_tree_resize(&tlist, 1) == -1)
                     log_msg(LOG_ERR, "hpx_tree_resize failed at line %ld", tag->line),
                     exit(EXIT_FAILURE);
                  if (hpx_tree_resize(&tlist->subtag[tlist->nsub], 0) == -1)
                     log_msg(LOG_ERR, "hpx_tree_resize failed at line %ld", tag->line),
                     exit(EXIT_FAILURE);
                  if ((tlist->subtag[tlist->nsub]->tag = hpx_tm_create(16)) == NULL)
                     log_msg(LOG_ERR, "hpx_tm_create failed at line %ld", tag->line),
                     exit(EXIT_FAILURE);
               }
               tlist->subtag[tlist->nsub]->nsub = 0;
               tag = tlist->subtag[tlist->nsub]->tag;
            }
            else if (tag->type == HPX_SINGLE)
            {
               memset(&nd, 0, sizeof(nd));
               proc_osm_node(tag, &nd);
#ifdef READ_FILTER
               if ((fi != NULL) && (n == OSM_NODE))
               {
                  // skip nodes which are outside of bounding box
                  if (fi->use_bbox && ((nd.lat > fi->c1.lat) || (nd.lat < fi->c2.lat) || (nd.lon > fi->c2.lon) || (nd.lon < fi->c1.lon)))
                  {
                     //log_debug("skipping node line %ld", oline_);
                     continue;
                  }
               }
#endif
               nd.type = n;
               if (!nd.id) nd.id = nid++;

               if ((ond = malloc(sizeof(*ond))) == NULL)
                  log_msg(LOG_ERR, "failed to alloc struct onode at line %ld: %s",
                        strerror(errno), tag->line),
                  exit(EXIT_FAILURE);
#ifdef MEM_USAGE
               mem_usage_ += sizeof(*ond);
#endif
               memcpy(&ond->nd, &nd, sizeof(nd));
               memset(((char*) ond) + sizeof(nd), 0, sizeof(*ond) - sizeof(nd));
               tr = bx_add_node(tree, nd.id);
               if (tr->next[nd.type == OSM_WAY] != NULL)
               {
                  free(tr->next[nd.type == OSM_WAY]);
                  // too much debugging if there are many duplicates
                  //log_msg(LOG_ERR, "object %ld already exists, overwriting.", nd.id);
               }
               tr->next[nd.type == OSM_WAY] = ond;

               // finally
               tlist->nsub = 0;
               nd.type = OSM_NA;
            }
            else if (tag->type == HPX_CLOSE)
            {
               if ((nd.type != OSM_NODE) && (nd.type != OSM_WAY))
                  continue;

               if ((ond = malloc(sizeof(*ond))) == NULL)
                  log_msg(LOG_ERR, "failed to alloc struct onode at line %ld: %s",
                        strerror(errno), tag->line),
                  exit(EXIT_FAILURE);
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
                  log_msg(LOG_ERR, "failed to realloc tags for struct onode at line %ld: %s",
                        strerror(errno), tag->line),
                  exit(EXIT_FAILURE);

               if ((ond->ref = malloc(ond->ref_cnt * sizeof(int64_t))) == NULL)
                  log_msg(LOG_ERR, "failed to alloc refs for struct onode at line %ld: %s",
                        strerror(errno), tag->line),
                  exit(EXIT_FAILURE);

#ifdef MEM_USAGE
               mem_usage_ += sizeof(*ond) + ond->ref_cnt * sizeof(int64_t) + ond->tag_cnt * sizeof(struct otag);
#endif
 
               for (i = 0, ref = ond->ref, j = 0, rcnt = 0; i < tlist->nsub; i++)
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
                     if (get_value("ref", tlist->subtag[i]->tag, &b) != -1)
                     {
                        *ref = bs_tol(b);
#ifdef READ_FILTER
                        if (get_object(OSM_NODE, *ref) == NULL)
                           continue;
#endif
                        ref++;
                        rcnt++;
                     }
                  }
               }

#ifdef READ_FILTER
               if ((fi != NULL) && (nd.type == OSM_WAY) && (rcnt == 0))
               {
                  //log_debug("freeing empty way");
#ifdef MEM_USAGE
                  mem_usage_ -= sizeof(*ond) + ond->ref_cnt * sizeof(int64_t) + ond->tag_cnt * sizeof(struct otag);
#endif
                  free(ond->ref);
                  free(ond);
               }
               else
#endif
               {
                  ond->ref_cnt = rcnt;
                  tr = bx_add_node(tree, nd.id);
                  if (tr->next[nd.type == OSM_WAY] != NULL)
                  {
                     free(tr->next[nd.type == OSM_WAY]);
                     // too much debugging if there are many duplicates
                     //log_msg(LOG_ERR, "object %ld already exists, overwriting.", nd.id);
                  }
                  tr->next[nd.type == OSM_WAY] = ond;
               }

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

