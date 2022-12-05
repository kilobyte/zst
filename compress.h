#include <stdbool.h>

typedef struct
{
    const char *path, *name_in, *name_out;
    unsigned long long sz, sd;
} file_info;

typedef int(compress_func)(int,int,file_info*restrict,char*);

#define MLEN 8
typedef struct
{
    const char          *name;
    const char          *ext;
    compress_func       *comp;
    char		magic[MLEN], magicmask[MLEN];
} compress_info;

extern compress_info compressors[];
extern compress_info decompressors[];

compress_info *comp_by_ext(const char *name, compress_info *ci);
compress_info *comp_by_name(const char *name, compress_info *ci);

bool decomp(bool can_cat, int in, int out, file_info*restrict fi);
