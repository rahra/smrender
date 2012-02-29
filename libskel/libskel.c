#include <stdio.h>

#include "smrender.h"


static FILE *f_;


/*! Library constructor. String constants which are returned to load program
 * are copied to heap memory.
 */
void __attribute__ ((constructor)) init_lib___(void)
{
   log_msg(LOG_INFO, "initializing libskel.so");
   f_ = stderr;
}


void __attribute__ ((destructor)) fini_lib___(void)
{
   log_msg(LOG_INFO, "libskel.so unloaded");
}


/*! Rule initialization function. Called once before the first object matches
 * the rule.
 */
void skelfunc_ini(orule_t *r)
{
   fprintf(f_, "print_out_init() called\n");
   if (r->rule.func.parm != NULL)
      fprintf(f_, "parameter string = '%s'\n", r->rule.func.parm);
}


/*! Rule function. Called every time an object matches the rule.
 */
int skelfunc(osm_obj_t *o)
{
   fprintf(f_, "object has %d tags and is ", o->tag_cnt);

   switch (o->type)
   {
      case OSM_NODE:
         fprintf(f_, "a node with coords %f.3 %f.3\n", ((osm_node_t*) o)->lat, ((osm_node_t*) o)->lon);
         break;

      case OSM_WAY:
         fprintf(f_, "a way with %d node references\n", ((osm_way_t*) o)->ref_cnt);
         break;

      default:
         fprintf(f_, "of unknown type %d\n", o->type);
   }

   return 0;
}


/*! Deinitialization function. Called once after the last object match.
 */
void skelfunc_fini(void)
{
   fprintf(f_, "skel_func_fini() called\n");
   fflush(f_);
}

