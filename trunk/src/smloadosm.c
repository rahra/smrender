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
#include <limits.h>
//#include <gd.h>

#include "smrender.h"
#include "osm_inplace.h"
#include "bstring.h"
#include "libhpxml.h"
#include "smlog.h"
#include "bxtree.h"


static size_t oline_ = 0;
static volatile sig_atomic_t usr1_ = 0;


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
         log_msg(LOG_INFO, "onode_memory: %ld kByte, oline %ld", onode_mem() / 1024, hpx_lineno());
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


void assign_o(osm_obj_t *dst, const osm_obj_t *src)
{
   dst->vis = src->vis;
   dst->id = src->id;
   dst->ver = src->ver;
   dst->cs = src->cs;
   dst->uid = src->uid;
   dst->tim = src->tim;

   if ((src->type == dst->type) && (src->type == OSM_NODE))
   {
      ((osm_node_t*) dst)->lat = ((osm_node_t*) src)->lat;
      ((osm_node_t*) dst)->lon = ((osm_node_t*) src)->lon;
   }
}


void clear_ostor(osm_storage_t *o)
{
   memset(o, 0, sizeof(*o));
}


int read_osm_file(hpx_ctrl_t *ctl, bx_node_t **tree, struct filter *fi)
{
   hpx_tag_t *tag;
   bstring_t b;
   int t = 0, e, i, j, rcnt;
   osm_storage_t o;
   osm_obj_t *obj;
   //struct onode *ond;
   hpx_tree_t *tlist = NULL;
   bx_node_t *tr;
   int64_t *ref;
   int64_t nid = INT64_MIN + 1;
   time_t tim;

   if (hpx_tree_resize(&tlist, 0) == -1)
      perror("hpx_tree_resize"), exit(EXIT_FAILURE);
   if ((tlist->tag = hpx_tm_create(16)) == NULL)
      perror("hpx_tm_create"), exit(EXIT_FAILURE);

   //oline_ = 0;
   tim = time(NULL);
   tlist->nsub = 0;
   tag = tlist->tag;
   clear_ostor(&o);

   while ((e = hpx_get_elem(ctl, &b, NULL, &tag->line)) > 0)
   {
      oline_ = tag->line;
      if (usr1_)
      {
         usr1_ = 0;
         log_msg(LOG_INFO, "onode_memory: %ld kByte, line %ld, %.2f MByte/s", onode_mem() / 1024, tag->line, ((double) ctl->pos / (double) (time(NULL) - tim)) / (double) (1024 * 1024));
         log_msg(LOG_INFO, "ctl->pos = %ld, ctl->len = %ld, ctl->buf.len = %ld", ctl->pos, ctl->len, ctl->buf.len);
      }

      if (!hpx_process_elem(b, tag))
      {
         //oline_++;

         if (!bs_cmp(tag->tag, "node"))
            t = OSM_NODE;
         else if (!bs_cmp(tag->tag, "way"))
            t = OSM_WAY;
         else
            t = 0;

         if (t)
         {
            if (tag->type == HPX_OPEN)
            {
               memset(&o, 0, sizeof(o));
               proc_osm_node(tag, (osm_obj_t*) &o);
#ifdef READ_FILTER
               if ((fi != NULL) && (t == OSM_NODE))
               {
                  // skip nodes which are outside of bounding box
                  if (fi->use_bbox && ((o.n.lat > fi->c1.lat) || (o.n.lat < fi->c2.lat) || (o.n.lon > fi->c2.lon) || (o.n.lon < fi->c1.lon)))
                  {
                     //log_debug("skipping node line %ld", oline_);
                     continue;
                  }
               }
#endif
               o.o.type = t;
               if (!o.o.id) o.o.id = nid++;

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
               memset(&o, 0, sizeof(o));
               proc_osm_node(tag, (osm_obj_t*) &o);
#ifdef READ_FILTER
               if ((fi != NULL) && (t == OSM_NODE))
               {
                  // skip nodes which are outside of bounding box
                  if (fi->use_bbox && ((o.n.lat > fi->c1.lat) || (o.n.lat < fi->c2.lat) || (o.n.lon > fi->c2.lon) || (o.n.lon < fi->c1.lon)))
                  {
                     //log_debug("skipping node line %ld", oline_);
                     continue;
                  }
               }
#endif
               o.o.type = t;
               if (!o.o.id) o.o.id = nid++;

               switch (o.o.type)
               {
                  case OSM_NODE:
                     obj = (osm_obj_t*) malloc_node(0);
                     break;

                  case OSM_WAY:
                     obj = (osm_obj_t*) malloc_way(0, 0);
                     break;
                     
                  default:
                     log_msg(LOG_ERR, "type %d no implemented yet", o.o.type);
                     clear_ostor(&o);
                     continue;

               }
               assign_o(obj, (osm_obj_t*) &o);

               tr = bx_add_node(tree, o.o.id);
               if (tr->next[o.o.type - 1] != NULL)
               {
                  free_obj(tr->next[o.o.type - 1]);
                  // too much debugging if there are many duplicates
                  //log_msg(LOG_ERR, "object %ld already exists, overwriting.", nd.id);
               }
               tr->next[o.o.type - 1] = obj;

               // finally
               tlist->nsub = 0;
               clear_ostor(&o);
            }
            else if (tag->type == HPX_CLOSE)
            {
               if ((o.o.type != OSM_NODE) && (o.o.type != OSM_WAY))
                  continue;

               // count 'nd' and 'tag' tags
               for (i = 0; i < tlist->nsub; i++)
               {
                  if (!bs_cmp(tlist->subtag[i]->tag->tag, "tag"))
                     o.o.tag_cnt++;
                  else if (!bs_cmp(tlist->subtag[i]->tag->tag, "nd"))
                     o.w.ref_cnt++;
               }

               switch (o.o.type)
               {
                  case OSM_NODE:
                     obj = (osm_obj_t*) malloc_node(o.o.tag_cnt);
                     break;

                  case OSM_WAY:
                     obj = (osm_obj_t*) malloc_way(o.o.tag_cnt, o.w.ref_cnt);
                     break;
                     
                  default:
                     log_msg(LOG_ERR, "type %d no implemented yet", o.o.type);
                     clear_ostor(&o);
                     continue;

               }

               assign_o(obj, (osm_obj_t*) &o);

               for (i = 0, ref = ((osm_way_t*) obj)->ref, j = 0, rcnt = 0; i < tlist->nsub; i++)
               {
                  if (!bs_cmp(tlist->subtag[i]->tag->tag, "tag"))
                  {
                     if (get_value("k", tlist->subtag[i]->tag, &obj->otag[j].k) == -1)
                        memset(&obj->otag[j].k, 0, sizeof(bstring_t));
                     if (get_value("v", tlist->subtag[i]->tag, &obj->otag[j].v) == -1)
                        memset(&obj->otag[j].v, 0, sizeof(bstring_t));
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
               if ((fi != NULL) && (o.o.type == OSM_WAY) && (rcnt == 0))
               {
                  free_obj(obj);
               }
               else
#endif
               {
                  if (o.o.type == OSM_WAY)
                     ((osm_way_t*) obj)->ref_cnt = rcnt;
                  tr = bx_add_node(tree, o.o.id);
                  if (tr->next[o.o.type - 1] != NULL)
                  {
                     free_obj(tr->next[o.o.type - 1]);
                     // too much debugging if there are many duplicates
                     //log_msg(LOG_ERR, "object %ld already exists, overwriting.", nd.id);
                  }
                  tr->next[o.o.type - 1] = obj;
               }

               // finally
               tlist->nsub = 0;
               tag = tlist->tag;
               clear_ostor(&o);
            }
            continue;
         } //if (!bs_cmp(tag->tag, "node"))

         if ((o.o.type != OSM_NODE) && (o.o.type != OSM_WAY))
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
   } //while ((e = hpx_get_elem(ctl, &b, NULL, &tag->line)) > 0)

   if (e == -1)
      log_msg(LOG_ERR, "hpx_get_elem() failed: %s", strerror(errno));

   log_msg(LOG_INFO, "onode_memory: %ld kByte, line %ld, %.2f MByte/s", onode_mem() / 1024, tag->line, ((double) ctl->len / (double) (time(NULL) - tim)) / (double) (1024 * 1024));
 
   hpx_tm_free(tag);

   return 0;
}

