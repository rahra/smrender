/* Copyright 2011-2014 Bernhard R. Fischer, 2048R/5C5FFD47 <bf@abenteuerland.at>
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

/*! \file smosmout.c
 * This file contains the functions for output of XML/OSM data.
 *
 *  @author Bernhard R. Fischer
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <inttypes.h>
#include <string.h>
#include <errno.h>

#include "smrender.h"
#include "smcore.h"
#include "rdata.h"


struct ostream
{
   FILE *stream;
   size_t len;
};


/*! Safely output a literal string, i.e. it replaces '<' and '"'.
 *  @return It returns the number of bytes written.
 */
int bs_safe_put_xml(FILE *f, const bstring_t *b)
{
   int i, c;

   for (i = 0, c = 0; i < b->len; i++)
      switch (b->buf[i])
      {
         case '"':
            c += fputs("&quot;", f);
            break;
         case '<':
            c += fputs("&lt;", f);
            break;
         default:
            c += fputc(b->buf[i], f);
      }
   return c;
}


static int64_t out_id(int64_t id, int type)
{
   struct rdata *rd = get_rdata();
   int64_t mask;

   if ((id > 0) || !(rd->flags & RD_UIDS))
      return id + rd->id_off;

   switch (type)
   {
      case OSM_NODE:
      case OSM_WAY:
      case OSM_REL:
         mask = rd->ds.id_mask[type];
         break;
      default:
         log_msg(LOG_EMERG, "unknown object type %d", type);
         return 0;
   }

   //log_debug("mask = %"PRIx64, mask);

   return ((id & mask) | (mask + 1)) + rd->id_off;
}


static int fprint_defattr(FILE *f, const osm_obj_t *o, const char *ostr)
{
#define TBUFLEN 24
   char ts[TBUFLEN] = "0000-00-00T00:00:00Z";
   struct tm *tm;

   //FIXME: not thread safe
   if ((tm = gmtime(&o->tim)) != NULL)
      strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%SZ", tm);

   return fprintf(f, "<%s id=\"%"PRId64"\" version=\"%d\" timestamp=\"%s\" uid=\"%d\" visible=\"%s\"", 
         ostr, out_id(o->id, o->type), o->ver ? o->ver : 1, ts, o->uid, o->vis ? "true" : "false");
}


/* This functions outputs an OSM object to a stream.
 * FIXME: Return values of fprintf() and fputs() are not verified.
 */
int print_onode(FILE *f, const osm_obj_t *o)
{
   size_t len = 0;
   int i;

   if (o == NULL)
   {
      log_warn("NULL pointer catched in print_onode()");
      return -1;
   }

   switch (o->type)
   {
      case OSM_NODE:
         len += fprint_defattr(f, o, "node");
         if (o->tag_cnt)
            len += fprintf(f, " lat=\"%.7f\" lon=\"%.7f\">\n", ((osm_node_t*) o)->lat, ((osm_node_t*) o)->lon);
         else
            len += fprintf(f, " lat=\"%.7f\" lon=\"%.7f\"/>\n", ((osm_node_t*) o)->lat, ((osm_node_t*) o)->lon);
         break;

      case OSM_WAY:
         len += fprint_defattr(f, o, "way");
         len += fprintf(f, ">\n");
         break;

      case OSM_REL:
         len += fprint_defattr(f, o, "relation");
         len += fprintf(f, ">\n");
         break;

      default:
         len += fprintf(f, "<!-- unknown node type: %d -->\n", o->type);
         return -1;
   }

   for (i = 0; i < o->tag_cnt; i++)
   {
      len += fputs("<tag k=\"", f);
      len += bs_safe_put_xml(f, &o->otag[i].k);
      len += fputs("\" v=\"", f);
      len += bs_safe_put_xml(f, &o->otag[i].v);
      len += fputs("\"/>\n", f);
      /*fprintf(f, "<tag k=\"%.*s\" v=\"%.*s\"/>\n",
            (int) o->otag[i].k.len, o->otag[i].k.buf, (int) o->otag[i].v.len, o->otag[i].v.buf);*/
   }

  switch (o->type)
   {
      case OSM_NODE:
         if (o->tag_cnt)
            len += fprintf(f, "</node>\n");
         break;

      case OSM_WAY:
         for (i = 0; i < ((osm_way_t*) o)->ref_cnt; i++)
            len += fprintf(f, "<nd ref=\"%"PRId64"\"/>\n", out_id(((osm_way_t*) o)->ref[i], OSM_NODE));
         len += fprintf(f, "</way>\n");
         break;

      case OSM_REL:
         for (i = 0; i < ((osm_rel_t*) o)->mem_cnt; i++)
            len += fprintf(f, "<member type=\"%s\" ref=\"%"PRId64"\" role=\"%s\"/>\n",
                  ((osm_rel_t*) o)->mem[i].type == OSM_NODE ? "node" : "way",
                  out_id(((osm_rel_t*) o)->mem[i].id, ((osm_rel_t*) o)->mem[i].type),
                  role_str(((osm_rel_t*) o)->mem[i].role));
         len += fprintf(f, "</relation>\n");
         break;
   }

   return len;
}


int print_tree(osm_obj_t *o, struct ostream *os)
{
   os->len += print_onode(os->stream, o);
   return 0;
}


/*! Save OSM data of tree to file f.
 *  @param s FILE handle of output file.
 *  @param Pointer to bxtree containing the information.
 *  @param bb Optional bounding box (written to tag <bounds>).
 *  @param info Optional information written to the header as comment (<!-- info -->).
 *  @return The function returns 0.
 */
size_t save_osm0(FILE *f, bx_node_t *tree, const struct bbox *bb, const char *info)
{
   struct ostream os;
   size_t len = 0;
   int n;

   if ((n = fprintf(f, "<?xml version='1.0' encoding='UTF-8'?>\n"
              "<osm version='0.6' generator='smrender'>\n")) < 0)
   {
      n = errno;
      log_msg(LOG_ERR, "fprintf() failed: '%s'", strerror(n));
      return -n;
   }
   len += n;

   if (info != NULL)
   {
      if ((n = fprintf(f, "<!--\n%s\n-->\n", info)) < 0)
      {
         n = errno;
         log_msg(LOG_ERR, "fprintf() failed: '%s'", strerror(n));
         return -n;
      }
      len += n;
   }

   if (bb != NULL)
   {
      if ((n = fprintf(f, "<bounds minlat='%f' minlon='%f' maxlat='%f' maxlon='%f'/>\n",
            bb->ll.lat, bb->ll.lon, bb->ru.lat, bb->ru.lon)) < 0)
      {
         n = errno;
         log_msg(LOG_ERR, "fprintf() failed: '%s'", strerror(n));
         return -n;
      }
      len += n;
   }

   os.stream = f;
   os.len = 0;
   traverse(tree, 0, IDX_NODE, (tree_func_t) print_tree, &os);
   traverse(tree, 0, IDX_WAY, (tree_func_t) print_tree, &os);
   traverse(tree, 0, IDX_REL, (tree_func_t) print_tree, &os);
   len += os.len;

   if ((n = fprintf(f, "</osm>\n")) < 0)
   {
      n = errno;
      log_msg(LOG_ERR, "fprintf() failed: '%s'", strerror(n));
      return -n;
   }
   len += n;

   return len;
}
 

/*! Save OSM data of tree to file s.
 *  @param s Filename of output file.
 *  @param Pointer to bxtree containing the information.
 *  @param bb Optional bounding box (written to tag <bounds>).
 *  @param info Optional information written to the header as comment (<!-- info -->).
 *  @return The function returns 0, or -1 in case of error.
 */
size_t save_osm(const char *s, bx_node_t *tree, const struct bbox *bb, const char *info)
{
   size_t len = 0;
   FILE *f;

   if (s == NULL)
      return -1;

   log_msg(LOG_INFO, "saving osm output to '%s'", s);
   if ((f = fopen(s, "w")) != NULL)
   {
      len = save_osm0(f, tree, bb, info);
      fclose(f);
   }
   else
   {
      log_msg(LOG_WARN, "could not open '%s': %s", s, strerror(errno));
      return -1;
   }

   return len;
}

