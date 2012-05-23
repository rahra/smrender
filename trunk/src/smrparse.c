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
#include <ctype.h>

#include "smrender_dev.h"
#include "smlog.h"
#include "smrparse.h"


//#define RULE_COUNT 8
//static const char *rule_type_[] = {"N/A", "ACT_IMG", "ACT_CAP", "ACT_FUNC", "ACT_DRAW", "ACT_IGNORE", "ACT_OUTPUT", "ACT_SETTAGS"};

/*
const char *rule_type_str(int n)
{
   if ((n < 0) || (n >= RULE_COUNT))
      n = 0;

   return rule_type_[n];
}
*/

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
      else if ((b->buf[0] == ']') && (b->buf[b->len - 1] == '['))
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
      else if ((b->buf[0] == '[') && (b->buf[b->len - 1] == ']'))
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
   long c;

   if (*s == '#')
   {
      s++;
      if (strlen(s) != 6)
      {
         log_msg(LOG_WARN, "format error in HTML color '#%s'", s);
         return rd->col[BLACK];
      }

      errno = 0;
      c = strtol(s, NULL, 16);
      if (errno)
      {
         log_msg(LOG_WARN, "cannot convert HTML color '#%s': %s", s, strerror(errno));
         return rd->col[BLACK];
      }

      return gdImageColorResolve(rd->img, (c >> 16) & 0xff, (c >> 8) & 0xff, c & 0xff);
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
   if (!strcmp(s, "transparent"))
      return rd->col[TRANSPARENT];

   log_msg(LOG_WARN, "unknown color %s, defaulting to black", s);
   return rd->col[BLACK];
}


int parse_style(const char *s)
{
   if (s == NULL)
      return DRAW_SOLID;

   if (!strcmp(s, "solid")) return DRAW_SOLID;
   if (!strcmp(s, "dashed")) return DRAW_DASHED;
   if (!strcmp(s, "dotted")) return DRAW_DOTTED;
   if (!strcmp(s, "transparent")) return DRAW_TRANSPARENT;

   return DRAW_SOLID;
}

#if 0
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

   ds->style = parse_style(s);

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


struct orule *rule_alloc(struct rdata *rd, osm_obj_t *o)
{
   bx_node_t *bn;
   struct orule *rl;

   if ((rl = malloc(sizeof(struct orule) + sizeof(struct stag) * o->tag_cnt)) == NULL)
      log_msg(LOG_ERR, "rule_alloc failed: %s", strerror(errno)),
         exit(EXIT_FAILURE);
   memset(&rl->rule, 0, sizeof(struct rule));
   rl->oo = o;
   rl->rule.tag_cnt = o->tag_cnt;

   if ((bn = bx_get_node(rd->rules, o->id)) == NULL)
      log_msg(LOG_EMERG, "bx_get_node() returned NULL in rule_alloc()"),
         exit(EXIT_FAILURE);

   bn->next[o->type - 1] = rl;
   return rl;
}
#endif


int get_structor(void *lhandle, structor_t *stor, const char *sym, const char *trail)
{
   char buf[strlen(sym) + strlen(trail) + 5];

   snprintf(buf, sizeof(buf), "act_%s%s", sym, trail);
   // Clear any existing error
   dlerror();
   stor->sym = dlsym(lhandle, buf);
   // Check for errors (BSD returns const char*, thus type is converted)
   if (dlerror() == NULL)
      return 0;

   log_msg(LOG_INFO, "no structor %s()", buf);
   return -1;
}


smrule_t *alloc_rule(struct rdata *rd, osm_obj_t *o)
{
   bx_node_t *bn;
   smrule_t *rl;

   if ((rl = malloc(sizeof(smrule_t) + sizeof(action_t) + sizeof(struct stag) * o->tag_cnt)) == NULL)
      log_msg(LOG_ERR, "alloc_rule failed: %s", strerror(errno)),
         exit(EXIT_FAILURE);
   rl->act = (action_t*) (rl + 1);
   //memset(&rl->act, 0, sizeof(action_t));
   //rl->oo = o;
   //rl->act->tag_cnt = o->tag_cnt;

   if ((bn = bx_get_node(rd->rules, o->id)) == NULL)
      log_msg(LOG_EMERG, "bx_get_node() returned NULL in rule_alloc()"),
         exit(EXIT_FAILURE);

   bn->next[o->type - 1] = rl;
   return rl;
}


char *skipb(char *s)
{
   for (; isspace(*s); s++);
   if (*s == '\0')
      return NULL;
   return s;
}


int init_rules(osm_obj_t *o, struct rdata *rd, void *p)
{
   char *s, *t, *func, buf[1024];
   smrule_t *rl;
   //action_t act;
   int e, i;

   log_debug("initializing rule 0x%016lx", o->id);

   rl = alloc_rule(rd, o);
   rl->oo = o;
   rl->data = NULL;
   memset(rl->act, 0, sizeof(*rl->act));

   rl->act->tag_cnt = o->tag_cnt;
   for (i = 0; i < o->tag_cnt; i++)
   {
      if (parse_matchtype(&o->otag[i].k, &rl->act->stag[i].stk) == -1)
         return 0;
      if (parse_matchtype(&o->otag[i].v, &rl->act->stag[i].stv) == -1)
         return 0;
   }

   if ((i = match_attr(o, "_action_", NULL)) == -1)
   {
      // FIXME need to be added to btree
      log_msg(LOG_WARN, "rule %ld has no action, it may be used as template", o->id);
      rl->act->func_name = "templ";
      rl->act->main.func = act_templ;
      return 0;
   }

   o->otag[i].v.buf[o->otag[i].v.len] = '\0';
   log_msg(LOG_DEBUG, "parsing '%s'", o->otag[i].v.buf);

   if ((s = skipb(o->otag[i].v.buf)) == NULL)
   {
      log_msg(LOG_WARN, "empty _action_ value");
      return 1;
   }

   rl->act->func_name = s;
   if ((t = strpbrk(s, "@:")) != NULL)
   {
      s = t + 1;
      if (*t == '@')
      {
         if ((t = strchr(s, ':')) != NULL)
         {
            *t = '\0';
            rl->act->parm = t + 1;
         }

         // Open shared library
         if ((rl->act->libhandle = dlopen(s, RTLD_LAZY)) == NULL)
         {
            log_msg(LOG_ERR, "could not open library: %s", dlerror());
            return 1;
         }
      }
      else
      {
         *t = '\0';
         rl->act->parm = t + 1;
      }
   }

   if (rl->act->libhandle != NULL)
   {
      strncpy(buf, rl->act->func_name, sizeof(buf));
      buf[sizeof(buf) - 1] = '\0';
      if ((func = strtok(buf, "@")) == NULL)
      {
         log_msg(LOG_CRIT, "strtok() returned NULL");
         return 1;
      }
   }
   else
      func = rl->act->func_name;

   if (get_structor(rl->act->libhandle, (structor_t*) &rl->act->main, func, ""))
      return 1;

   (void) get_structor(rl->act->libhandle, (structor_t*) &rl->act->ini, func, "_ini");
   (void) get_structor(rl->act->libhandle, (structor_t*) &rl->act->fini, func, "_fini");

   if (rl->act->parm != NULL)
      rl->act->fp = parse_fparam(rl->act->parm);

   // finally call initialization function
   if (rl->act->ini.func != NULL)
   {
      log_msg(LOG_DEBUG, "calling %s_ini()", rl->act->func_name);
      e = rl->act->ini.func(rl);
      if (e < 0)
      {
         log_msg(LOG_ERR, "%s_ini() failed: %d. Exiting.", rl->act->func_name, e);
         return e;
      }
      if (e > 0)
      {
         log_msg(LOG_ERR, "%s_ini() failed: %d. Rule will be ignored.", rl->act->func_name, e);
         rl->act->main.func = NULL;
         rl->act->fini.func = NULL;
         return e;
      }
   }

   // remove _action_ tag from tag list, i.e. move last element
   // to position of _action_ tag (order doesn't matter).
   if (i < rl->oo->tag_cnt - 1)
   {
      memmove(&rl->oo->otag[i], &rl->oo->otag[rl->oo->tag_cnt - 1], sizeof(struct otag));
      memmove(&rl->act->stag[i], &rl->act->stag[rl->oo->tag_cnt - 1], sizeof(struct stag));
   }
   rl->oo->tag_cnt--;
   rl->act->tag_cnt--;

   return 0;
}
 

void free_fparam(fparam_t **fp)
{
   fparam_t **fp0 = fp;

   // free string buffer (should be first attribute);
   if (*fp != NULL)
      free((*fp)->attr);

   // free fparam_t's
   for (; *fp != NULL; fp++)
      free(*fp);

   // free pointer list
   free(fp0);
}


/*! This function parses a string of the format "key1=val1,key2=val2,..." into a fparam_t* list.
 *  @param parm A pointer to the original string. Please not that the string is
 *  tokenized using strtok_r(), thus '\0' characters are inserted. If the
 *  original string is needed for something else it should be strdup()'ed
 *  before.
 *  @return A pointer to a fparam_t* list or NULL in case of error.
 */
fparam_t **parse_fparam(char *parm)
{
   fparam_t **fp, **fp0;
   char *s, *sp0, *sp1;
   int cnt;

   if ((fp = malloc(sizeof(fparam_t*))) == NULL)
   {
      log_msg(LOG_ERR, "malloc failed: %s", strerror(errno));
      return NULL;
   }
   *fp = NULL;

   for (s = strtok_r(parm, ";", &sp0), cnt = 0; s != NULL; s = strtok_r(NULL, ";", &sp0), cnt++)
   {
      if ((fp0 = realloc(fp, sizeof(fparam_t*) * (cnt + 2))) == NULL)
      {
         log_msg(LOG_ERR, "realloc in parse_fparam failed: %s", strerror(errno));
         break;
      }
      fp = fp0;
      if ((fp[cnt] = malloc(sizeof(fparam_t))) == NULL)
      {
         log_msg(LOG_ERR, "malloc in parse_fparam failed: %s", strerror(errno));
         break;
      }
      memset(fp[cnt], 0, sizeof(fparam_t));

      fp[cnt]->attr = strtok_r(s, "=", &sp1);
      if ((fp[cnt]->val = strtok_r(NULL, "=", &sp1)) != NULL)
         fp[cnt]->dval = atof(fp[cnt]->val);
      fp[cnt + 1] = NULL;
   }

   return fp;
}


char *get_param(const char *attr, double *dval, const action_t *act)
{
   fparam_t **fp;

   if ((act == NULL) || (act->fp == NULL) || (attr == NULL))
      return NULL;

   for (fp = act->fp; *fp != NULL; fp++)
   {
      if (!strcmp(attr, (*fp)->attr))
      {
         if (dval != NULL)
            *dval = (*fp)->dval;
         return (*fp)->val;
      }
   }
   return NULL;
}

