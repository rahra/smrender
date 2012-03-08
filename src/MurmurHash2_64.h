#ifndef MURMURHASH2_64_h
#define MURMURHASH2_64_h


#include <stdint.h>

#ifdef __LP64__
#define MurmurHash64 MurmurHash64A
#else
#define MurmurHash64 MurmurHash64B
#endif

uint64_t MurmurHash64 (const void *, int, unsigned int);


#endif

