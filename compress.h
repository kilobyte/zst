typedef int(compress_func)(int,int,const char*,const char*);

typedef struct
{
    const char          *name;
    const char          *ext;
    compress_func       *comp;
} compress_info;

extern compress_info compressors[];
extern compress_info decompressors[];

compress_info *comp_by_ext(const char *name, compress_info *ci);
compress_info *comp_by_name(const char *name, compress_info *ci);
