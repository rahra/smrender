#include <stdio.h>
#include <syslog.h>
#include <string.h>
#include <errno.h>


#include "smrender.h"
#include "seamark.h"


//#define TBUFLEN 24
#define SEAMARK_LIGHT_CHARACTER "seamark:light_character"


static const double altr_[] = {0.005, 0.005, 0.01, 0.005};
static const char *col_[] = {"white", "red", "green", "yellow", "orange", "blue", "violet", "amber", NULL};
static const char *col_abbr_[] = {"W", "R", "G", "Y", "Or", "Bu", "Vi", "Am", NULL};
static const int col_cnt_ = 8;
//static const char *atype_[] = {"undef", "solid", "suppress", "dashed", NULL};
static int untagged_circle_ = 0;


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

   if ((n = match_attr(nd, "seamark:light:group", NULL)) != -1)
      snprintf(group, sizeof(group), "(%.*s)", nd->otag[n].v.len, nd->otag[n].v.buf);
   if ((n = match_attr(nd, "seamark:light:period", NULL)) != -1)
      snprintf(period, sizeof(period), " %.*ss", nd->otag[n].v.len, nd->otag[n].v.buf);
   if ((n = match_attr(nd, "seamark:light:range", NULL)) != -1)
      snprintf(range, sizeof(range), " %.*sM", nd->otag[n].v.len, nd->otag[n].v.buf);
   if ((n = match_attr(nd, "seamark:light:character", NULL)) != -1)
      snprintf(lchar, sizeof(lchar), "%.*s%s", nd->otag[n].v.len, nd->otag[n].v.buf, group[0] == '\0' ? "." : "");
   if ((n = match_attr(nd, "seamark:light:colour", NULL)) != -1)
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
      //(void) rd->cb.put_object(rd->nodes, nd->nd.id, nd);
      (void) put_object(rd->nodes, nd->nd.id, nd);
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


void sort_sectors(struct sector *sec, int n)
{
   struct sector ss;
   int i, j;

   for (j = 1; j < n - 1; j++)
      for (i = 0; i < n - j; i++)
      {
         if (sec[i].mean > sec[i + 1].mean)
         {
            memcpy(&ss, &sec[i], sizeof(struct sector));
            memcpy(&sec[i], &sec[i + 1], sizeof(struct sector));
            memcpy(&sec[i + 1], &ss, sizeof(struct sector));
         }
      }
}


int vsector(struct onode *ond, struct rdata *rd)
{
   int i, j, n, k;
   struct sector sec[MAX_SEC];
   struct osm_node *nd = &ond->nd;
   bstring_t b = {0, ""};

   for (i = 0; i < MAX_SEC; i++)
      init_sector(&sec[i]);

   if (!(i = get_sectors(rd, ond, sec, MAX_SEC)))
      return 0;

   for (i = 0, n = 0; i < MAX_SEC; i++)
   {
      // check all parsed sectors for its validity and remove
      // illegal sectors
      if (sec[i].used)
      {
         // Skip 0 degree sector if it is a directional
         // light. Such definitions are incorrect and have
         // been accidently imported with the LoL import.
         if (i && (sec[i].start == sec[i].end) && (sec[i].start == sec[0].dir))
         {
            rd->cb.log_msg(LOG_INFO, "deprecated feature: %d:sector_start == %d:sector_end == orientation (node %ld)", sec[i].nr, sec[i].nr, nd->id);
            sec[i].used = 0;
            continue;
         }

         if ((!isnan(sec[i].dir) && (sec[i].cat != CAT_DIR)) ||
              (isnan(sec[i].dir) && (sec[i].cat == CAT_DIR)))
         {
            rd->cb.log_msg(LOG_WARNING, "sector %d has incomplete definition of directional light (node %ld)", sec[i].nr, nd->id);
            sec[i].dir = NAN;
            sec[i].cat = 0;
            sec[i].used = 0;
            continue;
         }
         if (isnan(sec[i].start) && isnan(sec[i].end))
         {
            if (sec[i].cat == CAT_DIR)
            {
               sec[i].start = sec[i].end = sec[i].dir;
            }
            else if (untagged_circle_)
            {
               sec[i].start = 0.0;
               sec[i].end = 360.0;
            }
            else
            {
               rd->cb.log_msg(LOG_WARNING, "sector %d of node %ld seems to lack start/end angle", sec[i].nr, nd->id);
               sec[i].used = 0;
               continue;
            }
         }
         else if (isnan(sec[i].start) || isnan(sec[i].end))
         {
            rd->cb.log_msg(LOG_WARNING, "sector %d of node %ld has either no start or no end angle!", sec[i].nr, nd->id);
            sec[i].used = 0;
            continue;
         }

         if (sec[i].start > sec[i].end)
            sec[i].end += 360;

         // increase counter for valid sectors
         n++;
      } // if (sec[i].used)
   } // for (i = 0; i < MAX_SEC; i++)

   // remove all unused (or invalid) sectors
   //rd->cb.log_msg(LOG_DEBUG, "removing unused sectors");
   for (i = 0, j = 0; i < MAX_SEC && j < n; i++, j++)
   {
      if (sec[i].used)
      {
         sec[i].mean = (sec[i].start + sec[i].end) / 2;
         continue;
      }
      memcpy(&sec[i], &sec[i + 1], sizeof(struct sector) * (MAX_SEC - i - 1));
      init_sector(&sec[MAX_SEC - 1]);
      i--;
      j--;
   }

   // sort sectors ascending on der mean angle
   sort_sectors(&sec[0], n);

   sec[n - 1].espace = sec[0].sspace = sec[0].start - sec[n - 1].end;
   for (i = 0; i < n - 1; i++)
      sec[i].espace = sec[i + 1].sspace = sec[i + 1].start - sec[i].end;

   // render sectors
   for (i = 0; i < MAX_SEC; i++)
   {
      if (sec[i].used)
      {
         if (proc_sfrac(&sec[i]) == -1)
         {
            rd->cb.log_msg(LOG_WARNING, "negative angle definition is just allowed in last segment! (sector %d node %ld)", sec[i].nr, nd->id);
            continue;
         }
         //printf("   <!-- [%d]: start = %.2f, end = %.2f, col = %d, r = %.2f, nr = %d -->\n",
         //   i, sec[i].start, sec[i].end, sec[i].col, sec[i].r, sec[i].nr);
         if (sector_calc3(rd, ond, &sec[i], b))
            rd->cb.log_msg(LOG_ERR, "sector_calc3 failed: %s", strerror(errno));

         if (sec[i].col[1] != -1)
         {
            sec[i].sf[0].startr = sec[i].sf[sec[i].fused - 1].endr = 0;
            for (j = 0; j < 4; j++)
            {
               for (k = 0; k < sec[i].fused; k++)
                  sec[i].sf[k].r -= altr_[j];
               sec[i].al++;
               if (sector_calc3(rd, ond, &sec[i], b))
                  rd->cb.log_msg(LOG_ERR, "sector_calc3 failed: %s", strerror(errno));
            }
         }
      }
   } // for (i = 0; n && i < MAX_SEC; i++)
}

