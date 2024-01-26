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
 * This file contains all functions regarding the index file.
 *
 *  \author Bernhard R. Fischer
 *  \date 2024/01/26
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <inttypes.h>
#ifdef WITH_MMAP
#include <sys/mman.h>
#endif

#include "smrender_dev.h"
#include "smcore.h"
#include "smloadosm.h"
#include "smcoast.h"

#define INDEX_FDIRTY 1
#define INDEX_EXT ".index"
#define INDEX_IDENT "SMRENDER.INDEX"


typedef struct index_hdr
{
   //! file identification string (INDEX_IDENT)
   char type_str[16];
   //! file format version
   int version;
   //! file flags (INDEX_F...)
   int flags;
   //! number of roles attached to header
   int role_cnt;
   //! number of bytes used by these roles
   int role_size;
   //! number of variable data bytes for the next header after roles
   int var_size;
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


void *baseloc(const void *base, void *ptr)
{
   return (void*) (ptr - base);
}


void *reloc(const void *base, void *ptr)
{
   return ptr + (intptr_t) base; //FIXME: this discards const
}


int index_write_obj(osm_obj_t *o, const indexf_t *idxf)
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


int index_write_header(index_hdr_t *ih, const indexf_t *idxf)
{
   log_debug("writing index header...");
   return sm_write(idxf->fd, ih, sizeof(*ih)) < 0 ? -1 : 0;
}


int index_write_roles(index_hdr_t *ih, const indexf_t *idxf)
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

      len = strlen(s) + 1;
      if (sm_write(idxf->fd, &len, sizeof(len)) < 0)
         return -1;
      if (sm_write(idxf->fd, s, len) < 0)
         return -1;

      ih->role_size += len + sizeof(len);
   }

   return 0;
}


void index_init_header(index_hdr_t *ih, int flags)
{
   strcpy(ih->type_str, INDEX_IDENT);
   ih->version = 1;
   ih->flags = flags;
}


int index_write(const char *fname, bx_node_t *tree, const void *base, const struct dstats *ds)
{
   indexf_t idxf;
   index_hdr_t ih;
   int e = -1;

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
   log_debug("header @ 0x%08lx", 0L);
   index_init_header(&ih, INDEX_FDIRTY);
   index_write_header(&ih, &idxf);
   log_debug("roles @ 0x%08lx", sizeof(ih));
   index_write_roles(&ih, &idxf);
   lseek(idxf.fd, 0, SEEK_SET);
   index_write_header(&ih, &idxf);
   lseek(idxf.fd, ih.role_size, SEEK_CUR);

   log_debug("dstats @ 0x%08lx", sizeof(ih) + ih.role_size);
   log_debug("writing dstats");
   if ((e = sm_write(idxf.fd, ds, sizeof(*ds))) == -1)
      goto iw_exit;
   ih.var_size += e;

   log_debug("objects @ 0x%08lx", sizeof(ih) + ih.role_size + ih.var_size);
   log_debug("saving node index...");
   traverse(tree, 0, IDX_NODE, (tree_func_t) index_write_obj, &idxf);
   log_debug("saving way index...");
   traverse(tree, 0, IDX_WAY, (tree_func_t) index_write_obj, &idxf);
   log_debug("saving relation index...");
   traverse(tree, 0, IDX_REL, (tree_func_t) index_write_obj, &idxf);

   lseek(idxf.fd, 0, SEEK_SET);
   ih.flags = 0;
   index_write_header(&ih, &idxf);

   e = 0;

iw_exit:
   close(idxf.fd);
   return e;
}


int index_read_roles(const index_hdr_t *ih, const void *base, int len)
{
   bstring_t b;
   int i;

   log_debug("called");
   for (i = 0; i < ih->role_cnt; i++)
   {
      if (len < (int) sizeof(short))
         return -1;
      b.len = *((short*) base);
      base += sizeof(short);
      len -= sizeof(short);
      if (!b.len)
         continue;
      // check that rlen makes sense
      if (b.len < 0 || b.len > len)
         return -1;
      // check that string is \0-terminated
      if (((char*) base)[b.len - 1] != '\0')
         return -1;
      // add role to memory data
      b.buf = (char*) base;
      strrole(&b);
      base += b.len;
      len -= b.len;
   }

   return 0;
}


static int check_type(int type)
{
   switch (type)
   {
      case OSM_NODE:
      case OSM_WAY:
      case OSM_REL:
         return type;
   }
   return -1;
}


static int alloc_cpy_upd(void **base, int *len, void **dst, int olen)
{
   if (*len < olen)
      return -1;

   if (*dst != 0)
   {
      log_msg(LOG_ERR, "ptr != 0");
      return -1;
   }

   *dst = malloc_mem(olen, 1);
   memcpy(*dst, *base, olen);
   *base += olen;
   *len -= olen;

   return 0;
}

 
/*! This file reads the objects from the index file.
 * @param base Pointer to index file which points to an object (usually the
 * 1st one).
 * @param len Number of bytes in base.
 * @param osm_base Pointer to memory mapped area of OSM data.
 * @return On success, the function returns 0. On error, -1 is returned. The
 * function does several data checks to check the data integrity of the index.
 * If something odd is discovered, -1 is returned and the index should not be
 * used. If the system is out of memory, the program exits.
 */
int index_read_objs(void *base, int len, const void *osm_base)
{
   const osm_obj_t *o0;
   osm_obj_t *o;
   osm_way_t *w;
   osm_rel_t *r;
   int i;
   long n;

   log_debug("called");
   for (n = 0; len > 0; n++)
   {
      // check minimum object size
      if (len < (int) sizeof(*o0))
         goto iro_err;

      // check object type
      o0 = base;
      if (check_type(o0->type) == -1)
         goto iro_err;

      // copy object data from index to memory
      o = 0;
      if (alloc_cpy_upd(&base, &len, (void**) &o, SIZEOF_OSM_OBJ(o0)))
         goto iro_err;

      // malloc memory for otags
      if (alloc_cpy_upd(&base, &len, (void**) &o->otag, sizeof(*o->otag) * o->tag_cnt))
         goto iro_err;

      // reloc tag pointers
      for (i = 0; i < o->tag_cnt; i++)
      {
         o->otag[i].k.buf = reloc(osm_base, o->otag[i].k.buf);
         o->otag[i].v.buf = reloc(osm_base, o->otag[i].v.buf);
      }

      // read refs
      if (o->type == OSM_WAY)
      {
         w = (osm_way_t*) o;
         if (alloc_cpy_upd(&base, &len, (void**) &w->ref, sizeof(*w->ref) * w->ref_cnt))
            goto iro_err;
      }

      // read members info
      else if (o->type == OSM_REL)
      {
         r = (osm_rel_t*) o;
         if (alloc_cpy_upd(&base, &len, (void**) &r->mem, sizeof(*r->mem) * r->mem_cnt))
            goto iro_err;
      }

      // store object into tree
      put_object(o);
   }

   log_debug("read %ld objects", n);
   return 0;

iro_err:
   log_debug("index error at adress %p, len = %d", base, len);
   return -1;
}


static int cmp_time(time_t a, time_t b)
{
   return a - b;
}


/*! This function compares 2 timespec structs.
 * @param a Pointer to a timespec struct.
 * @param b Pointer to another timespec struct.
 * @return This function returns 0 if both are equal. If a is greater than b, a
 * positiv number is returned, if a is less than b, a negative number is
 * returned.
 */
int cmp_timespec(const struct timespec *a, const struct timespec *b)
{
   int r;

   if ((r = cmp_time(a->tv_sec, b->tv_sec)))
      return r;
   return cmp_time(a->tv_nsec, b->tv_nsec);
}


/*! This function reads the index from the index file.
 * @param fname Name of OSM data file. The index file name ist constructed by
 * concattenating INDEX_EXT.
 * @param base Pointer to memory mapped OSM data.
 * @return On success, the function returns 0, otherwise -1.
 */
int index_read(const char *fname, const void *base, struct dstats *ds)
{
   indexf_t idxf;
   index_hdr_t *ih;
   struct stat st;
   struct timespec ts;
   void *idata, *ibase;
   int size;
   int e = -2;

   log_debug("called");
   // safety check
   if (fname == NULL)
   {
      log_msg(LOG_CRIT, "null pointer caught");
      return ESM_NULLPTR;
   }

   // get mtime from OSM file
   if (stat(fname, &st) == -1)
   {
      log_errno(LOG_ERR, "could not stat() OSM file");
      return -1;
   }
   ts = st.st_mtim;

   // make index file name
   char buf[strlen(fname) + strlen(INDEX_EXT) + 1];
   snprintf(buf, sizeof(buf), "%s%s", fname, INDEX_EXT);

   log_msg(LOG_NOTICE, "reading index file \"%s\"", buf);

   // open index file
   memset(&idxf, 0, sizeof(idxf));
   idxf.base = base;
   if ((idxf.fd = open(buf, O_RDWR)) == -1)
   {
      log_errno(LOG_NOTICE, "could not open index file");
      return ESM_NOFILE;
   }

   // stat index file
   if (fstat(idxf.fd, &st) == -1)
   {
      log_errno(LOG_ERR, "stat() failed");
      goto ri_err;
   }

   // compare timestamps
   if (cmp_timespec(&st.st_mtim, &ts) < 0)
   {
      e = ESM_OUTDATED;
      log_msg(LOG_WARN, "index file is older than data file");
      goto ri_err;
   }

   // make sure index file has reasonable minimum size
   if (st.st_size < (off_t) sizeof(*ih))
   {
      log_msg(LOG_ERR, "index file too small: %ld", st.st_size);
      e = ESM_TRUNCATED;
      goto ri_err;

   }

   // map file to memory
   if ((ibase = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE | MAP_NORESERVE, idxf.fd, 0)) == MAP_FAILED)
   {
      log_errno(LOG_ERR, "mmap() failed");
      goto ri_err;
   }

   // get header and check integrity
   ih = idata = ibase;
   if (memcmp(ih->type_str, INDEX_IDENT, strlen(INDEX_IDENT) + 1))
   {
      log_msg(LOG_ERR, "file identification does not match");
      goto ri_err2;
   }
   if (ih->version != 1)
   {
      log_msg(LOG_ERR, "incorrection version: %d", ih->version);
      goto ri_err2;
   }
   if (ih->flags & INDEX_FDIRTY)
   {
      log_msg(LOG_ERR, "index is flagged as dirty");
      goto ri_err2;
   }

   idata += sizeof(*ih);
   size = st.st_size - sizeof(*ih);

   // read roles
   if (index_read_roles(ih, idata, size) == -1)
   {
      log_msg(LOG_ERR, "index corrupt");
      goto ri_err2;
   }
   idata += ih->role_size;
   size -= ih->role_size;

   // read dstats (variable data chunk)
   log_debug("reading dstats");
   if (ih->var_size < 0)
   {
      log_msg(LOG_ERR, "index corrupt");
      goto ri_err2;
   }
   if (ih->var_size < (int) sizeof(*ds) || size < (int) sizeof(*ds) || ih->var_size != (int) sizeof(*ds))
      goto ri_err2;
   memcpy(ds, idata, sizeof(*ds));
   idata += ih->var_size;
   size -= ih->var_size;

   // read OSM objects
   log_debug("reading objects");
   if (index_read_objs(idata, size, idxf.base) == -1)
   {
      log_msg(LOG_ERR, "index corrupt");
      goto ri_err2;
   }

   e = 0;

ri_err2:
   munmap(ibase, st.st_size);

ri_err:
   close(idxf.fd);

   return e;
}

