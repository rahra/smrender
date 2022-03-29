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
 *  \date 2022/03/29
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <string.h>

#include "smrender_dev.h"
#include "smcore.h"


#define INDENT 3
#define CCHAR (condensed_ ? ' ' : '\n')


typedef struct rinfo
{
   int version;
   FILE *f;
} rinfo_t;


static int condensed_ = 0;
static int indent_ = 1;


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


static void fchar(FILE *f, char c)
{
   fprintf(f, "%c%c", c, CCHAR);
}


static void fint(FILE *f, const char *k, long v, int indent)
{
   findent(f, indent);
   fprintf(f, "\"%s\": %ld,%c", k, v, CCHAR);
}


static void fstring(FILE *f, const char *k, const char *v, int indent)
{
   findent(f, indent);
   fprintf(f, "\"%s\": \"%s\",%c", k, v, CCHAR);
}


static void ftag(FILE *f, const struct otag *ot, const struct stag *st, int indent)
{
   findent(f, indent);
   fprintf(f, "\"k\": {\"str\": \"%.*s\", \"op\": \"%s\"",
         ot->k.len, ot->k.buf, op_str(st->stk.type));
   if (st->stk.type & ~SPECIAL_MASK)
      fprintf(f, ", \"mod\": \"%s\"", mod_str(st->stk.type));
   fprintf(f, "},%c", CCHAR);
   findent(f, indent);
   fprintf(f, "\"v\": {\"str\": \"%.*s\", \"op\": \"%s\"",
         ot->v.len, ot->v.buf, op_str(st->stv.type));
   if (st->stv.type & ~SPECIAL_MASK)
      fprintf(f, ", \"mod\": \"%s\"", mod_str(st->stv.type));
   fprintf(f, "}%c", CCHAR);
}


static void rule_info_tags(FILE *f, const smrule_t *r, int indent)
{
   findent(f, indent);
   fchar(f, '[');
   for (int i = 0; i < r->oo->tag_cnt; i++)
   {
      findent(f, indent + 1);
      fchar(f, '{');
      ftag(f, &r->oo->otag[i], &r->act->stag[i], indent + 2);
      findent(f, indent + 1);
      fprintf(f, "},%c", CCHAR);
   }
   funsep(f);
   findent(f, indent);
   fprintf(f, "]\n");
}


static void fparams(FILE *f, fparam_t **fp, int indent)
{
   findent(f, indent);
   fprintf(f, "{%c", CCHAR);
   for (; *fp != NULL; fp++)
   {
      fstring(f, (*fp)->attr, (*fp)->val, indent + 1);
   }
   funsep(f);
   findent(f, indent);
   fprintf(f, "},\n");
}


static int rule_info(const smrule_t *r, const rinfo_t *ri)
{
   int indent = 4;

   //safety check
   if (r == NULL || ri == NULL) return -1;

   if (r->oo->ver != ri->version)
      return 0;

   findent(ri->f, indent);
   fprintf(ri->f, "{\n");
   fstring(ri->f, "type", type_str(r->oo->type), indent + 1);
   fint(ri->f, "id", r->oo->id, indent + 1);
   fstring(ri->f, "name", r->act->func_name, indent + 1);
   if (r->act->func_name == NULL) fstring(ri->f, "note", "template", indent + 1);
   fint(ri->f, "visible", r->oo->vis, indent + 1);
   
   findent(ri->f, indent + 1);

   condensed_ = 1;
   if (r->act->fp != NULL)
   {
      fprintf(ri->f, "\"params\":%c", CCHAR);
      fparams(ri->f, r->act->fp, indent + 1);
   }

   condensed_ = 0;
   findent(ri->f, indent + 1);

   condensed_ = 1;
   fprintf(ri->f, "\"tags\":%c", CCHAR);
   rule_info_tags(ri->f, r, indent + 1);
   condensed_ = 0;

   findent(ri->f, indent);
   fprintf(ri->f, "},\n");
   return 0;
}


int rules_info(const struct rdata *rd, const char *fname, const struct dstats *rstats)
{
   rinfo_t ri;

   // safety check
   if (fname == NULL || rstats == NULL)
   {
      log_msg(LOG_EMERG, "{fname|rstats} == NULL");
      return -1;
   }

   if ((ri.f = fopen(fname, "w")) == NULL)
   {
      log_errno(LOG_ERR, "fopen() failed");
      return -1;
   }

   fprintf(ri.f, "{\n");
   for (int i = 0; i < rstats->ver_cnt; i++)
   {
      log_msg(LOG_NOTICE, "saving pass %d (ver = %d)", i, rstats->ver[i]);
      ri.version = rstats->ver[i];
      findent(ri.f, 1);
      fchar(ri.f, '[');
      findent(ri.f, 2);
      fchar(ri.f, '{');
      findent(ri.f, 3);
      fprintf(ri.f, "\"version\": %d,\n", ri.version);
      findent(ri.f, 3);
      fprintf(ri.f, "\"obj\":\n");
      findent(ri.f, 3);
      fchar(ri.f, '[');
      execute_rules0(rd->rules, (tree_func_t) rule_info, &ri);
      funsep(ri.f);
      findent(ri.f, 3);
      fprintf(ri.f, "]\n");
      findent(ri.f, 2);
      fprintf(ri.f, "}\n");
      findent(ri.f, 1);
      fprintf(ri.f, "],\n");
   }
   funsep(ri.f);
   fprintf(ri.f, "}\n");

   fclose(ri.f);
   return 0;
}

