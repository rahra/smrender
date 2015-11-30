/* Copyright 2011-2014 Bernhard R. Fischer, 2048R/5C5FFD47 <bf@abenteuerland.at>
 *
 * This file is part of Smrender.
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
 * along with Smrender. If not, see <http://www.gnu.org/licenses/>.
 */

/*! This program reads an OSM/XML file and parses it into an object tree.
 * Originally it was written for smfilter and was reused and adapted.
 *
 *  @author Bernhard R. Fischer
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <math.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <limits.h>
#include <dirent.h>
#include <regex.h>

#include "smrender_dev.h"
#include "smloadosm.h"
#include "libhpxml.h"


static size_t oline_ = 0;
static volatile sig_atomic_t usr1_ = 0;


static int proc_osm_node(const hpx_tag_t *tag, osm_obj_t *o)
{
   int i;

   if (!bs_cmp(tag->tag, "node"))
      o->type = OSM_NODE;
   else if (!bs_cmp(tag->tag, "way"))
      o->type = OSM_WAY;
   else if (!bs_cmp(tag->tag, "relation"))
      o->type = OSM_REL;
   else 
      return -1;

   // set visibile by default to 1
   o->vis = 1;

   for (i = 0; i < tag->nattr; i++)
   {
      if (o->type == OSM_NODE)
      {
         if (!bs_cmp(tag->attr[i].name, "lat"))
            ((osm_node_t*) o)->lat = bs_tod(tag->attr[i].value);
         else if (!bs_cmp(tag->attr[i].name, "lon"))
            ((osm_node_t*) o)->lon = bs_tod(tag->attr[i].value);
      }

      if (!bs_cmp(tag->attr[i].name, "id"))
         o->id = bs_tol(tag->attr[i].value);
      else if (!bs_cmp(tag->attr[i].name, "version"))
         o->ver = bs_tol(tag->attr[i].value);
      else if (!bs_cmp(tag->attr[i].name, "changeset"))
         o->cs = bs_tol(tag->attr[i].value);
      else if (!bs_cmp(tag->attr[i].name, "uid"))
         o->uid = bs_tol(tag->attr[i].value);
      else if (!bs_cmp(tag->attr[i].name, "timestamp"))
         o->tim = parse_time(tag->attr[i].value);
      else if (!bs_cmp(tag->attr[i].name, "visible") && !bs_cmp(tag->attr[i].value, "false"))
            o->vis = 0;
   }

   if (!o->ver)
      o->ver = 1;
   if (!o->tim)
      o->tim = time(NULL);

   return tag->type;
}


static int get_value(const char *k, hpx_tag_t *tag, bstring_t *b)
{
   int i;

   for (i = 0; i < tag->nattr; i++)
      if (!bs_cmp(tag->attr[i].name, k))
      {
         *b = tag->attr[i].value;
         return 0;
      }

   return -1;
}


static void init_stats(struct dstats *ds)
{
   memset(ds, 0, sizeof(*ds));
   for (int i = 0; i < 4; i++)
   {
      ds->min_id[i] = INT64_MAX;
      ds->max_id[i] = INT64_MIN;
   }
   ds->bb.ll.lat = 90;
   ds->bb.ru.lat = -90;
   ds->bb.ll.lon = 180;
   ds->bb.ru.lon = -180;
   ds->lo_addr = (void*) (-1L);
   //ds->hi_addr = 0;
}

 
static void log_stats(const struct dstats *ds)
{
   for (int i = 1; i < 4; i++)
      log_debug("[%d] cnt = %ld, min_id = %ld, max_id = %ld (%d bits)",
            i, ds->cnt[i], ds->min_id[i], ds->max_id[i], ds->id_bits[i]);
   log_debug(" left lower %.3f,%.3f right bottom %.3f,%.3f", ds->bb.ll.lon, ds->bb.ll.lat, ds->bb.ru.lon, ds->bb.ru.lat);
   log_debug(" lo_addr = %p, hi_addr = %p", ds->lo_addr, ds->hi_addr);
}


static void update_node_stats(const osm_node_t *n, struct dstats *ds)
{
   if (ds->bb.ru.lat < n->lat) ds->bb.ru.lat = n->lat;
   if (ds->bb.ll.lon > n->lon) ds->bb.ll.lon = n->lon;
   if (ds->bb.ll.lat > n->lat) ds->bb.ll.lat = n->lat;
   if (ds->bb.ru.lon < n->lon) ds->bb.ru.lon = n->lon;
}


static int update_stats(const osm_obj_t *o, struct dstats *ds)
{
   int i;

   switch (o->type)
   {
      case OSM_NODE:
         update_node_stats((osm_node_t*) o, ds);
      case OSM_WAY:
      case OSM_REL:
         ds->cnt[o->type]++;
         if (ds->min_id[o->type] > o->id) ds->min_id[o->type] = o->id;
         if (ds->max_id[o->type] < o->id) ds->max_id[o->type] = o->id;
         break;

      default:
         log_msg(LOG_ERR, "unknown type %d", o->type);
         return -1;
   }
   if ((void*) o > ds->hi_addr)
      ds->hi_addr = o;
   if ((void*) o < ds->lo_addr)
      ds->lo_addr = o;

   // look if version is registered
   for (i = 0; i < ds->ver_cnt; i++)
   {
      if (ds->ver[i] == o->ver)
         break;
   }
   //version was not found and array has enough entries
   if ((i >= ds->ver_cnt) && (ds->ver_cnt < MAX_ITER))
   {
      ds->ver[ds->ver_cnt] = o->ver;
      ds->ver_cnt++;
   }

   return 0;
}


/*! Determine the number of bits used by n.
 */
static int bits(int64_t n)
{
   int i;

   if (n < 0) n *= -1;
   for (i = 0; n > (int64_t) 1 << i; i++);
   return i;
}


static void fin_stats(struct dstats *ds)
{
   for (int i = 1; i < 4; i++)
   {
      ds->id_bits[i] = bits(ds->max_id[i]);
      ds->id_mask[i] = ((int64_t) 1 << ds->id_bits[i]) - 1;
   }
}


void osm_read_exit(void)
{
   static short ae = 0;

   switch (ae)
   {
      case 0:
#ifdef USE_ATEXIT
         if (atexit(osm_read_exit))
            log_msg(LOG_ERR, "atexit(osm_read_exit) failed");
#endif
         break;

      default:
         log_msg(LOG_DEBUG, "onode_memory: %ld kByte, onode free: %ld kByte, leak = %ld, oline %ld",
               (long) onode_mem() / 1024, (long) onode_freed() / 1024, (long) (onode_mem() - onode_freed()), hpx_lineno());
   }
   ae++;
}


static void usr1_handler(int UNUSED(sig))
{
   usr1_++;
}


void __attribute__((constructor)) install_sigusr1(void)
{
   struct sigaction sa;
   //static int sig_inst = 0;

   //if (sig_inst)
   //   return;

   //sig_inst++;
   memset(&sa, 0, sizeof(sa));
   sa.sa_handler = usr1_handler;

   if (sigaction(SIGUSR1, &sa, NULL) == -1)
      log_msg(LOG_WARNING, "SIGUSR1 handler cannot be installed: %s", strerror(errno));
   else
      log_debug("SIGUSR1 installed (pid = %ld)", (long) getpid());
}


static void assign_o(osm_obj_t *dst, const osm_obj_t *src)
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


static void clear_ostor(osm_storage_t *o)
{
   memset(o, 0, sizeof(*o));
}


int read_osm_obj(hpx_ctrl_t *ctl, hpx_tree_t **tlistptr, osm_obj_t **obj)
{
   bstring_t b;
   int t = 0, e, i, j, rcnt, mcnt;
   osm_storage_t o;
   hpx_tag_t *tag;
   int64_t *ref;
   static int64_t nid = MIN_ID + 1;
   struct rmember *mem;
   // FIXME: this is temporary
   hpx_tree_t *tlist = *tlistptr;

   clear_ostor(&o);
   *obj = NULL;
   tlist->nsub = 0;
   tag = tlist->tag;

   while ((e = hpx_get_elem(ctl, &b, NULL, &tag->line)) > 0)
   {
      oline_ = tag->line;

      if (!hpx_process_elem(b, tag))
      {
         if (!bs_cmp(tag->tag, "node"))
            t = OSM_NODE;
         else if (!bs_cmp(tag->tag, "way"))
            t = OSM_WAY;
         else if (!bs_cmp(tag->tag, "relation"))
            t = OSM_REL;
         else
            t = 0;

         if (t)
         {
            if (tag->type == HPX_OPEN)
            {
               clear_ostor(&o);
               proc_osm_node(tag, (osm_obj_t*) &o);
               o.o.type = t;
               if (!o.o.id) o.o.id = nid++;
               //if (o.o.id <= 0) o.o.id = get_osm_id(&o.o);

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
               clear_ostor(&o);
               proc_osm_node(tag, (osm_obj_t*) &o);
               o.o.type = t;
               if (!o.o.id) o.o.id = nid++;
               //if (o.o.id <= 0) o.o.id = get_osm_id(&o.o);

               switch (o.o.type)
               {
                  case OSM_NODE:
                     *obj = (osm_obj_t*) malloc_node(0);
                     break;

                  case OSM_WAY:
                     log_msg(LOG_WARN, "single <way/>?");
                     *obj = (osm_obj_t*) malloc_way(0, 0);
                     break;

                  case OSM_REL:
                     log_msg(LOG_WARN, "single <relation/>?");
                     *obj = (osm_obj_t*) malloc_rel(0, 0);
                     break;
                     
                  default:
                     log_msg(LOG_ERR, "type %d no implemented yet", o.o.type);
                     clear_ostor(&o);
                     continue;

               }
               assign_o(*obj, (osm_obj_t*) &o);
               break;
            }
            else if (tag->type == HPX_CLOSE)
            {
               if ((o.o.type != OSM_NODE) && (o.o.type != OSM_WAY) && (o.o.type != OSM_REL))
                  continue;

               // count 'nd', 'tag', and 'member' tags
               for (i = 0; i < tlist->nsub; i++)
               {
                  if (!bs_cmp(tlist->subtag[i]->tag->tag, "tag"))
                     o.o.tag_cnt++;
                  else if (!bs_cmp(tlist->subtag[i]->tag->tag, "nd"))
                     o.w.ref_cnt++;
                  else if (!bs_cmp(tlist->subtag[i]->tag->tag, "member"))
                     o.r.mem_cnt++;
               }

               switch (o.o.type)
               {
                  case OSM_NODE:
                     *obj = (osm_obj_t*) malloc_node(o.o.tag_cnt);
                     break;

                  case OSM_WAY:
                     *obj = (osm_obj_t*) malloc_way(o.o.tag_cnt, o.w.ref_cnt);
                     break;

                  case OSM_REL:
                     *obj = (osm_obj_t*) malloc_rel(o.o.tag_cnt, o.r.mem_cnt);
                     break;
 
                  default:
                     log_msg(LOG_EMERG, "this should never happen! type %d no implemented yet", o.o.type);
                     clear_ostor(&o);
                     continue;
               }

               assign_o(*obj, (osm_obj_t*) &o);

               ref = ((osm_way_t*) *obj)->ref;
               rcnt = 0;
               mem = ((osm_rel_t*) *obj)->mem;
               mcnt = 0;

               for (i = 0, j = 0; i < tlist->nsub; i++)
               {
                  if (!bs_cmp(tlist->subtag[i]->tag->tag, "tag"))
                  {
                     if (get_value("k", tlist->subtag[i]->tag, &(*obj)->otag[j].k) == -1)
                        memset(&(*obj)->otag[j].k, 0, sizeof(bstring_t));
                     if (get_value("v", tlist->subtag[i]->tag, &(*obj)->otag[j].v) == -1)
                        memset(&(*obj)->otag[j].v, 0, sizeof(bstring_t));
                     j++;
                  }
                  else if (!bs_cmp(tlist->subtag[i]->tag->tag, "nd"))
                  {
                     if (o.o.type != OSM_WAY)
                     {
                        log_msg(LOG_WARN, "<nd> only allowed in <way>");
                        continue;
                     }
                     if (get_value("ref", tlist->subtag[i]->tag, &b) != -1)
                     {
                        *ref = bs_tol(b);
                        ref++;
                        rcnt++;
                     }
                  }
                  else if (!bs_cmp(tlist->subtag[i]->tag->tag, "member"))
                  {
                     if (o.o.type != OSM_REL)
                     {
                        log_msg(LOG_WARN, "<member> only allowed in <relation>");
                        continue;
                     }
                     if (get_value("type", tlist->subtag[i]->tag, &b) != -1)
                     {
                        if (!bs_cmp(b, "node"))
                           mem->type = OSM_NODE;
                        else if (!bs_cmp(b, "way"))
                           mem->type = OSM_WAY;
                        else if (!bs_cmp(b, "relation"))
                           mem->type = OSM_REL;
                        else
                           log_msg(LOG_WARN, "relation type may only be 'node', 'way', or 'relation'");
                     }
                     if (get_value("ref", tlist->subtag[i]->tag, &b) != -1)
                     {
                        mem->id = bs_tol(b);
                     }
                     if (get_value("role", tlist->subtag[i]->tag, &b) != -1)
                     {
                        if (!(mem->role = strrole(&b)))
                           log_msg(LOG_WARN, "relation %"PRId64": role type not implemented: '%.*s'",
                                 o.o.id, b.len, b.buf);
                     }
                     // FIXME: 'role' not parsed yet
                     if (mem->type)
                     {
                        mem++;
                        mcnt++;
                     }
                  }
               } // for (i = 0, j = 0; i < tlist->nsub; i++)

               if (o.o.type == OSM_WAY)
                  ((osm_way_t*) *obj)->ref_cnt = rcnt;
               else if (o.o.type == OSM_REL)
                  ((osm_rel_t*) *obj)->mem_cnt = mcnt;
               break;
            } // else if (tag->type == HPX_CLOSE)
            continue;
         } // if (t)

         if ((o.o.type != OSM_NODE) && (o.o.type != OSM_WAY) && (o.o.type != OSM_REL))
            continue;

         if (!bs_cmp(tag->tag, "tag") || !bs_cmp(tag->tag, "nd") || !bs_cmp(tag->tag, "member"))
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
      } // if (!hpx_process_elem(b, tag))
   } //while ((e = hpx_get_elem(ctl, &b, NULL, &tag->line)) > 0)

   *tlistptr = tlist;
   return e;
}


int read_osm_file(hpx_ctrl_t *ctl, bx_node_t **tree, const struct filter *fi, struct dstats *ds)
{
   osm_obj_t *obj;
   osm_node_t *n;
   hpx_tree_t *tlist = NULL;
   bx_node_t *tr;
   time_t tim;
   int e, dup_cnt = 0;

   log_debug("revision >= 1593");
   //install_sigusr1();

   if (hpx_tree_resize(&tlist, 0) == -1)
      perror("hpx_tree_resize"), exit(EXIT_FAILURE);
   if ((tlist->tag = hpx_tm_create(16)) == NULL)
      perror("hpx_tm_create"), exit(EXIT_FAILURE);

   //oline_ = 0;
   tim = time(NULL);

   if (ds != NULL)
      init_stats(ds);

   while ((e = read_osm_obj(ctl, &tlist, &obj)) > 0)
   {
      if (usr1_)
      {
         usr1_ = 0;
         log_msg(LOG_INFO, "onode_memory: %ld kByte, line %ld, %.2f MByte/s",
               (long) onode_mem() / 1024, oline_, ((double) ctl->pos / (double) (time(NULL) - tim)) / (double) (1024 * 1024));
         log_msg(LOG_INFO, "ctl->pos = %ld (%ld %%), ctl->len = %ld, ctl->buf.len = %ld", ctl->pos, ctl->pos * 100 / ctl->len, ctl->len, ctl->buf.len);
      }

      if (obj != NULL)
      {
         // filtering
         if (fi != NULL)
         {
            switch(obj->type)
            {
               case OSM_NODE:
                  n = (osm_node_t*) obj;
                  // skip nodes which are outside of bounding box
                  if (fi->use_bbox && ((n->lat > fi->c1.lat) || (n->lat < fi->c2.lat) || (n->lon > fi->c2.lon) || (n->lon < fi->c1.lon)))
                  {
                     //log_debug("skipping node line %ld", oline_);
                     free_obj(obj);
                     obj = NULL;
                  }
                  break;

               case OSM_WAY:
                  for (int i = 0; i < ((osm_way_t*) obj)->ref_cnt; i++)
                     // test if referenced node exists
                     if (get_object(OSM_NODE, ((osm_way_t*) obj)->ref[i]) == NULL)
                     {
                        // remove, if not
                        memmove(&((osm_way_t*) obj)->ref[i], &((osm_way_t*) obj)->ref[i + 1],
                              (((osm_way_t*) obj)->ref_cnt - i - 1) * sizeof(*((osm_way_t*) obj)->ref));
                        ((osm_way_t*) obj)->ref_cnt--;
                        i--;
                     }
                  if (!((osm_way_t*) obj)->ref_cnt)
                  {
                     free_obj(obj);
                     obj = NULL;
                  }
                  break;

               case OSM_REL:
                  for (int i = 0; i < ((osm_rel_t*) obj)->mem_cnt; i++)
                     if (get_object(((osm_rel_t*) obj)->mem[i].type, ((osm_rel_t*) obj)->mem[i].type) == NULL)
                     {
                        memmove(&((osm_rel_t*) obj)->mem[i], &((osm_rel_t*) obj)->mem[i + 1],
                              (((osm_rel_t*) obj)->mem_cnt - i - 1) * sizeof(*((osm_rel_t*) obj)->mem));
                        ((osm_rel_t*) obj)->mem_cnt--;
                        i--;
 
                     }
                  if (!((osm_rel_t*) obj)->mem_cnt)
                  {
                     free_obj(obj);
                     obj = NULL;
                  }
                  break;
            }
         } // if (fi != NULL)

         // object was deleted by filter
         if (obj == NULL)
            continue;

         tr = bx_add_node(tree, obj->id);
         if (tr->next[obj->type - 1] != NULL)
         {
            free_obj(tr->next[obj->type - 1]);
            dup_cnt++;
         }
         tr->next[obj->type - 1] = obj;

         if (ds != NULL)
            update_stats(obj, ds);
      } //if (obj != NULL)
      else
         log_debug("read_osm_obj() returned NULL object");
   }

   if (e == -1)
      log_msg(LOG_ERR, "hpx_get_elem() failed: %s", strerror(errno));

   if (dup_cnt)
      log_msg(LOG_WARN, "%d duplicate elements found! This may cause unexpected results!", dup_cnt);

   log_msg(LOG_NOTICE, "onode_memory: %ld kByte, line %ld, %.2f MByte/s",
         (long) onode_mem() / 1024, oline_, ((double) ctl->len / (double) (time(NULL) - tim)) / (double) (1024 * 1024));
 
   hpx_tm_free_tree(tlist);

   if (ds != NULL)
   {
      fin_stats(ds);
      log_stats(ds);
   }

   return 0;
}


static int file_cmp(const struct file *a, const struct file *b)
{
   return strcmp(a->name, b->name);
}


hpx_ctrl_t *open_osm_source(const char *s, int w_mmap)
{
   int d, e, i, fd = 0, tfd, fcnt;
   struct file *file = NULL, *p;
   char buf[1024];
   struct dirent *de;
   hpx_ctrl_t *ctl;
   struct stat st;
   regex_t re;
   long size;
   DIR *dir;

   if ((s != NULL) && ((fd = open(s, O_RDONLY)) == -1))
   {
      log_msg(LOG_ERR, "cannot open file %s: %s", s, strerror(errno));
      return NULL;
   }

   if (fstat(fd, &st) == -1)
   {
      log_msg(LOG_ERR, "stat() failed: %s", strerror(errno));
      goto oos_close_fd;
   }

   if (S_ISDIR(st.st_mode))
   {
      if ((dir = fdopendir(fd)) == NULL)
      {
         log_msg(LOG_ERR, "fdopendir() failed: %s", strerror(errno));
         goto oos_close_fd;
      }

      if ((e = regcomp(&re, "\\.osm$", REG_ICASE | REG_NOSUB)))
      {
         log_msg(LOG_ERR, "regcomp() failed: %d", e);
         goto oos_close_dir;
      }

      errno = 0;
      for (size = 0, fcnt = 0; (de = readdir(dir)) != NULL;)
      {
         //if (S_ISDIR(st.st_mode))
         //   continue;

         if (regexec(&re, de->d_name, 0, NULL, 0))
            continue;

         if ((p = realloc(file, sizeof(*file) * (fcnt + 1))) == NULL)
         {
            log_msg(LOG_ERR, "realloc() failed: %s", strerror(errno));
            goto oos_freeall;
         }
         file = p;
         fcnt++;

         snprintf(buf, sizeof(buf), "%s/%s", s, de->d_name);
         if (stat(buf, &st) == -1)
         {
            log_msg(LOG_ERR, "stat() failed: %s", strerror(errno));
            goto oos_close_fd;
         }

         if ((file[fcnt - 1].name = strdup(buf)) == NULL)
         {
            log_msg(LOG_ERR, "strdup() failed: %s", strerror(errno));
            goto oos_freeall;
         }

         log_debug("%s %ld", buf, (long) st.st_size * -(w_mmap != 0));
         size += st.st_size;
         file[fcnt - 1].size = st.st_size;
      }

      if (errno)
         log_msg(LOG_ERR, "readdir() failed: %s", strerror(errno));

      qsort(file, fcnt, sizeof(*file), (int(*)(const void*, const void*)) file_cmp);

#define TEMPFILE "/tmp/smrulesXXXXXX"
      strcpy(buf, TEMPFILE);
      if ((tfd = mkstemp(buf)) == -1)
      {
         log_msg(LOG_ERR, "mkstemp() failed: %s", strerror(errno));
         goto oos_freeall;
      }
      log_debug("created temporary file '%s'", buf);

      if (unlink(buf) == -1)
         log_msg(LOG_WARN, "unlink(%s) failed: %s", buf, strerror(errno));

      for (i = 0; i < fcnt; i++)
      {
         log_debug("reading '%s'...", file[i].name);
         if ((d = open(file[i].name, O_RDONLY)) == -1)
         {
            log_msg(LOG_WARN, "open(%s) failed: %s", buf, strerror(errno));
            continue;
         }

         while ((e = read(d, buf, sizeof(buf))) > 0)
         {
            if (write(tfd, buf, e) == -1)
            {
               log_msg(LOG_ERR, "could not write to temporary file: %s", strerror(errno));
               goto oos_freeallt;
            }
         }

         if (e == -1)
         {
            log_msg(LOG_ERR, "read(%d) failed: %s", d, strerror(errno));
            (void) close(d);
            goto oos_freeallt;
         }

         (void) close(d);
      }

      (void) close(fd);
      fd = tfd;
      regfree(&re);
      while (fcnt--)
         free(file[fcnt].name);
      free(file);
      (void) closedir(dir);
 
      if (lseek(fd, 0, SEEK_SET) == -1)
      {
         log_msg(LOG_ERR, "lseek(%d) failed: %s", fd, strerror(errno));
         goto oos_close_fd;
      }

      if (fstat(fd, &st) == -1)
      {
         log_msg(LOG_ERR, "fstat(%d) failed: %s", fd, strerror(errno));
         goto oos_close_fd;
      }
   }

   if ((ctl = hpx_init(fd, st.st_size)) != NULL)
      return ctl;

   log_msg(LOG_ERR, "hpx_init failed: %s", strerror(errno));
   goto oos_close_fd;

oos_freeallt:
   (void) close(tfd);

oos_freeall:
   regfree(&re);
   while (fcnt--)
      free(file[fcnt].name);
   free(file);

oos_close_dir:
   (void) closedir(dir);
 
oos_close_fd:
   (void) close(fd);
   return NULL;
}

