#include <stdio.h>
#include <syslog.h>

#include "smrender.h"


int demo(struct onode *nd, struct rdata *rd)
{
   static int cnt = 0;

   rd->cb.log_msg(LOG_DEBUG, "node id %ld (%d)", nd->nd.id, ++cnt);

   return 0;
}


