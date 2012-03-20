#ifndef SMRPARSE_H

/*
typedef struct fparam
{
   char *attr;
   char *val;
   double dval;
} fparam_t;
*/

int parse_color(const struct rdata *, const char *);
const char *rule_type_str(int);
int prepare_rules(osm_obj_t*, struct rdata*, void*);
fparam_t **parse_fparam(char*);
//char *get_param(const char*, double*, fparam_t**);


#endif

