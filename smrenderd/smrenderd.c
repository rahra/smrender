#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <signal.h>

#include "smhttp.h"
#include "smrender.h"
#include "smloadosm.h"
#include "smcore.h"
#include "libhpxml.h"


/* from smlog.c/libsmrender */
FILE *init_log(const char *, int );


volatile sig_atomic_t int_ = 0;
int render_all_nodes_ = 1;
bx_node_t *index_ = NULL;


static int free_objects(osm_obj_t *o, void * UNUSED(p))
{
   free_obj(o);
   return 0;
}


int main(int argc, char **argv)
{
   char *osm_ifile = "/dev/stdin";
   //bx_node_t *index = NULL;
   struct dstats ds;
   hpx_ctrl_t *ctl;
   struct stat st;
   int w_mmap = 1;
   int fd = 0;

   (void) init_log("stderr", LOG_DEBUG);

   if (argc > 1)
      osm_ifile = argv[1];

   if ((osm_ifile != NULL) && ((fd = open(osm_ifile, O_RDONLY)) == -1))
      log_msg(LOG_ERR, "cannot open file %s: %s", osm_ifile, strerror(errno)),
         exit(EXIT_FAILURE);

   if (fstat(fd, &st) == -1)
      perror("stat"), exit(EXIT_FAILURE);

   if (w_mmap)
   {
      log_msg(LOG_INFO, "input file will be memory mapped with mmap()");
      st.st_size = -st.st_size;
   }
   if ((ctl = hpx_init(fd, st.st_size)) == NULL)
      perror("hpx_init_simple"), exit(EXIT_FAILURE);

   log_msg(LOG_INFO, "reading osm data (file size %ld kb, memory at %p)",
         (long) labs(st.st_size) / 1024, ctl->buf.buf);

 (void) read_osm_file(ctl, get_objtree(), NULL, &ds);

   log_debug("tree memory used: %ld kb", (long) bx_sizeof() / 1024);
   log_debug("onode memory used: %ld kb", (long) onode_mem() / 1024);

   log_msg(LOG_INFO, "creating reverse pointers from nodes to ways");
   traverse(*get_objtree(), 0, IDX_WAY, (tree_func_t) rev_index_way_nodes, &index_);
   log_msg(LOG_INFO, "creating reverse pointers from relation members to relations");
   traverse(*get_objtree(), 0, IDX_REL, (tree_func_t) rev_index_rel_nodes, &index_);

   main_smrenderd();
   (void) close(ctl->fd);
   hpx_free(ctl);

   log_debug("freeing main objects");
   traverse(*get_objtree(), 0, IDX_REL, free_objects, NULL);
   traverse(*get_objtree(), 0, IDX_WAY, free_objects, NULL);
   traverse(*get_objtree(), 0, IDX_NODE, free_objects, NULL);

   log_debug("freeing main object tree");
   bx_free_tree(*get_objtree());

   log_msg(LOG_INFO, "Thanks for using smrender!");
   return EXIT_SUCCESS;
}


