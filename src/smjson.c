/* Copyright 2022-2023 Bernhard R. Fischer, 4096R/8E24F29D <bf@abenteuerland.at>
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

/*! \file smjson.c
 * This file contains the functions to create rules file in JSON format.
 * The implementation shall follow the JSON specification:
 * https://www.json.org/json-en.html
 *
 *  \author Bernhard R. Fischer, <bf@abenteuerland.at>
 *  \date 2023/09/24
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <errno.h>

#include "smrender_dev.h"
#include "smcore.h"
#include "bstring.h"


/*! This function returns the index of the first occurence if c in the string
 * s.
 * \param s Pointer to string.
 * \param c Character to search for in s.
 * \return The function returns index of c in s. If c does not occur in s, -1
 * is returned.
 */
int strpos(const char *s, int c)
{
   char *d;

   if ((d = strchr(s, c)) == NULL)
      return -1;
   return d - s;
}


/*! This function escapes characters in the string src and puts the resulting
 * string into dst. The chars are escape with a backslash. Thereby all chars
 * found in echars are replaced by the corresponding character in uchars and
 * prepended by a single backslash.
 * The destination buffer dst obviously must be bigger than src. In the worst
 * case it is twice as large as src if every charcter has to be escaped. The
 * destination buffer will be 0-terminated, thus the buffer must also have 1
 * byte extra space for it. That means dlen should be src.len * 2 + 1 to be
 * safe.
 * If dst is NULL the function will escape the source string src but will not
 * write the result anywhere. Thus, it returns the number of bytes which would
 * be needed for the escape buffer (exluding the terminating \0).
 * \param src The source string as a bstring_t structure.
 * \param dst A pointer to the destination buffer.
 * \param dlen The size of the destination buffer.
 * \param echars A character array of the chars which shall be escaped.
 * \param uchars A character array which are the replacements.
 * \return The function returns the length of the destination string excluding
 * the terminating \0 char, i.e. it is the strlen(dst).
 * In case of error, -1 is returned.
 */
int bs_stresc(bstring_t src, char *dst, int dlen, const char *echars, const char *uchars)
{
   int len, n;

   // safety check
   if (src.buf == NULL || echars == NULL || uchars == NULL || strlen(echars) != strlen(uchars))
   {
      log_msg(LOG_EMERG, "NULL pointer caught, or strlen(echars) != strlen(uchars))");
      return -1;
   }

   if (dst == NULL)
      dlen = src.len * 2 + 1;

   dlen--;
   for (len = 0; src.len > 0 && len < dlen; src.buf++, src.len--, len++, dst++)
   {
      if ((n = strpos(echars, *src.buf)) == -1)
      {
         if (dst != NULL)
            *dst = *src.buf;
         continue;
      }

      // check if there is enough space in destinatin
      if (dlen - len < 2)
         return -1;

      if (dst != NULL)
      {
         *dst++ = '\\';
         *dst = uchars[n];
      }

      len++;
   }

   if (dst != NULL)
      *dst = '\0';

   return len;
}


/*! This function is a wrapper for bs_stresc(). */
int stresc(const char *src, int slen, char *dst, int dlen, const char *echars, const char *uchars)
{
   return bs_stresc((bstring_t) {slen, (char*) src}, dst, dlen, echars, uchars);
}


int jesc(const char *src, int slen, char *dst, int dlen)
{
   return stresc(src, slen, dst, dlen, "\"\\/\b\f\n\r\t", "\"\\/bfnrt");
}


static const char *mod_str(int n)
{
   n &= 0xffff;
   switch (n & ~SPECIAL_MASK)
   {
      case 0:
         return "";
      case SPECIAL_NOT | SPECIAL_INVERT:
         return "INV|NOT";
      case SPECIAL_NOT:
         return "NOT";
      case SPECIAL_INVERT:
         return "INV";
      default:
         return "unknown";
   }
}


static const char *op_str(int n)
{
   switch (n & SPECIAL_MASK)
   {
      case SPECIAL_DIRECT:
         return "cmp";
      case SPECIAL_REGEX:
         return "regex";
      case SPECIAL_GT:
         return "gt";
      case SPECIAL_LT:
         return "lt";
      default:
         return "unknown";
   }
}


static void findent(const rinfo_t *ri)
{
   if (ri->flags & RI_CONDENSED)
      return;

   int len = ri->indent * ri->nindent;
   char buf[len];

   memset(buf, ' ', len);
   fwrite(buf, len, 1, ri->f);
}


static void fcondchar(const rinfo_t *ri, char c)
{
   if (!(ri->flags & RI_CONDENSED))
      fprintf(ri->f, "%c", c);
}


static void fspace(const rinfo_t *ri)
{
   fcondchar(ri, ' ');
}


static void fnl(const rinfo_t *ri)
{
   fcondchar(ri, '\n');
}


static void funsep(const rinfo_t *ri)
{
   fseek(ri->f, ri->flags & RI_CONDENSED ? -1 : -2, SEEK_CUR);
   fnl(ri);
}


static void fochar(rinfo_t *ri, char c)
{
   findent(ri);
   fprintf(ri->f, "%c", c);
   fnl(ri);
   ri->indent++;
}


static void fcchar(rinfo_t *ri, char c)
{
   ri->indent--;
   findent(ri);
   fprintf(ri->f, "%c,", c);
   fnl(ri);
}


static void fkey0(const rinfo_t *ri, const char *k, void (fsepchar)(const rinfo_t*))
{
   findent(ri);
   fprintf(ri->f, "\"%s\":", k);
   fsepchar(ri);
}


static void fkey(const rinfo_t *ri, const char *k)
{
   fkey0(ri, k, fspace);
}


static void fkeyblock(const rinfo_t *ri, const char *k)
{
   fkey0(ri, k, fnl);
}


static void fbkey(const rinfo_t *ri, const bstring_t *k)
{
   char buf[k->len * 2 + 2];
   int len;

   if ((len = jesc(k->buf, k->len, buf, sizeof(buf))) == -1)
      return;

   fkey(ri, buf);
}


static void fint(const rinfo_t *ri, const char *k, int64_t v)
{
   fkey(ri, k);
   fprintf(ri->f, "%"PRId64",", v);
   fnl(ri);
}


static void fbool(const rinfo_t *ri, const char *k, int v)
{
   fkey(ri, k);
   fprintf(ri->f, "%s,", v ? "true" : "false");
   fnl(ri);
}


static void fcoords(const rinfo_t *ri, const char *k, double lat, double lon)
{
   fkey(ri, k);
   fprintf(ri->f, "[%.7f,", lat);
   fspace(ri);
   fprintf(ri->f, "%.7f],", lon);
   fnl(ri);
}


static void fbstring(const rinfo_t *ri, const char *k, const bstring_t *v)
{
   char buf[v->len * 2 + 2];
   int len;

   if ((len = jesc(v->buf, v->len, buf, sizeof(buf))) == -1)
      return;

   fkey(ri, k);
   fprintf(ri->f, "\"%.*s\",", len, buf);
   fnl(ri);
}


static void fbbstring(const rinfo_t *ri, const bstring_t *k, const bstring_t *v)
{
   char buf[v->len * 2 + 2];
   int len;

   if ((len = jesc(v->buf, v->len, buf, sizeof(buf))) == -1)
      return;

   fbkey(ri, k);
   fprintf(ri->f, "\"%.*s\",", len, buf);
   fnl(ri);
}


static void fstring(const rinfo_t *ri, const char *k, const char *v)
{
   char buf[strlen(v) * 2 + 2];
   int len;

   if ((len = jesc(v, strlen(v), buf, sizeof(buf))) == -1)
      return;

   fkey(ri, k);
   fprintf(ri->f, "\"%s\",", buf);
   fnl(ri);
}


static void ftag1(rinfo_t *ri, const char *k, const bstring_t *b, int type)
{
   fkeyblock(ri, k);
   fochar(ri, '{');
   fbstring(ri, "str", b);
   fstring(ri, "op", op_str(type));

   if (type & ~SPECIAL_MASK)
      fstring(ri, "mod", mod_str(type));

   funsep(ri);
   fcchar(ri, '}');
}


static void ftag(rinfo_t *ri, const struct otag *ot, const struct stag *st)
{
   ftag1(ri, "k", &ot->k, st->stk.type);
   ftag1(ri, "v", &ot->v, st->stv.type);
   funsep(ri);
}


static void rule_info_tags(rinfo_t *ri, const smrule_t *r)
{
   if (!r->oo->tag_cnt)
      return;

   fkeyblock(ri, "tags");
   fochar(ri, '[');
   for (int i = 0; i < r->oo->tag_cnt; i++)
   {
      fochar(ri, '{');
      ftag(ri, &r->oo->otag[i], &r->act->stag[i]);
      fcchar(ri, '}');
   }
   funsep(ri);
   fcchar(ri, ']');
}


static void fparams(rinfo_t *ri, fparam_t **fp)
{
   if (fp == NULL)
      return;

   fkeyblock(ri, "params");
   fochar(ri, '{');
   for (; *fp != NULL; fp++)
   {
      fstring(ri, (*fp)->attr, (*fp)->val);
   }
   funsep(ri);
   fcchar(ri, '}');
}


static int rule_info(const smrule_t *r, const rinfo_t *ri0)
{
   rinfo_t _ri, *ri = &_ri;

   //safety check
   if (r == NULL || ri == NULL) return -1;

   *ri = *ri0;

   if (r->oo->ver != ri->version)
      return 0;

   fochar(ri, '{');
   fstring(ri, "type", type_str(r->oo->type));
   fint(ri, "version", r->oo->ver);
   fint(ri, "id", r->oo->id);
   if (r->act->func_name != NULL)
      fstring(ri, "action", r->act->func_name);
   fbool(ri, "visible", r->oo->vis);
   fparams(ri, r->act->fp);
   rule_info_tags(ri, r);
   funsep(ri);
   fcchar(ri, '}');
   return 0;
}


int rules_info(const struct rdata *rd, rinfo_t *ri, const struct dstats *rstats)
{
   // safety check
   if (ri->fname == NULL || rstats == NULL)
   {
      log_msg(LOG_EMERG, "{fname|rstats} == NULL");
      return -1;
   }

   if ((ri->f = fopen(ri->fname, "w")) == NULL)
   {
      log_errno(LOG_ERR, "fopen() failed");
      return -1;
   }

   fochar(ri, '[');
   for (int i = 0; i < rstats->ver_cnt; i++)
   {
      log_msg(LOG_NOTICE, "saving pass %d (ver = %d)", i, rstats->ver[i]);
      ri->version = rstats->ver[i];
      execute_rules0(rd->rules, (tree_func_t) rule_info, ri);
   }
   funsep(ri);
   fprintf(ri->f, "]\n");

   fclose(ri->f);
   return 0;
}


typedef enum {JTYPE, JVERSION, JID, JVIS, JTAGS, JCOORDS, JREF, JMEM, JROLE, J_MAX} jkey_t;


static const char *jkeystr(const rinfo_t *ri, jkey_t k)
{
   const char *jstr[2][J_MAX] =
   {
      {"type", "version", "id", "visible", "tags", "coords", "ref", "members", "role"},
      {"o", "v", "i", "s", "t", "c", "r", "m", "l"}
   };

   // safety check
   if (k < 0 || k >= J_MAX)
      return "";

   return jstr[(ri->flags & RI_SHORT) == RI_SHORT][k];
}


static void onode_info_tags(rinfo_t *ri, const osm_obj_t *o)
{
   if (!o->tag_cnt)
      return;

   fkeyblock(ri, jkeystr(ri, JTAGS));
   fochar(ri, '[');
   for (int i = 0; i < o->tag_cnt; i++)
      fbbstring(ri, &o->otag[i].k, &o->otag[i].v);
   funsep(ri);
   fcchar(ri, ']');
}


static void fwrefs(rinfo_t *ri, const osm_way_t *w)
{
   if (!w->ref_cnt)
      return;

   fkeyblock(ri, jkeystr(ri, JREF));
   fochar(ri, '[');
   for (int i = 0; i < w->ref_cnt; i++)
   {
      findent(ri);
      fprintf(ri->f, "%"PRId64",", w->ref[i]);
      fnl(ri);
   }
   funsep(ri);
   fcchar(ri, ']');
}


static void frmembers(rinfo_t *ri, const osm_rel_t *r)
{
   if (!r->mem_cnt)
      return;

   fkeyblock(ri, jkeystr(ri, JMEM));
   fochar(ri, '[');
   for (int i = 0; i < r->mem_cnt; i++)
   {
      fochar(ri, '{');

      if (ri->flags & RI_SHORT)
         fint(ri, jkeystr(ri, JTYPE), r->mem[i].type);
      else
         fstring(ri, jkeystr(ri, JTYPE), type_str(r->mem[i].type));

      fint(ri, jkeystr(ri, JID), r->mem[i].id);
      fstring(ri, jkeystr(ri, JROLE), role_str(r->mem[i].role));
      funsep(ri);
      fcchar(ri, '}');
   }
   funsep(ri);
   fcchar(ri, ']');
}


//FIXME: this call counter is not thread safe
static long call_cnt_;


static int print_onode_json(const osm_obj_t *o, rinfo_t *ri)
{
   // omit invisible objects if visible-only flag (RI_VISIBLE) is set
   if ((ri->flags & RI_VISIBLE) && !o->vis)
      return 0;

   call_cnt_++;
   fochar(ri, '{');
   if (ri->flags & RI_SHORT)
      fint(ri, jkeystr(ri, JTYPE), o->type);
   else
      fstring(ri, jkeystr(ri, JTYPE), type_str(o->type));
   fint(ri, jkeystr(ri, JVERSION), o->ver);
   fint(ri, jkeystr(ri, JID), o->id);
   if (ri->flags & RI_SHORT)
   {
      if (!(ri->flags & RI_VISIBLE))
         fint(ri, jkeystr(ri, JVIS), o->vis);
   }
   else
      fbool(ri, jkeystr(ri, JVIS), o->vis);
   onode_info_tags(ri, o);

   switch (o->type)
   {
      case OSM_NODE:
         fcoords(ri, jkeystr(ri, JCOORDS), ((osm_node_t*) o)->lat, ((osm_node_t*) o)->lon);
         break;

      case OSM_WAY:
         fwrefs(ri, (osm_way_t*) o);
        break;

      case OSM_REL:
         frmembers(ri, (osm_rel_t*) o);
         break;

      default:
         fstring(ri, "note", "*** type unknown");
   }

   funsep(ri);
   fcchar(ri, '}');
   return 0;
}


/*! Save OSM data of tree to file s in JSON format.
 *  @param s Filename of output file.
 *  @param Pointer to bxtree containing the information.
 *  @return The function returns 0, or -1 in case of error.
 */
size_t save_json(const char *s, bx_node_t *tree, int flags)
{
   rinfo_t _ri, *ri = &_ri;

   if (s == NULL)
      return -1;

   memset(&_ri, 0, sizeof(_ri));

   log_msg(LOG_INFO, "saving JSON output to '%s'", s);
   if ((ri->f = fopen(s, "w")) == NULL)
   {
      log_msg(LOG_WARN, "could not open '%s': %s", s, strerror(errno));
      return -1;
   }

   ri->flags = flags;
   ri->nindent = flags >> 16;

   fochar(ri, '{');
   fkeyblock(ri, "node");
   fochar(ri, '[');
   call_cnt_ = 0;
   traverse(tree, 0, IDX_NODE, (tree_func_t) print_onode_json, ri);
   if (!call_cnt_)
      fnl(ri);
   funsep(ri);
   fcchar(ri, ']');
   fkeyblock(ri, "way");
   fochar(ri, '[');
   call_cnt_ = 0;
   traverse(tree, 0, IDX_WAY, (tree_func_t) print_onode_json, ri);
   if (!call_cnt_)
      fnl(ri);
   funsep(ri);
   fcchar(ri, ']');
   fkeyblock(ri, "relation");
   fochar(ri, '[');
   call_cnt_ = 0;
   traverse(tree, 0, IDX_REL, (tree_func_t) print_onode_json, ri);
   if (!call_cnt_)
      fnl(ri);
   funsep(ri);
   fcchar(ri, ']');
   funsep(ri);
   fcchar(ri, '}');
   funsep(ri);

   return 0;
}

