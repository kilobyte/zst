#define _GNU_SOURCE
#include "config.h"
#include <endian.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#ifdef HAVE_LIBBZ3
# include <libbz3.h>
#endif
#include "compress.h"
#include "zst.h"

#define BUFFER_SIZE 32768

#define GRIPE(f,msg,...) fprintf(stderr, "%s: %s%s: " msg, exe, fi->path, fi->name_##f?: "std" #f,##__VA_ARGS__)
#define ERR(l,f,msg,...) do {GRIPE(f, msg "\n",##__VA_ARGS__);goto l;} while(0)
#define ERRoom(l,f) ERR(l,f,"Out of memory.")
#define ERRueof(l,f) ERR(l,f,"unexpected end of file")
#define ERRlibc(l,f) ERR(l,f,"%m")

#ifdef HAVE_LIBBZ3
#define ERRbz3(l,f) do {GRIPE(f, "%s\n", bz3_strerror(state));goto l;} while (0)
// 65KB..511MB is weird but that's what upstream decreed.
#define MIN_BLOCK (65*KB)
#define MAX_BLOCK (511*MB)

static uint32_t get_u32(const void *mem)
{
    typedef struct __attribute__((__packed__)) { uint32_t v; } u32_unal;
    uint32_t bad_endian = ((u32_unal*)mem)->v;
    return htole32(bad_endian);
}

static uint64_t decompress_bz3(int in, int out, file_info *restrict fi, uint32_t blen)
{
    uint64_t bhead;
    uint8_t *buffer = 0;
    struct bz3_state *state = bz3_new(blen);

    if (!state)
        ERRoom(fail, in);
    buffer = malloc(bz3_bound(blen));
    if (!buffer)
        ERRoom(fail, in);

    while (1)
    {
        int ret = read(in, &bhead, 8);
        if (ret != 8)
            if (!ret)
                break;
            else if (ret == -1)
                ERRlibc(fail, in);
            else
                ERRueof(fail, in);
        uint32_t *bhead32 = (void*)&bhead;
        uint32_t zlen = htole32(bhead32[0]);
        uint32_t dlen = htole32(bhead32[1]);

        if (zlen == 0x76335a42)
        {
            bz3_free(state);
            free(buffer);
            return bhead;
        }

        if (dlen > blen || zlen > blen + 31)
            ERR(fail, in, "file corrupted: inconsistent headers");

        ssize_t iolen = read(in, buffer, zlen);
        if (iolen != zlen)
            if (iolen == -1)
                ERRlibc(fail, in);
            else
                ERRueof(fail, in);
        if (bz3_decode_block(state, buffer, zlen, dlen) != dlen)
            ERR(fail, in, "file corrupted: %s", bz3_strerror(state));
        if (rewrite(out, buffer, dlen))
            ERRlibc(fail, out);

        fi->sd += dlen;
        fi->sz += zlen + 8;
    }
    bz3_free(state);
    free(buffer);
    return 0;

fail:
    bz3_free(state);
    free(buffer);
    return -1;
}

int read_bz3(int in, int out, file_info *restrict fi, magic_t head)
{
    uint32_t blen;
    char shead[9];
    if (head)
        *((magic_t*)shead) = head;

new_stream:
    // Read the stream header.
    {
        int hlen = head? MLEN : 0;
        int ret = read(in, shead + hlen, 9 - hlen);
        if (ret != 9 - hlen)
            if (ret == -1)
                ERRlibc(fail, in);
            else
                ERRueof(fail, in);
        if (shead[0]!='B' || shead[1]!='Z' || shead[2]!='3' || shead[3]!='v' || shead[4]!='1')
            ERR(fail, in, "Invalid signature.");
        blen = get_u32(shead + 5);
        if (blen < MIN_BLOCK || blen > MAX_BLOCK)
            ERR(fail, in, "file corrupted: invalid block size in header");
        fi->sz += 9;
    }

    head = decompress_bz3(in, out, fi, blen);
    if (!head)
        return 0;
    if (head != -1)
        goto new_stream;

fail:
    return 1;
}

static bool compress_bz3(int in, int out, file_info *restrict fi, uint32_t blen)
{
    uint8_t *buffer = 0;
    struct bz3_state *state = bz3_new(blen);
    ssize_t dlen = 0;
    size_t zlen;

    if (!state)
        ERRoom(end, in);
    buffer = malloc(bz3_bound(blen));
    if (!buffer)
        ERRoom(end, in);

    while ((dlen = read(in, buffer, blen)))
    {
        if (dlen == -1)
            ERRlibc(end, in);
        if ((zlen = bz3_encode_block(state, buffer, dlen)) < 0)
            ERR(end, in, "%s", bz3_strerror(state));
        uint32_t bhead[2] = {zlen, dlen};
        if (rewrite(out, bhead, sizeof bhead) || rewrite(out, buffer, zlen))
            ERRlibc(end, out);

        fi->sd += dlen;
        fi->sz += zlen + 8;
    }

end:
    bz3_free(state);
    free(buffer);
    return !!dlen;
}

int write_bz3(int in, int out, file_info *restrict fi, magic_t head)
{
    static uint32_t bz3_levels[10] =
    {
        16*MB,
        /* 1 */ 65*KB,
        /* 2 */ 256*KB,
        /* 3 */ 1*MB,
        /* 4 */ 4*MB,
        /* 5 */ 16*MB,
        /* 6 */ 64*MB,
        /* 7 */ 128*MB,
        /* 8 */ 256*MB,
        /* 9 */ 511*MB,
    };

    uint32_t blen = bz3_levels[level];

    {
        char shead[12] = "\0\0\0BZ3v1";
        *((uint32_t*)(shead+8)) = htole32(blen);
        if (rewrite(out, shead+3, 9))
            ERRlibc(fail, out);
        fi->sz += 9;
    }

    return compress_bz3(in, out, fi, blen);

fail:
    return 1;
}
#endif
