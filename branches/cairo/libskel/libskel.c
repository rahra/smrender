#include <stdio.h>
#include <stdlib.h>

#include "smrender.h"


struct some_data
{
   FILE *f;
};


/*! Library constructor. String constants which are returned to load program
 * are copied to heap memory.
 */
void __attribute__ ((constructor)) init_lib___(void)
{
   log_msg(LOG_INFO, "initializing libskel.so");
}


void __attribute__ ((destructor)) fini_lib___(void)
{
   log_msg(LOG_INFO, "libskel.so unloaded");
}


/*! Rule initialization function. Called once before the first object matches
 * the rule.
 */
int act_skelfunc_ini(smrule_t *r)
{
   struct some_data *s;
   char *b;

   if ((s = malloc(sizeof(*s))) == NULL)
      return 1;

   s->f = stderr;
   r->data = s;

   fprintf(s->f, "print_out_init() called\n");
   if ((b = get_param("foo", NULL, r->act)) != NULL)
      fprintf(s->f, "parameter 'foo' = '%s'\n", b);

   return 0;
}


/*! Rule function. Called every time an object matches the rule.
 */
int act_skelfunc_main(smrule_t *r, osm_obj_t *o)
{
   struct some_data *s = r->data;

   fprintf(s->f, "object has %d tags and is ", o->tag_cnt);

   switch (o->type)
   {
      case OSM_NODE:
         fprintf(s->f, "a node with coords %f.3 %f.3\n", ((osm_node_t*) o)->lat, ((osm_node_t*) o)->lon);
         break;

      case OSM_WAY:
         fprintf(s->f, "a way with %d node references\n", ((osm_way_t*) o)->ref_cnt);
         break;

      default:
         fprintf(s->f, "of unknown type %d\n", o->type);
   }

   return 0;
}


/*! Deinitialization function. Called once after the last object match.
 */
int act_skelfunc_fini(smrule_t *r)
{
   struct some_data *s = r->data;

   fprintf(s->f, "skel_func_fini() called\n");
   fflush(s->f);
   free(s);
   r->data = NULL;
   return 0;
}

