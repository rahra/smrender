/* Copyright 2014-2018 Bernhard R. Fischer.
 *
 * This file is part of Smrender.
 *
 * Smrender is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3 of the License.
 *
 * Smrender is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Smrender. If not, see <http://www.gnu.org/licenses/>.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stdint.h>
#include <inttypes.h>
#include <errno.h>
#include <signal.h>

#include "smrender.h"
#include "smdfunc.h"


// this is defined in smrender_dev.h
int apply_smrules1(smrule_t *r, long ver, const bx_node_t *ot);


int act_ws_traverse_ini(smrule_t *UNUSED(r))
{
   return 0;
}


int act_ws_traverse_main(smrule_t *r, osm_obj_t *o)
{
   trv_com_t *tc;
   int ret = 0;

   if (r == NULL || o == NULL)
   {
      log_msg(LOG_ERR, "rule == NULL || o == NULL. This should not happen!");
      return 1;
   }

   tc = r->data;
   pthread_mutex_lock(&tc->mtx);
   switch (tc->slave_cmd)
   {
      case TC_BREAK:
      case TC_EXIT:
         ret = 1;
         break;

      case TC_NEXT:
         tc->o = o;
         tc->master_cmd = TC_NEXT;
         tc->slave_cmd = TC_WAIT;
         log_debug("signalling master, that next object is ready");
         pthread_cond_signal(&tc->master_cnd);
         while (tc->slave_cmd == TC_WAIT)
            pthread_cond_wait(&tc->slave_cnd, &tc->mtx);
         break;

      default:
         log_msg(LOG_ERR, "ill command: %d", tc->slave_cmd);
   }
   pthread_mutex_unlock(&tc->mtx);

   return ret;
}


int act_ws_traverse_fini(smrule_t *r)
{
   trv_com_t *tc;

   if (r->data != NULL)
   {
      tc = r->data;
      pthread_mutex_lock(&tc->mtx);
      tc->master_cmd = TC_READY;
      tc->o = NULL;
      pthread_cond_signal(&tc->master_cnd);
      pthread_mutex_unlock(&tc->mtx);
   }
 
   return 0;
}


/*! Thread routine, which traverses.
 * This is the slave. */
void *traverse_thread(void *p)
{
   trv_com_t *tc = p;
   sigset_t sset;
   smrule_t *r;
   int e = 0;

   // safety check
   if (p == NULL)
      return (void*) -1L;

   sigemptyset(&sset);
   if ((e = pthread_sigmask(SIG_BLOCK, &sset, NULL)))
      log_msg(LOG_ERR, "pthread_sigmask() failed: %s", strerror(e));
 
   for (int running = 1; running;)
   {
      pthread_mutex_lock(&tc->mtx);
      while (tc->slave_cmd == TC_WAIT)
      {
         log_debug("signal master that slave is ready");
         tc->master_cmd = TC_READY;
         pthread_cond_signal(&tc->master_cnd);
         pthread_cond_wait(&tc->slave_cnd, &tc->mtx);
      }

      switch (tc->slave_cmd)
      {
         case TC_TRAVERSE:
            r = tc->r;
            // safety check
            if (r == NULL || r->oo == NULL)
            {
               pthread_mutex_unlock(&tc->mtx);
               break;
            }

            tc->slave_cmd = TC_NEXT;
            r->data = tc;
            pthread_mutex_unlock(&tc->mtx);
            e = apply_smrules1(r, r->oo->ver, tc->ot);
            break;

         case TC_BREAK:
            tc->slave_cmd = TC_WAIT;
            pthread_mutex_unlock(&tc->mtx);
            break;

         case TC_EXIT:
            pthread_mutex_unlock(&tc->mtx);
            running = 0;
            break;

         default:
            log_msg(LOG_ERR, "ill slave command: %d", tc->slave_cmd);
            tc->slave_cmd = TC_WAIT;
            pthread_mutex_unlock(&tc->mtx);
      }
   }

   log_debug("thread exiting");
   return (void*) (long) e;
}


void *tc_next(trv_com_t *tc)
{
   void *o = NULL;

   pthread_mutex_lock(&tc->mtx);
   tc->slave_cmd = TC_NEXT;
   pthread_cond_signal(&tc->slave_cnd);
   while (tc->master_cmd == TC_WAIT)
      pthread_cond_wait(&tc->master_cnd, &tc->mtx);
   switch (tc->master_cmd)
   {
      // no more elements available
      case TC_READY:
         break;
      case TC_NEXT:
         o = tc->o;
         tc->master_cmd = TC_WAIT;
         break;
      default:
         log_msg(LOG_ERR, "ill master_cmd: %d", tc->master_cmd);
   }
   pthread_mutex_unlock(&tc->mtx);
   return o;
}


int tc_traverse(trv_com_t *tc)
{
   pthread_mutex_lock(&tc->mtx);
   log_debug("signalling slave to traverse");
   tc->slave_cmd = TC_TRAVERSE;
   pthread_cond_signal(&tc->slave_cnd);
   pthread_mutex_unlock(&tc->mtx);
   return 0;
}


/*! Signal slave to break and wait until it is ready for next command. */
int tc_break(trv_com_t *tc)
{
   pthread_mutex_lock(&tc->mtx);
   log_debug("breaking slave");
   tc->slave_cmd = TC_BREAK;
   tc->master_cmd = TC_WAIT;
   pthread_cond_signal(&tc->slave_cnd);
   while (tc->master_cmd != TC_READY)
      pthread_mutex_unlock(&tc->mtx);
   pthread_mutex_unlock(&tc->mtx);
   return 0;
}


/*! Initialize trv_com_t structure and run thread.
 */
int tc_init(trv_com_t *tc)
{
   int e;

   // safety check
   if (tc == NULL)
      return -1;

   memset(tc, 0, sizeof(*tc));
   tc->slave_cmd = TC_WAIT;
   if ((e = pthread_mutex_init(&tc->mtx, NULL)))
      goto tc_init_err;

   if ((e = pthread_cond_init(&tc->slave_cnd, NULL)))
      goto tc_init_err;

   if ((e = pthread_cond_init(&tc->master_cnd, NULL)))
      goto tc_init_err;

   if ((e = pthread_create(&tc->thread, NULL, traverse_thread, tc)))
      goto tc_init_err;

   return 0;

tc_init_err:
   // FIXME: free all mutex and condition resources here!
   log_msg(LOG_ERR, "pthread error: %s", strerror(e));
   return -1;
}


int tc_free(trv_com_t *tc)
{
   void *ret;
   int e;

   //safety check
   if (tc == NULL)
      return -1;

   pthread_mutex_lock(&tc->mtx);
   tc->slave_cmd = TC_EXIT;
   pthread_cond_signal(&tc->slave_cnd);
   pthread_mutex_unlock(&tc->mtx);

   if ((e = pthread_join(tc->thread, &ret)))
      log_msg(LOG_ERR, "pthread_join() failed: %s", strerror(e));

   pthread_cond_destroy(&tc->slave_cnd);
   pthread_cond_destroy(&tc->master_cnd);
   pthread_mutex_destroy(&tc->mtx);

   return 0;
}

