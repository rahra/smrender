#include <stdio.h>
#include <syslog.h>
#include <string.h>
#include <errno.h>


#include "smrender.h"


//#define TBUFLEN 24
#define SEAMARK_LIGHT_CHARACTER "seamark:light_character"


static const double altr_[] = {0.005, 0.005, 0.01, 0.005};
static const char *col_[] = {"white", "red", "green", "yellow", "orange", "blue", "violet", "amber", NULL};
static const char *col_abbr_[] = {"W", "R", "G", "Y", "Or", "Bu", "Vi", "Am", NULL};
static const int col_cnt_ = 8;
//static const char *atype_[] = {"undef", "solid", "suppress", "dashed", NULL};


int parse_color(bstring_t b)
{
   int i;

   for (i = 0; col_[i] != NULL; i++)
      if (!bs_cmp(b, col_[i]))
         return i;
   return -1;
}


int pchar(struct onode *nd, struct rdata *rd)
{
   char lchar[8] = "", group[8] = "", period[8] = "", range[8] = "", col[8] = "", buf[256];
   int n;
   struct onode *node;

   //if ((tm = gmtime(&nd->nd.tim)) != NULL)
   //   strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%SZ", tm);

   if ((n = rd->cb.match_attr(nd, "seamark:light:group", NULL)) != -1)
      snprintf(group, sizeof(group), "(%.*s)", nd->otag[n].v.len, nd->otag[n].v.buf);
   if ((n = rd->cb.match_attr(nd, "seamark:light:period", NULL)) != -1)
      snprintf(period, sizeof(period), " %.*ss", nd->otag[n].v.len, nd->otag[n].v.buf);
   if ((n = rd->cb.match_attr(nd, "seamark:light:range", NULL)) != -1)
      snprintf(range, sizeof(range), " %.*sM", nd->otag[n].v.len, nd->otag[n].v.buf);
   if ((n = rd->cb.match_attr(nd, "seamark:light:character", NULL)) != -1)
      snprintf(lchar, sizeof(lchar), "%.*s%s", nd->otag[n].v.len, nd->otag[n].v.buf, group[0] == '\0' ? "." : "");
   if ((n = rd->cb.match_attr(nd, "seamark:light:colour", NULL)) != -1)
   {
      if ((n = parse_color(nd->otag[n].v)) != -1)
         snprintf(col, sizeof(col), "%s.",  col_abbr_[n]);
   }

   if (!snprintf(buf, sizeof(buf), "%s%s%s%s%s", lchar, group, col, period, range))
      return 0;
    
   if ((node = realloc(nd, sizeof(struct onode) + sizeof(struct otag) * (nd->tag_cnt + 1) + sizeof(int64_t) * nd->ref_cnt)) == NULL)
   {
      rd->cb.log_msg(LOG_ERR, "could not realloc new node: %s", strerror(errno));
      return -1;
   }

   // check of realloc changed address
   if (node != nd)
   {
      nd = node;
      (void) rd->cb.put_object(rd->nodes, nd->nd.id, nd);
   }

   // clear additional otag structure
   memset(&nd->otag[nd->tag_cnt], 0, sizeof(struct otag));
   if ((nd->otag[nd->tag_cnt].v.buf = strdup(buf)) == NULL)
   {
      rd->cb.log_msg(LOG_ERR, "cannot strdup: %s", strerror(errno));
      return -1;
   }
   nd->otag[nd->tag_cnt].v.len = strlen(buf);
   nd->otag[nd->tag_cnt].k.buf = SEAMARK_LIGHT_CHARACTER;
   nd->otag[nd->tag_cnt].k.len = strlen(SEAMARK_LIGHT_CHARACTER);
   nd->tag_cnt++;

   return 0;
}

