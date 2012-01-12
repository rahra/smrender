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

/*! This file contains the code of the rule parser and main loop of the render
 * as well as the code for traversing the object (nodes/ways) tree.
 *
 *  @author Bernhard R. Fischer
 */
#include <string.h>
#include <errno.h>
#include <dlfcn.h>

#include "smrender.h"
#include "smlog.h"
#include "smrparse.h"


#define RULE_COUNT 7
static const char *rule_type_[] = {"N/A", "ACT_IMG", "ACT_CAP", "ACT_FUNC", "ACT_DRAW", "ACT_IGNORE", "ACT_OUTPUT"};


const char *rule_type_str(int n)
{
   if ((n < 0) || (n >= RULE_COUNT))
      n = 0;

   return rule_type_[n];
}


#if 0
/*! Returns degrees and minutes of a fractional coordinate.
 */
void fdm(double x, int *deg, int *min)
{
   double d;

   *min = round(modf(x, &d) * 60);
   *deg = round(d);
   if (*min == 60)
   {
      (*deg)++;
      *min = 0;
   }
}


double fround(double x, double y)
{
   return x - fmod(x, y);
}


char *cfmt(double c, int d, char *s, int l)
{
   // FIXME: modf should be used instead
   switch (d)
   {
      case LAT:
         snprintf(s, l, "%02.0f %c %1.2f", fabs(c), c < 0 ? 'S' : 'N', (c - floor(c)) * 60.0);
         break;

      case LON:
         snprintf(s, l, "%03.0f %c %1.2f", fabs(c), c < 0 ? 'W' : 'E', (c - floor(c)) * 60.0);
         break;

      default:
         *s = '\0';
   }
   return s;
}
#endif


int parse_matchtype(bstring_t *b, struct specialTag *t)
{
   t->type = 0;

   if (b->len > 2)
   {
      if ((b->buf[0] == '!') && (b->buf[b->len - 1] == '!'))
      {
         b->buf[b->len - 1] = '\0';
         b->buf++;
         b->len -= 2;
         t->type |= SPECIAL_INVERT;
      }
      else if ((b->buf[0] == '~') && (b->buf[b->len - 1] == '~'))
      {
         b->buf[b->len - 1] = '\0';
         b->buf++;
         b->len -= 2;
         t->type |= SPECIAL_NOT;
      }
 
   }

   if (b->len > 2)
   {
      if ((b->buf[0] == '/') && (b->buf[b->len - 1] == '/'))
      {
         log_debug("seems to be regex: '%.*s' (%d, %c)", b->len, b->buf, b->len, b->buf[b->len - 1]);
         b->buf[b->len - 1] = '\0';
         b->buf++;
         b->len -= 2;

         if (regcomp(&t->re, b->buf, REG_EXTENDED | REG_NOSUB))
         {
            log_msg(LOG_WARN, "failed to compile regex '%s'", b->buf);
            return -1;
         }
         t->type |= SPECIAL_REGEX;
      }
      else if ((b->buf[0] == '>') && (b->buf[b->len - 1] == '>'))
      {
         log_debug("parsing GT rule");
         b->buf[b->len - 1] = '\0';
         b->buf++;
         b->len -= 2;
         errno = 0;
         t->val = strtod(b->buf, NULL);
         if (errno)
            log_msg(LOG_ERR, "failed to convert value of GT rule: %s", strerror(errno));
         else
            t->type |= SPECIAL_GT;
      }
      else if ((b->buf[0] == '<') && (b->buf[b->len - 1] == '<'))
      {
         log_debug("parsing LT rule");
         b->buf[b->len - 1] = '\0';
         b->buf++;
         b->len -= 2;
         errno = 0;
         t->val = strtod(b->buf, NULL);
         if (errno)
            log_msg(LOG_ERR, "failed to convert value of LT rule: %s", strerror(errno));
         else
            t->type |= SPECIAL_LT;
      }
   }

   return 0;
}


short ppos(const char *s)
{
   char c[] = "nsmewc";
   int p[] = {POS_N, POS_S, POS_M, POS_E, POS_W, POS_C};
   int i;
   short pos = 0;

   for (i = 0; i < strlen(c); i++)
      if (strchr(s, c[i]) != NULL)
         pos |= p[i];

   return pos;
}


int parse_color(const struct rdata *rd, const char *s)
{
   if (*s == '#')
   {
      log_msg(LOG_WARN, "HTML color style (%s) not supported yet, defaulting to black", s);
      return rd->col[BLACK];
   }
   if (!strcmp(s, "white"))
      return rd->col[WHITE];
   if (!strcmp(s, "yellow"))
      return rd->col[YELLOW];
   if (!strcmp(s, "black"))
      return rd->col[BLACK];
   if (!strcmp(s, "blue"))
      return rd->col[BLUE];
   if (!strcmp(s, "magenta"))
      return rd->col[MAGENTA];
   if (!strcmp(s, "brown"))
      return rd->col[BROWN];

   log_msg(LOG_WARN, "unknown color %s, defaulting to black", s);
   return rd->col[BLACK];
}


int parse_draw(const char *src, struct drawStyle *ds, const struct rdata *rd)
{
   char buf[strlen(src) + 1];
   char *s, *sb;

   strcpy(buf, src);
   if ((s = strtok_r(buf, ",", &sb)) == NULL)
   {
      log_msg(LOG_WARN, "syntax error in draw rule %s", src);
      return -1;
   }

   ds->col = parse_color(rd, s);

   if ((s = strtok_r(NULL, ",", &sb)) == NULL)
      return 0;

   ds->width = atof(s);

   if ((s = strtok_r(NULL, ",", &sb)) == NULL)
      return 0;

   if (!strcmp(s, "solid")) ds->style = DRAW_SOLID;
   else if (!strcmp(s, "dashed")) ds->style = DRAW_DASHED;
   else if (!strcmp(s, "dotted")) ds->style = DRAW_DOTTED;
   else if (!strcmp(s, "transparent")) ds->style = DRAW_TRANSPARENT;

   //log_msg(LOG_WARN, "draw width and styles are not parsed yet (sorry...)");
   return 0;
}


int parse_auto_rot(struct rdata *rd, const char *str, struct auto_rot *rot)
{
   char buf[strlen(str) + 1], *s, *b;

   strcpy(buf, str);
   rot->autocol = rd->col[WHITE];
   rot->weight = 1;
   rot->phase = 0;

   // first part contains "auto"
   if ((s = strtok_r(buf, ";", &b)) == NULL) return 0;
   if ((s = strtok_r(NULL, ";", &b)) == NULL) return 0;

   rot->autocol = parse_color(rd, s);

   if ((s = strtok_r(NULL, ";", &b)) == NULL) return 0;

   rot->weight = atof(s);

   if ((s = strtok_r(NULL, ";", &b)) == NULL) return 0;

   rot->phase = atof(s);

   return 0;
}


struct orule *rule_alloc(struct rdata *rd, struct onode *nd)
{
   bx_node_t *bn;
   struct orule *rl;

   if ((rl = malloc(sizeof(struct orule) + sizeof(struct stag) * nd->tag_cnt)) == NULL)
      log_msg(LOG_ERR, "rule_alloc failed: %s", strerror(errno)),
         exit(EXIT_FAILURE);
   memset(&rl->rule, 0, sizeof(struct rule));
   rl->ond = nd;
   rl->rule.tag_cnt = nd->tag_cnt;

   if ((bn = bx_get_node(rd->rules, nd->nd.id)) == NULL)
      log_msg(LOG_EMERG, "bx_get_node() returned NULL in rule_alloc()"),
         exit(EXIT_SUCCESS);

   bn->next[nd->nd.type == OSM_WAY] = rl;
   return rl;
}


int prepare_rules(struct onode *nd, struct rdata *rd, void *p)
{
   char *s, *lib;
   FILE *f;
   int i;
   struct orule *rl;

   rl = rule_alloc(rd, nd);

   for (i = 0; i < rl->ond->tag_cnt; i++)
   {
      if (parse_matchtype(&rl->ond->otag[i].k, &rl->rule.stag[i].stk) == -1)
         return 0;
      if (parse_matchtype(&rl->ond->otag[i].v, &rl->rule.stag[i].stv) == -1)
         return 0;
   }

   if ((i = match_attr(rl->ond, "_action_", NULL)) == -1)
   {
      log_msg(LOG_WARN, "rule %ld has no action", rl->ond->nd.id);
      return 0;
   }

   rl->ond->otag[i].v.buf[rl->ond->otag[i].v.len] = '\0';
   s = strtok(rl->ond->otag[i].v.buf, ":");
   if (!strcmp(s, "img"))
   {
      if ((s = strtok(NULL, ":")) == NULL)
         return E_SYNTAX;
      if ((f = fopen(s, "r")) == NULL)
      {
         log_msg(LOG_WARN, "fopen(%s) failed: %s", s, strerror(errno));
         return E_SYNTAX;
      }

      rl->rule.img.angle = 0;
      if ((rl->rule.img.img = gdImageCreateFromPng(f)) == NULL)
         log_msg(LOG_WARN, "could not read PNG from %s", s);
      (void) fclose(f);

      rl->rule.type = ACT_IMG;
      log_debug("successfully imported PNG %s", s);
   }
   else if (!strcmp(s, "img-auto"))
   {
      if ((s = strtok(NULL, ":")) == NULL)
         return E_SYNTAX;
      if ((f = fopen(s, "r")) == NULL)
      {
         log_msg(LOG_WARN, "fopen(%s) failed: %s", s, strerror(errno));
         return 0;
      }

      rl->rule.img.angle = NAN;
      if ((rl->rule.img.img = gdImageCreateFromPng(f)) == NULL)
         log_msg(LOG_WARN, "could not read PNG from %s\n", s);
      (void) fclose(f);

      rl->rule.type = ACT_IMG;
      log_debug("img-auto, successfully imported PNG %s", s);
   }
   else if (!strcmp(s, "cap"))
   {
      if ((s = strtok(NULL, ",")) == NULL) return E_SYNTAX;
      rl->rule.cap.font = s;
      if ((s = strtok(NULL, ",")) == NULL) return E_SYNTAX;
      rl->rule.cap.size = atof(s);
      if ((s = strtok(NULL, ",")) == NULL) return E_SYNTAX;
      rl->rule.cap.pos = ppos(s);
      if ((s = strtok(NULL, ",")) == NULL) return E_SYNTAX;

      rl->rule.cap.col = parse_color(rd, s);

      if ((s = strtok(NULL, ",")) == NULL) return E_SYNTAX;
      if (!strncmp(s, "auto", 4))
      {
         rl->rule.cap.angle = NAN;
         parse_auto_rot(rd, s, &rl->rule.cap.rot);
         log_debug("auto;%08x;%.1f;%.1f", rl->rule.cap.rot.autocol, rl->rule.cap.rot.weight, rl->rule.cap.rot.phase);
      }
      else
         rl->rule.cap.angle = atof(s);
      if ((s = strtok(NULL, ",")) == NULL) return E_SYNTAX;

      rl->rule.cap.key = s;
      rl->rule.type = ACT_CAP;
      log_debug("successfully parsed caption rule");
   }
   else if (!strcmp(s, "func"))
   {
      if ((s = strtok(NULL, "@")) == NULL)
      {
         log_msg(LOG_ERR, "syntax error in function rule");
         return E_SYNTAX;
      }
      if ((lib = strtok(NULL, "")) == NULL)
      {
         log_msg(LOG_ERR, "syntax error in function rule");
         return E_SYNTAX;
      }

      // Open shared library
      if ((rl->rule.func.libhandle = dlopen(lib, RTLD_LAZY)) == NULL)
      {
         log_msg(LOG_ERR, "could not open library: %s", dlerror());
         return 0;
      }

      // Clear any existing error
      dlerror();

      rl->rule.func.sym = dlsym(rl->rule.func.libhandle, s);

      // Check for errors (BSD returns const char*, thus type is converted)
      if ((s = (char*) dlerror()) != NULL)
      {
         log_msg(LOG_ERR, "error loading symbol from libary: %s", s);
         return 0;
      }

      rl->rule.type = ACT_FUNC;
      log_debug("successfully parsed function rule");
   }
   else if (!strcmp(s, "draw"))
   {
      if ((s = strtok(NULL, "")) == NULL)
      {
         log_warn("syntax error in draw rule");
         return E_SYNTAX;
      }

      if (*s != ':')
      {
         s = strtok(s, ":");
         if (parse_draw(s, &rl->rule.draw.fill, rd) == -1)
            return E_SYNTAX;
         rl->rule.draw.fill.used = 1;
         if ((s = strtok(NULL, ":")) != NULL)
         {
            if (!parse_draw(s, &rl->rule.draw.border, rd))
               rl->rule.draw.border.used = 1;
         }
      }
      else
      {
         if (strlen(s) <= 1)
         {
            log_warn("syntax error in draw rule");
            return E_SYNTAX;
         }
         if (!parse_draw(s + 1, &rl->rule.draw.border, rd))
            rl->rule.draw.border.used = 1;
      }

      rl->rule.type = ACT_DRAW;
      log_debug("successfully parsed draw rule");
   }
   else if (!strcmp(s, "out"))
   {
      if ((s = strtok(NULL, "")) == NULL)
      {
         log_warn("syntax error in out rule");
         return E_SYNTAX;
      }
      if ((rl->rule.out.fhandle = fopen(s, "w")) == NULL)
      {
         log_msg(LOG_ERR, "error opening output file: %s", s);
         return 0;
      }
      fprintf(rl->rule.out.fhandle, "<?xml version='1.0' encoding='UTF-8'?>\n<osm version='0.6' generator='smrender'>\n");
      rl->rule.type = ACT_OUTPUT;
      log_debug("successfully parsed output rule");
   }
   else if (!strcmp(s, "ignore"))
   {
      rl->rule.type = ACT_IGNORE;
   }
   else
   {
      log_warn("action type '%s' not supported yet", s);
   }

   // remove _action_ tag from tag list, i.e. move last element
   // to position of _action_ tag (order doesn't matter).
   if (i < rl->ond->tag_cnt - 1)
   {
      memmove(&rl->ond->otag[i], &rl->ond->otag[rl->ond->tag_cnt - 1], sizeof(struct otag));
      memmove(&rl->rule.stag[i], &rl->rule.stag[rl->ond->tag_cnt - 1], sizeof(struct stag));
   }
   rl->ond->tag_cnt--;
   rl->rule.tag_cnt--;

   return 0;
}
