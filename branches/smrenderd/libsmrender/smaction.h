/* Copyright 2011 Bernhard R. Fischer, 2048R/5C5FFD47 <bf@abenteuerland.at>
 *
 * This file is part of smrender.
 *
 * Smfilter is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3 of the License.
 *
 * Smfilter is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with smrender. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef SMACTION_H
#define SMACTION_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdint.h>
#include <regex.h>

#define SPECIAL_DIRECT 0x0000
#define SPECIAL_REGEX 0x0001
#define SPECIAL_GT 0x0002
#define SPECIAL_LT 0x0003
#define SPECIAL_INVERT 0x8000
#define SPECIAL_NOT 0x4000
#define SPECIAL_MASK 0x00ff

#define ACTION_THREADED 1
#define ACTION_OPEN_WAY -1
#define ACTION_CLOSED_WAY 1

#define TM_RESCALE 100
#define T_RESCALE (60 * TM_RESCALE)
#define MIN10(x) round((x) * T_RESCALE)

#define RED(x) ((((x) >> 16) & 0xff))
#define GREEN(x) ((((x) >> 8) & 0xff))
#define BLUE(x) (((x) & 0xff))
#define SQRL(x) ((long) (x) * (long) (x))

typedef struct action action_t;

typedef struct fparam
{
   char *attr;
   char *val;
   double dval;
   int conv_error;   //!< contains conversion errors from strtod(3), 0 || ERANGE || EDOM
} fparam_t;

struct specialTag
{
   short type;
   union
   {
      regex_t re;
      double val;
   };
};

struct stag
{
   struct specialTag stk;
   struct specialTag stv;
};

struct actParam
{
   char *buf;
   fparam_t **fp;
};

struct action
{
   union             // initialization function _ini()
   {
      int (*func)(void*);
      void *sym;
   } ini;
   union             // rule function
   {
      int (*func)(void*, osm_obj_t*);
      void *sym;
   } main;
   union             // finalization function _fini()
   {
      int (*func)(void*);
      void *sym;
   } fini;
   void *libhandle;  // pointer to lib base
   char *func_name;  // pointer to function name
   char *parm;       // function argument string
   fparam_t **fp;    // pointer to parsed parameter list
   short flags;      // execution control flags.
   short finished;   // set to 1 after _fini was called, otherwise 0
   short way_type;   // -1 if open, 0 in any case, 1 of closed
   short tag_cnt;
   struct stag stag[];
};

#endif

