/* Copyright 2011-2016 Bernhard R. Fischer, 2048R/5C5FFD47 <bf@abenteuerland.at>
 *
 * This file is part of Smrender.
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

#include <iostream>
#include <stddef.h>
#include <string.h>
#include "bstring.h"
#include <stdio.h>


Bstring::Bstring(void)
{
   init();
   printf("Bstring()\n");
}


Bstring::Bstring(const char *s)
{
   set(s);
}


Bstring::Bstring(const char *s, int n)
{
   set(s, n);
}


Bstring::~Bstring(void)
{
//   del();
   printf("~Bstring()\n");
}


void Bstring::init(void)
{
   len = 0;
   buf = 0;
}

void Bstring::del(void)
{
   init();
}


void Bstring::set(const char *s)
{
   if (s)
      set(s, strlen(s));
   else
      set(0, 0);
}


void Bstring::set(const char *s, int n)
{
   len = n;
   buf = const_cast<char*>(s);
}


const char *Bstring::get_buf(void) const
{
   return buf;
}

size_t Bstring::get_len(void) const
{
   return len;
}


/*! Advance bstring_t->buf and decrease bstring_t->len. This function does NOT
 * check if string length >= 1 and if b->buf != NULL which might result in
 * buffer underflows or segfaults.
 * @param b Pointer to bstring_t;
 * @return Length of string.
 */
int Bstring::advance(void)
{
   buf++;
   len--;
   return len;
}


/*! This function is like bs_advance() but does safety checks on pointers and
 * buffer length.
 * @param b Pointer to bstring_t.
 * @return Length of string.
 */
int Bstring::advance2(void)
{
   if (buf == NULL || len < 1)
      return 0;
   
   return advance();
}


int Bstring::nadvance(size_t n)
{
   buf += n;
   len -= n;
   return len;
}


/*! bs_ncmp compares exactly n bytes of b and s. If they are equal, 0 is
 * returned. If they are not equal, the return value of strncmp(3) is returned.
 * If the string length of either is less then n, -2 is returned.
 */
int Bstring::ncmp(const char *s, size_t n)
{
   if ((len < n) || (strlen(s) < n))
      return -2;
   return strncmp(buf, s, n);
}


/*! This function compares a b_string to a regular C \0-terminated character
 * string.
 * @param b String as bstring_t structure.
 * @param s Pointer to C string.
 * @return The function returns an integer less than, equal, or greater than 0
 * exactly like strcmp(3).
 */
int Bstring::cmp(const char *s)
{
   char c;

   // compare characters and return difference if they are not equal
   for (; len && *s; (void) advance(), s++)
      if ((c = *buf - *s))
         return c;

   // strings are equal and of equal length
   if (!len && !*s)
      return 0;

   // string s is longer than b
   if (*s)
      return -*s;

   // string s is shorter than b
   return *buf;
}


/*! This function converts the string in b into a long integer. Currently, it
 * converts only decimal numbers, i.e. it uses a base of 10.
 * @param b String of type bstring_t.
 * @return The function returns the value of the converted string. The
 * conversion stops at the first character which is not between 0 and 9. Thus,
 * it returns 0 if there is no digit at the beginning of the string.
 * FIXME: This function should be improved to something similar to strtol(3).
 */
long Bstring::tol(void)
{
   int n = 1;
   long l = 0;

   if (len && *buf == '-')
   {
      (void) advance();
      n = -1;
   }

   for (; len && *buf >= '0' && *buf <= '9'; (void) advance())
   {
      l *= 10;
      l += *buf - '0';
   }

   return l * n;
}


double Bstring::tod(void)
{
   int n = 0, e;
   double d = 0.0;

   if (len && *buf == '-')
   {
      (void) advance();
      n = 1;
   }

   for (e = -1; len; (void) advance())
   {
      if (*buf == '.')
      {
         e++;
         continue;
      }
      if ((*buf < '0') || (*buf > '9'))
         break;
      if (e >= 0) e++;
      d *= 10.0;
      d += (double) (*buf - '0');
   }
   
   for (; e > 0; e--)
      d /= 10.0;

   if (n)
      return -d;

   return d;
}


HeapBstring::HeapBstring(void)
{
   init();
   printf("HeapBconst()\n");
}


HeapBstring::HeapBstring(const char *s)
{
   init();
   set(s);
   printf("HeapBconst()\n");
}

HeapBstring::HeapBstring(const Bstring &src)
{
   init();
   set(src.get_buf(), src.get_len());
   printf("HeapBconst()\n");
}

/*! Copy constructure
 */
HeapBstring::HeapBstring(const HeapBstring &src)
{
   init();
   set(src.buf, src.len);
   printf("copy_HeapBconst()\n");
/*   len = src.len;
   base = buf = new char[len];
   memcpy(buf, src.buf, len);*/
}

void HeapBstring::init(void)
{
   Bstring::init();
   base = 0;
}


/*
HeapBstring::HeapBstring(const Bstring &src)
{
   copy(src);
}

void HeapBstring::copy(const Bstring &src)
{
   len = src.len;
   base = buf = new char[len];
   memcpy(buf, src.buf, len);
}*/


HeapBstring::~HeapBstring(void)
{
   del();
   printf("~HeapBconst()\n");
}


void HeapBstring::set(const char *s)
{
   set(s, s ? strlen(s) : 0);
}


void HeapBstring::set(const char *s, int n)
{
   del();
   if (s)
   {
      base = new char[n];
      memcpy(base, s, n);
      Bstring::set(base, n);
   }
   else
      len = 0;
}


void HeapBstring::del(void)
{
   delete base;
   init();
}


#ifdef TEST_BSTRING
using std::cout;

int main(int argc, char **argv)
{
   Bstring b;

   if (argc <= 1)
      return 1;

   b.set(argv[1]);

   printf("%ld\n", b.tol());
   printf("%f\n", b.tod());

   HeapBstring h(b);
   printf("%ld\n", h.tol());
   printf("%f\n", h.tod());



   return 0;
}

#endif

