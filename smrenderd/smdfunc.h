#ifndef SMDFUNC_H
#define SMDFUNC_H

#include "smrender.h"
#include "osm_inplace.h"


enum {
   TC_WAIT,    // both: wait for signal
   TC_BREAK,   // master->slave: break traversal
   TC_EXIT,    // master->slave: break traversal and terminate thread
   TC_NEXT,    // slave->master: next object is ready in data structure
               // master->slave: return next object
   TC_TRAVERSE,// master->slave: start traversal
   TC_READY};  // slave->master: ready for next traversal


typedef struct trv_com
{  
	pthread_t thread;
	//pthread_mutex_t slave_mtx, master_mtx;
	pthread_mutex_t mtx;
	pthread_cond_t slave_cnd, master_cnd;

	int slave_cmd, master_cmd;
   osm_obj_t *o;
   smrule_t *r;
   bx_node_t *ot;
} trv_com_t;

void *tc_next(trv_com_t *tc);
int tc_traverse(trv_com_t *tc);
int tc_init(trv_com_t *tc);
int tc_free(trv_com_t *tc);
int tc_break(trv_com_t *tc);


#endif

