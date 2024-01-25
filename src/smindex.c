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
#define INDEX_EXT ".index"


typedef struct index_hdr
{
   char type_str[16];
   int version;
   int flags;
   int role_cnt;
   int role_size;
} index_hdr_t;


ssize_t sm_write(int fd, const void *buf, size_t len)
{
   ssize_t wlen;

   wlen = write(fd, buf, len);
   if (wlen == -1)
      log_errno(LOG_ERR, "write() failed");
   else if ((size_t) wlen < len)
   {
      log_msg(LOG_ERR, "write() truncated, wrote %ld of %ld bytes", wlen, len);
      wlen = -wlen;
   }

   return wlen;
}


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

   if (sm_write(idxf->fd, &os, size) < 0)
      return -1;

   for (i = 0; i < o->tag_cnt; i++)
   {
      memcpy(&otag, &o->otag[i], sizeof(otag));
      otag.k.buf = baseloc(idxf->base, otag.k.buf);
      otag.v.buf = baseloc(idxf->base, otag.v.buf);
      if (sm_write(idxf->fd, &otag, sizeof(otag)) < 0)
         return -1;
   }

   if (o->type == OSM_WAY)
   {
      osm_way_t *w = (osm_way_t*) o;
      if (sm_write(idxf->fd, w->ref, w->ref_cnt * sizeof(*w->ref)) < 0)
         return -1;
   }
   else if (o->type == OSM_REL)
   {
      osm_rel_t *r = (osm_rel_t*) o;
      if (sm_write(idxf->fd, r->mem, r->mem_cnt * sizeof(*r->mem)) < 0)
         return -1;
   }

   return 0;
}


int write_index_header(index_hdr_t *ih, const indexf_t *idxf)
{
   log_debug("writing index header...");
   return sm_write(idxf->fd, ih, sizeof(*ih)) < 0 ? -1 : 0;
}


int write_index_roles(index_hdr_t *ih, const indexf_t *idxf)
{
   int i;
   short len;
   const char *s;

   log_debug("writing roles..");
   for (i = ROLE_FIRST_FREE_NUM; ; i++, ih->role_cnt++)
   {
      s = role_str(i);
      if (!strcmp(s, "n/a"))
         break;

      len = strlen(s) + 1 + sizeof(len);
      if (sm_write(idxf->fd, &len, sizeof(len)) < 0)
         return -1;
      if (sm_write(idxf->fd, s, len) < 0)
         return -1;

      ih->role_size += len;
   }

   return 0;
}


void init_index_header(index_hdr_t *ih, int flags)
{
   strcpy(ih->type_str, "SMRENDER.INDEX");
   ih->version = 1;
   ih->flags = flags;
}


int write_index(const char *fname, bx_node_t *tree, const void *base)
{
   indexf_t idxf;
   index_hdr_t ih;

   log_debug("called");
   if (fname == NULL || tree == NULL)
   {
      log_msg(LOG_CRIT, "null pointer caught");
      return -1;
   }

   char buf[strlen(fname) + strlen(INDEX_EXT) + 1];
   snprintf(buf, sizeof(buf), "%s%s", fname, INDEX_EXT);

   memset(&idxf, 0, sizeof(idxf));
   idxf.base = base;
   log_msg(LOG_NOTICE, "creating index file \"%s\"", buf);
   if ((idxf.fd = creat(buf, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH )) == -1)
   {
      log_errno(LOG_ERR, "could not create index file");
      return -1;
   }

   memset(&ih, 0, sizeof(ih));
   init_index_header(&ih, INDEX_DIRTY);
   write_index_header(&ih, &idxf);
   write_index_roles(&ih, &idxf);
   lseek(idxf.fd, 0, SEEK_SET);
   write_index_header(&ih, &idxf);
   lseek(idxf.fd, ih.role_size, SEEK_CUR);

   log_debug("saving node index...");
   traverse(tree, 0, IDX_NODE, (tree_func_t) write_obj_index, &idxf);
   log_debug("saving way index...");
   traverse(tree, 0, IDX_WAY, (tree_func_t) write_obj_index, &idxf);
   log_debug("saving relation index...");
   traverse(tree, 0, IDX_REL, (tree_func_t) write_obj_index, &idxf);

   lseek(idxf.fd, 0, SEEK_SET);
   init_index_header(&ih, 0);
   write_index_header(&ih, &idxf);

   return 0;
}

