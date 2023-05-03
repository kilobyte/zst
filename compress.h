#include <stdbool.h>
#include <stdint.h>

typedef uint64_t magic_t;

typedef struct
{
    const char *path, *name_in, *name_out;
    unsigned long long sz, sd;
} file_info;

typedef int(compress_func)(int,int,file_info*restrict,magic_t);

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

int match_suffix(const char *txt, const char *ext);
int rewrite(int fd, const void *buf, size_t len);

int read_bz3(int in, int out, file_info *restrict fi, magic_t head);
int write_bz3(int in, int out, file_info *restrict fi, magic_t head);
