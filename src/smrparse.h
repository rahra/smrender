#ifndef SMRPARSE_H


int parse_color(const struct rdata *, const char *);
const char *rule_type_str(int);
int prepare_rules(osm_obj_t*, struct rdata*, void*);


#endif

