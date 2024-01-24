/* Copyright 2024 Bernhard R. Fischer, 4096R/8E24F29D <bf@abenteuerland.at>
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

/*! \file smindex.c
 * This file contains all rule functions which do not create graphics output.
 *
 *  @author Bernhard R. Fischer
 *  \date 2023/09/24
 */
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <inttypes.h>

#include "smrender_dev.h"
#include "smcore.h"
#include "smloadosm.h"
#include "smcoast.h"

#define INDEX_DIRTY 1

typedef struct index_hdr
{
   char type_str[16];
   int version;
   int flags;
   int role_cnt;
   int role_size;
} index_hdr_t;


void *baseloc(void *base, void *ptr)
{
   return (void*) (ptr - base);
}


int write_obj_index(osm_obj_t *o, const indexf_t *idxf)
{
   struct otag otag;
   osm_storage_t os;
   int i, size;

   if (o == NULL)
      return -1;

   size = SIZEOF_OSM_OBJ(o);
   memcpy(&os, o, size);
   os.o.otag = 0;
   if (o->type == OSM_WAY)
      os.w.ref = 0;
   else if (o->type == OSM_REL)
      os.r.mem = 0;

   write(idxf->fd, &os, size);
   for (i = 0; i < o->tag_cnt; i++)
   {
      memcpy(&otag, &o->otag[i], sizeof(otag));
      otag.k.buf = baseloc(idxf->base, otag.k.buf);
      otag.v.buf = baseloc(idxf->base, otag.v.buf);
      write(idxf->fd, &otag, sizeof(otag));
   }

   if (o->type == OSM_WAY)
   {
      osm_way_t *w = (osm_way_t*) o;
      write(idxf->fd, w->ref, w->ref_cnt * sizeof(*w->ref));
   }
   else if (o->type == OSM_REL)
   {
      osm_rel_t *r = (osm_rel_t*) o;
      write(idxf->fd, r->mem, r->mem_cnt * sizeof(*r->mem));
   }

   return 0;
}


int write_index_header(index_hdr_t *ih, const indexf_t *idxf)
{
   int i;
   short len;
   char *s;

   memset(ih, 0, sizeof(*ih));
   strcpy(ih->type_str, "SMRENDER.INDEX");
   ih->version = 1;
   ih->flags = INDEX_DIRTY;

   errno = 0;
   if (write(idxf->fd, ih, sizeof(*ih)) < sizeof(*ih))
      return -1;

   for (i = ROLE_FIRST_FREE_NUM; ; i++, ih.role_cnt++)
   {
      s = role_str(i);
      if (!strcmp(s, "n/a"))
         break;

      len = strlen(s) + 1 + sizeof(len);
      if (write(idxf->fd, &len, sizeof(len)) < sizeof(len))
         return -1;
      if (write(idxf->fd, s, len) < len)
         return -1;

      ih->role_size += len;
   }

   lseek(idxf->fd, 0, SEEK_SET);
   write(idxf->fd, ih, sizeof(*ih));
   lseek(idxf->fd, ih->role_size, SEEK_CUR);

   return 0;
}

#define INDEX_EXT ".index"
int write_index(const char *fname, bx_node_t *tree)
{
   indexf_t idxf;
   index_hdr_t ih;

   log_debug("write_index() called");
   if (fname == NULL || tree == NULL)
   {
      log_msg(LOG_CRIT, "null pointer caught");
      return -1;
   }

   char buf[strlen(fname) + strlen(INDEX_EXT) + 1];
   snprintf(buf, sizeof(buf), "%s%s", fname, INDEX_EXT);

   memset(&idxf, 0, sizeof(idxf));
   log_msg(LOG_NOTICE, "creating index file \"%s\"" buf);
   if ((idxf->fd = creat(buf, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH )) == -1)
   {
      log_errno(LOG_ERR, "could not create index file");
      return -1;
   }

   rn->tree = tree;
   rn->id = 1;
   log_debug("renumbering nodes...");
   traverse(tree, 0, IDX_NODE, (tree_func_t) renumber_obj, rn);
   rn->id = 1;
   log_debug("renumbering ways...");
   traverse(tree, 0, IDX_WAY, (tree_func_t) renumber_obj, rn);
   rn->id = 1;
   log_debug("renumbering relations, pass 1...");
   traverse(tree, 0, IDX_REL, (tree_func_t) renumber_obj, rn);
   rn->id = 1;
   rn->pass++;
   log_debug("renumbering relations, pass 2...");
   traverse(tree, 0, IDX_REL, (tree_func_t) renumber_obj, rn);

   return 0;
}

