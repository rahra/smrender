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

/*! This file contains the code of the rule parser and main loop of the render
 * as well as the code for traversing the object (nodes/ways) tree.
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
      return id;

   switch (type)
   {
      case OSM_NODE:
         mask = rd->ds.nid_mask;
         break;
      case OSM_WAY:
         mask = rd->ds.wid_mask;
         break;
      case OSM_REL:
         // FIXME: artificial limit!
         mask = INT64_C(30);
         break;
      default:
         log_msg(LOG_EMERG, "unknown object type %d", type);
         return 0;
   }

   //log_debug("mask = %"PRIx64, mask);

   return (id & mask) | (mask + 1);
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


int print_onode(FILE *f, const osm_obj_t *o)
{
   int i;

   if (o == NULL)
   {
      log_warn("NULL pointer catched in print_onode()");
      return -1;
   }

   switch (o->type)
   {
      case OSM_NODE:
         fprint_defattr(f, o, "node");
         if (o->tag_cnt)
            fprintf(f, " lat=\"%.7f\" lon=\"%.7f\">\n", ((osm_node_t*) o)->lat, ((osm_node_t*) o)->lon);
         else
            fprintf(f, " lat=\"%.7f\" lon=\"%.7f\"/>\n", ((osm_node_t*) o)->lat, ((osm_node_t*) o)->lon);
         break;

      case OSM_WAY:
         fprint_defattr(f, o, "way");
         fprintf(f, ">\n");
         break;

      case OSM_REL:
         fprint_defattr(f, o, "relation");
         fprintf(f, ">\n");
         break;

      default:
         fprintf(f, "<!-- unknown node type: %d -->\n", o->type);
         return -1;
   }

   for (i = 0; i < o->tag_cnt; i++)
   {
      fputs("<tag k=\"", f);
      bs_safe_put_xml(f, &o->otag[i].k);
      fputs("\" v=\"", f);
      bs_safe_put_xml(f, &o->otag[i].v);
      fputs("\"/>\n", f);
      /*fprintf(f, "<tag k=\"%.*s\" v=\"%.*s\"/>\n",
            (int) o->otag[i].k.len, o->otag[i].k.buf, (int) o->otag[i].v.len, o->otag[i].v.buf);*/
   }

  switch (o->type)
   {
      case OSM_NODE:
         if (o->tag_cnt)
            fprintf(f, "</node>\n");
         break;

      case OSM_WAY:
         for (i = 0; i < ((osm_way_t*) o)->ref_cnt; i++)
            fprintf(f, "<nd ref=\"%"PRId64"\"/>\n", out_id(((osm_way_t*) o)->ref[i], OSM_NODE));
         fprintf(f, "</way>\n");
         break;

      case OSM_REL:
         for (i = 0; i < ((osm_rel_t*) o)->mem_cnt; i++)
            fprintf(f, "<member type=\"%s\" ref=\"%"PRIu64"\" role=\"%s\"/>\n",
                  ((osm_rel_t*) o)->mem[i].type == OSM_NODE ? "node" : "way",
                  out_id(((osm_rel_t*) o)->mem[i].id, ((osm_rel_t*) o)->mem[i].type),
                  role_str(((osm_rel_t*) o)->mem[i].role));
         fprintf(f, "</relation>\n");
         break;
   }

   return 0;
}


int print_tree(osm_obj_t *o, void *p)
{
   print_onode(p, o);
   return 0;
}


/*! Save OSM data of tree to file s.
 *  @param s Filename of output file.
 *  @param Pointer to bxtree containing the information.
 *  @param bb Optional bounding box (written to tag <bounds>).
 *  @param info Optional information written to the header as comment (<!-- info -->).
 *  @return The function returns 0, or -1 in case of error.
 */
int save_osm(const char *s, bx_node_t *tree, const struct bbox *bb, const char *info)
{
   FILE *f;

   if (s == NULL)
      return -1;

   log_msg(LOG_INFO, "saving osm output to '%s'", s);
   if ((f = fopen(s, "w")) != NULL)
   {
      fprintf(f, "<?xml version='1.0' encoding='UTF-8'?>\n"
                 "<osm version='0.6' generator='smrender'>\n");
      if (info != NULL)
         fprintf(f, "<!--\n%s\n-->\n", info);
      if (bb != NULL)
         fprintf(f, "<bounds minlat='%f' minlon='%f' maxlat='%f' maxlon='%f'/>\n",
               bb->ll.lat, bb->ll.lon, bb->ru.lat, bb->ru.lon);
      traverse(tree, 0, IDX_NODE, print_tree, f);
      traverse(tree, 0, IDX_WAY, print_tree, f);
      traverse(tree, 0, IDX_REL, print_tree, f);
      fprintf(f, "</osm>\n");
      fclose(f);
   }
   else
      log_msg(LOG_WARN, "could not open '%s': %s", s, strerror(errno));

   return 0;
}

