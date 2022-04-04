/* Copyright 2022 Bernhard R. Fischer, 4096R/8E24F29D <bf@abenteuerland.at>
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
 *
 *  \author Bernhard R. Fischer, <bf@abenteuerland.at>
 *  \date 2022/04/04
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <string.h>

#include "smrender_dev.h"
#include "smcore.h"
#include "bstring.h"


#define INDENT 3
#define CCHAR (condensed_ ? ' ' : '\n')


static int condensed_ = 0;
static int indent_ = 1;


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
      return -1;

   if (dst == NULL)
      dlen = src.len * 2 + 1;

   dlen--;
   for (len = 0; src.len > 0 && len < dlen; src.buf++, src.len--, len++)
   {
      if ((n = strpos(echars, *src.buf)) == -1)
      {
         if (dst != NULL)
            *dst++ = *src.buf;
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
   return stresc(src, slen, dst, dlen, "\b\f\n\r\t\"\\", "bfnrt\"\\");

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


static void findent(FILE *f, int n)
{
   if (condensed_ || !indent_)
      return;

   int len = n * INDENT;
   char buf[n * INDENT];

   memset(buf, ' ', len);
   fwrite(buf, len, 1, f);
}


static void funsep(FILE *f)
{
   fseek(f, -2, SEEK_CUR);
   fprintf(f, "%c", CCHAR);
}


static void fochar(FILE *f, char c)
{
   fprintf(f, "%c%c", c, CCHAR);
}


static void fcchar(FILE *f, char c)
{
   fprintf(f, "%c,%c", c, CCHAR);
}


static void fint(FILE *f, const char *k, long v, int indent)
{
   findent(f, indent);
   fprintf(f, "\"%s\": %ld,%c", k, v, CCHAR);
}


static void fbstring(FILE *f, const char *k, const bstring_t *v, int indent)
{
   char buf[v->len * 2 + 2];
   int len;

   if ((len = jesc(v->buf, v->len, buf, sizeof(buf))) == -1)
      return;

   findent(f, indent);
   fprintf(f, "\"%s\": \"%.*s\",%c", k, len, buf, CCHAR);
}


static void fstring(FILE *f, const char *k, const char *v, int indent)
{
   char buf[strlen(v) * 2 + 2];
   int len;

   if ((len = jesc(v, strlen(v), buf, sizeof(buf))) == -1)
      return;


   findent(f, indent);
   fprintf(f, "\"%s\": \"%s\",%c", k, buf, CCHAR);
}


static void ftag1(FILE *f, const char *k, const bstring_t *b, int type, int indent)
{
   findent(f, indent);
   fprintf(f, "\"%s\": ", k);
   fochar(f, '{');
   fbstring(f, "str", b, indent);
   fstring(f, "op", op_str(type), indent);

   if (type & ~SPECIAL_MASK)
      fstring(f, "mod", mod_str(type), indent);

   funsep(f);
   findent(f, indent);
   fcchar(f, '}');
}


static void ftag(FILE *f, const struct otag *ot, const struct stag *st, int indent)
{
   ftag1(f, "k", &ot->k, st->stk.type, indent);
   ftag1(f, "v", &ot->v, st->stv.type, indent);
   funsep(f);
}


static void rule_info_tags(FILE *f, const smrule_t *r, int indent)
{
   findent(f, indent);
   fochar(f, '[');
   for (int i = 0; i < r->oo->tag_cnt; i++)
   {
      findent(f, indent + 1);
      fochar(f, '{');
      ftag(f, &r->oo->otag[i], &r->act->stag[i], indent + 2);
      findent(f, indent + 1);
      fcchar(f, '}');
   }
   funsep(f);
   findent(f, indent);
   fcchar(f, ']');
}


static void fparams(FILE *f, fparam_t **fp, int indent)
{
   findent(f, indent);
   fochar(f, '{');
   for (; *fp != NULL; fp++)
   {
      fstring(f, (*fp)->attr, (*fp)->val, indent + 1);
   }
   funsep(f);
   findent(f, indent);
   fcchar(f, '}');
}


static int rule_info(const smrule_t *r, const rinfo_t *ri)
{
   //safety check
   if (r == NULL || ri == NULL) return -1;

   if (r->oo->ver != ri->version)
      return 0;

   findent(ri->f, ri->indent);
   fochar(ri->f, '{');
   fstring(ri->f, "type", type_str(r->oo->type), ri->indent + 1);
   fint(ri->f, "version", r->oo->ver, ri->indent + 1);
   fint(ri->f, "id", r->oo->id, ri->indent + 1);
   if (r->act->func_name != NULL)
      fstring(ri->f, "action", r->act->func_name, ri->indent + 1);
   fint(ri->f, "visible", r->oo->vis, ri->indent + 1);

   if (r->act->fp != NULL)
   {
      findent(ri->f, ri->indent + 1);
      condensed_ = ri->condensed;
      fprintf(ri->f, "\"params\":%c", CCHAR);
      fparams(ri->f, r->act->fp, ri->indent + 1);
      condensed_ = 0;
      fprintf(ri->f, "\n");
   }

   if (r->oo->tag_cnt)
   {
      findent(ri->f, ri->indent + 1);
      condensed_ = ri->condensed;
      fprintf(ri->f, "\"tags\":%c", CCHAR);
      rule_info_tags(ri->f, r, ri->indent + 1);
      condensed_ = 0;
   }

   funsep(ri->f);
   findent(ri->f, ri->indent);
   fcchar(ri->f, '}');
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

   fprintf(ri->f, "{\n");
   findent(ri->f, 1);
   fochar(ri->f, '[');
   for (int i = 0; i < rstats->ver_cnt; i++)
   {
      log_msg(LOG_NOTICE, "saving pass %d (ver = %d)", i, rstats->ver[i]);
      ri->version = rstats->ver[i];
      ri->indent = 2;
      execute_rules0(rd->rules, (tree_func_t) rule_info, ri);
   }
   funsep(ri->f);
   findent(ri->f, 1);
   fprintf(ri->f, "]\n");
   fprintf(ri->f, "}\n");

   fclose(ri->f);
   return 0;
}

