
class BString
{
   protected:
      int len;
      char *buf;
};


class OsmTag
{
   protected:
      BString key, val;
};


class OsmObj
{
   public:
      OsmObj();
      virtual ~OsmObj();

   protected:
      short vis;
      int64_t id;
      int ver, cs, uid;
      time_t tim;
      int tag_cnt;
      OsmTag *otag;
};

