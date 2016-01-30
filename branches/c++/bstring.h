#ifndef bstring_h__
#define bstring_h__


class Bstring
{
   protected:
      size_t len;
      char *buf;

      virtual void init(void);

   public:

      Bstring(void);
      Bstring(const char *);
      Bstring(const char *, int);
      virtual ~Bstring(void);

      virtual void set(const char *);
      virtual void set(const char *, int);

      const char *get_buf(void) const;
      size_t get_len(void) const;

      virtual void del(void);

      int advance(void);
      int advance2(void);
      int nadvance(size_t );
      int ncmp(const char *, size_t );
      int cmp(const char *);
      long tol(void);
      double tod(void);
};


class HeapBstring : public Bstring
{
   protected:
      char *base;
      virtual void init(void);
      //void copy(const Bstring &);
   public:
      HeapBstring(void);
      HeapBstring(const char *);
      HeapBstring(const char *, int);
      HeapBstring(const Bstring &);
      HeapBstring(const HeapBstring &);
      //HeapBstring(const Bstring &);
      virtual ~HeapBstring(void);

      virtual void set(const char *);
      virtual void set(const char *, int);

      virtual void del(void);
};


#endif

