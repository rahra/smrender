

#include "smrender.h"
#include "smcore.h"
#include "rdata.h"


struct query
{
   bx_node_t *root;
   bx_node_t *index;
   const struct bbox *bb;
};


int is_in_bb(const osm_node_t *n, const struct bbox *bb)
{
   return n->lat >= bb->ll.lat && n->lat < bb->ru.lat && n->lon >= bb->ll.lon && n->lon < bb->ru.lon;
}


/*! add all relations of a given object
 * */
void put_obj_rels(bx_node_t **root, bx_node_t *index, osm_obj_t *o)
{
   osm_obj_t **optr;

   // get index pointer for the way
   if ((optr = get_object0(index, o->id, o->type - 1)) != NULL)
   {
      for (; *optr != NULL; optr++)
      {
        // add all relations which reference this way
        if ((*optr)->type == OSM_REL)
          (void) put_object0(root, (*optr)->id, *optr, IDX_REL);
      }
   }
}

 
int get_node_bb(osm_node_t *n, struct query *q)
{
   osm_obj_t **optr;

   // check if node is within bounding box
   if (is_in_bb(n, q->bb))
   {
      // add node to output data
      (void) put_object0(&q->root, n->obj.id, n, n->obj.type - 1);

      // get index pointer (list of objects of which this node is a member)
      if ((optr = get_object0(q->index, n->obj.id, n->obj.type - 1)) != NULL)
      {
         // loop over all those objects
         for (; *optr != NULL; optr++)
         {
            switch ((*optr)->type)
            {
               case OSM_REL:
                  // add all relations that reference the corresponding node
                  (void) put_object0(&q->root, (*optr)->id, *optr, IDX_REL);
                  // add all relations that reference the previous relation
                  put_obj_rels(&q->root, q->index, *optr);
                  break;

               case OSM_WAY:
                  // add all ways that reference the corresponding node
                  (void) put_object0(&q->root, (*optr)->id, *optr, IDX_WAY);
                  // add all relations that reference the previous way
                  put_obj_rels(&q->root, q->index, *optr);
                  // add all nodes of those ways
                  for (int i = 0; i < ((osm_way_t*) (*optr))->ref_cnt; i++)
                  {
                     if ((n = get_object(OSM_NODE, ((osm_way_t*) (*optr))->ref[i])) != NULL)
                     {
                        (void) put_object0(&q->root, n->obj.id, n, IDX_NODE);
                        // add all relations that reference the previous node
                        put_obj_rels(&q->root, q->index, (osm_obj_t*) n);
                     }
                  }
                  break;

               default:
                  log_msg(LOG_ERR, "ill object type!");
            }
         }
      }
   }
   return 0;
}


bx_node_t *get_obj_bb(bx_node_t *index, const struct bbox *bb)
{
   struct query q = {NULL, index, bb};

   traverse(*get_objtree(), 0, IDX_NODE, (tree_func_t) get_node_bb, &q);
   return q.root;
}

