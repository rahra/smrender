/* Copyright 2011 Bernhard R. Fischer, 2048R/5C5FFD47 <bf@abenteuerland.at>
 *
 * This file is part of libhpxml.
 *
 * Libhpxml is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3 of the License.
 *
 * Libhpxml is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with libhpxml. If not, see <http://www.gnu.org/licenses/>.
 */

#include <stddef.h>
#include <string.h>
#include "bstring.h"


/*! Advance bstring_t->buf and decrease bstring_t->len. This function does NOT
 * check if string length >= 1 and if b->buf != NULL which might result in
 * buffer underflows or segfaults.
 * @param b Pointer to bstring_t;
 * @return Length of string.
 */
int bs_advance(bstring_t *b)
{
   b->buf++;
   b->len--;
   return b->len;
}


int bs_advancel(bstringl_t *b)
{
   b->buf++;
   b->len--;
   return b->len;
}


/*! This function is like bs_advance() but does safety checks on pointers and
 * buffer length.
 * @param b Pointer to bstring_t.
 * @return Length of string.
 */
int bs_advance2(bstring_t *b)
{
   if (b == NULL || b->buf == NULL || b->len < 1)
      return 0;
   
   return bs_advance(b);
}


/*! bs_ncmp compares exactly n bytes of b and s. If they are equal, 0 is
 * returned. If they are not equal, the return value of strncmp(3) is returned.
 * If the string length of either is less then n, -2 is returned.
 */
int bs_ncmp(bstring_t b, const char *s, int n)
{
   if ((b.len < n) || ((int) strlen(s) < n))
      return -2;
   return strncmp(b.buf, s, n);
}


int bs_cmp(bstring_t b, const char *s)
{
   for (; b.len && *s; (void) bs_advance(&b), s++)
   {
      // element of b is less than element of s
      if (*b.buf < *s)
         return -1;
      // element of b is greater than element of s
      if (*b.buf > *s)
         return 1;
   }

   // strings are equal and of equal length
   if (!b.len && !*s)
      return 0;

   // string s is longer than b
   if (*s)
      return 1;

   // string s is shorter than b
   return -1;
}


long bs_tol(bstring_t b)
{
   int n = 0;
   long l = 0;

   if (b.len && *b.buf == '-')
   {
      (void) bs_advance(&b);
      n = 1;
   }

   for (; b.len && *b.buf >= '0' && *b.buf <= '9'; (void) bs_advance(&b))
   {
      l *= 10;
      l += *b.buf - '0';
   }

   if (n)
      return -l;

   return l;
}


double bs_tod(bstring_t b)
{
   int n = 0, e;
   double d = 0.0;

   if (b.len && *b.buf == '-')
   {
      (void) bs_advance(&b);
      n = 1;
   }

   for (e = -1; b.len; (void) bs_advance(&b))
   {
      if (*b.buf == '.')
      {
         e++;
         continue;
      }
      if ((*b.buf < '0') || (*b.buf > '9'))
         break;
      if (e >= 0) e++;
      d *= 10.0;
      d += (double) (*b.buf - '0');
   }
   
   for (; e > 0; e--)
      d /= 10.0;

   if (n)
      return -d;

   return d;
}

