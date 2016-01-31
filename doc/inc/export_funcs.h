
// Use smrender's standard logging. This function is defined in 'smlog.h' and
// works similar to syslog(3).
void log_msg(int, const char*, ...);

// Get an OSM object (OSM_NODE, OSM_WAY, OSM_REL) with the specifed id. This
// function returns a pointer to either an osm_node_t or osm_way_t or_osm_rel_t
// structure on success, or NULL on error.
void *get_object(int, int64_t);

// Add an OSM object to the memory. The function returns 0 on success,
// otherwise -1 is returned. Preexisting objects with the same id are simply
// overwritten.
int put_object(osm_obj_t*);

// These functions return unique ids for nodes and ways.
int64_t unique_node_id(void);
int64_t unique_way_id(void);

// Initialize an OSM object. The number of tags (type short) and the number of
// node references (type int) must be supplied. Currently, the functions always
// return a valid pointer to an object. The objects returned are just partially
// initialized (see 'osm_func.c').
osm_node_t *malloc_node(short);
osm_way_t *malloc_way(short, int);
osm_rel_t *malloc_rel(short, short);

