typedef struct osm_obj
{
   // type of object: {OSM_NODE, OSM_WAY, OSM_REL}
   short type;
   // visibility: {0, 1}
   short vis;
   // OSM id
   int64_t id;
   // version, changeset, user id
   int ver, cs, uid;
   // Unix timestamp
   time_t tim;
   // number of tags
   short tag_cnt;
   // Pointer to tags
   struct otag *otag;
} osm_obj_t;

