typedef struct smrule smrule_t;
typedef struct action action_t;

struct smrule
{
   osm_obj_t *oo;
   void *data;       // arbitrary data
   action_t *act;
};

char *get_param(const char*, double*, const action_t*);
char *get_parami(const char*, int*, const action_t*);
