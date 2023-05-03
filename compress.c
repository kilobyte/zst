#define _GNU_SOURCE
#include "config.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <stdlib.h>
#ifdef HAVE_LIBBZ2
# include <bzlib.h>
#endif
#ifdef HAVE_LIBZ
# include <zlib.h>
#endif
#ifdef HAVE_LIBLZMA
# include <lzma.h>
#endif
#ifdef HAVE_LIBZSTD
# include <zstd.h>
#endif
#include "compress.h"
#include "zst.h"

#define U64(x) (*((uint64_t*)(x)))

#define BUFFER_SIZE 32768

#define GRIPE(f,msg,...) fprintf(stderr, "%s: %s%s: " msg, exe, fi->path, fi->name_##f?: "std" #f,##__VA_ARGS__)
#define ERR(l,f,msg,...) do {GRIPE(f, msg "\n",##__VA_ARGS__);goto l;} while(0)
#define ERRoom(l,f) ERR(l,f,"Out of memory.")
#define ERRueof(l,f) ERR(l,f,"unexpected end of file")
#define ERRlibc(l,f) ERR(l,f,"%m")

int rewrite(int fd, const void *buf, size_t len)
{
    if (fd == -1)
        return 0;

    while (len)
    {
        size_t done = write(fd, buf, len);
        if (done == -1)
            if (errno == EINTR)
                continue;
            else
                return -1;
        buf += done;
        len -= done;
    }
    return 0;
}

#ifdef HAVE_LIBBZ2
static const char *bzerr(int e)
{
    switch (e)
    {
    case BZ_MEM_ERROR:
        return "out of memory";
    case BZ_DATA_ERROR:
        return "compressed data corrupted";
    case BZ_DATA_ERROR_MAGIC:
        return "not a bzip2 file";
    case BZ_IO_ERROR:
        return strerror(errno);
    case BZ_UNEXPECTED_EOF:
        return "unexpected end of file";
# ifndef NDEBUG
    // these can happen only if our code is bogus
    case BZ_SEQUENCE_ERROR:
        return "internal error: bad call sequence";
    case BZ_PARAM_ERROR:
        return "internal error: param error";
    case BZ_OUTBUFF_FULL:
        return "internal error: buffer full";
    case BZ_CONFIG_ERROR:
        return "internal error: bad config";
    case BZ_OK:
        return "internal_error: OK when not obviously not OK";
    case BZ_RUN_OK:
        return "internal error: unexpected RUN_OK";
    case BZ_FLUSH_OK:
        return "internal error: unexpected FLUSH_OK";
    case BZ_FINISH_OK:
        return "internal error: unexpected FINISH_OK";
    case BZ_STREAM_END:
        return "internal error: unexpected STREAM_END";
# endif
    default:
        return "invalid error?!?";
    }
}

#define ERRbz2(l,f) ERR(l,f,"%s", bzerr(ret))

static int read_bz2(int in, int out, file_info *restrict fi, char *head)
{
    bz_stream st;
    int ret;
    char inbuf[BUFFER_SIZE], outbuf[BUFFER_SIZE];

    bzero(&st, sizeof st);
    if (ret = BZ2_bzDecompressInit(&st, 0, 0))
        ERRbz2(end, in);

    if (head)
    {
        if ((st.avail_in = read(in, inbuf + MLEN, BUFFER_SIZE - MLEN)) == -1)
            ERRlibc(fail, in);
        st.avail_in += MLEN;
        st.next_in = inbuf;
        U64(inbuf) = U64(head);
        goto work;
    }

    while ((st.avail_in = read(in, st.next_in = inbuf, BUFFER_SIZE)) > 0)
    {
work:
        fi->sz += st.avail_in;
        do
        {
            if (ret == BZ_STREAM_END)
                if ((ret = BZ2_bzDecompressEnd(&st))
                    || (ret = BZ2_bzDecompressInit(&st, 0, 0)))
                {
                    ERRbz2(end, in);
                }

            st.next_out  = outbuf;
            st.avail_out = sizeof outbuf;
            if ((ret = BZ2_bzDecompress(&st)) && ret != BZ_STREAM_END)
                ERRbz2(fail, in);

            if (rewrite(out, outbuf, st.next_out - outbuf))
                ERRlibc(fail, out);
            fi->sd += st.next_out - outbuf;
        } while (st.avail_in);
    }
    if (st.avail_in)
        ERRlibc(fail, in);
    if (ret == BZ_STREAM_END)
        goto ok;

    // Flush the stream    
    do
    {
        st.next_out  = outbuf;
        st.avail_out = sizeof outbuf;
        ret = BZ2_bzDecompress(&st);

        if (rewrite(out, outbuf, st.next_out - outbuf))
            ERRlibc(fail, out);
        fi->sd += st.next_out - outbuf;
    } while (!ret && !st.avail_out);
    if (ret == BZ_OK)
        ret = BZ_UNEXPECTED_EOF; // happens on very short files
    if (ret != BZ_STREAM_END)
        ERRbz2(fail, in);
ok:
    BZ2_bzDecompressEnd(&st);
    return 0;

fail:
    BZ2_bzDecompressEnd(&st);
end:
    return 1;
}

static int write_bz2(int in, int out, file_info *restrict fi, char *head)
{
    bz_stream st;
    int ret;
    char inbuf[BUFFER_SIZE], outbuf[BUFFER_SIZE];

    bzero(&st, sizeof st);
    if ((ret = BZ2_bzCompressInit(&st, level?:9, 0, 0)))
        ERRbz2(end, in);

    while ((st.avail_in = read(in, st.next_in = inbuf, BUFFER_SIZE)) > 0)
    {
        fi->sd += st.avail_in;
        do
        {
            st.next_out  = outbuf;
            st.avail_out = sizeof(outbuf);
            if ((ret = BZ2_bzCompress(&st, BZ_RUN)) && ret != BZ_RUN_OK)
                ERRbz2(fail, in);

            if (rewrite(out, outbuf, st.next_out - outbuf))
                ERRlibc(fail, out);
            fi->sz += st.next_out - outbuf;
        } while (st.avail_in);
    }
    if (st.avail_in)
        ERRlibc(fail, in);

    // Flush the stream
    do
    {
        st.next_out  = outbuf;
        st.avail_out = sizeof(outbuf);
        ret = BZ2_bzCompress(&st, BZ_FINISH);

        if (rewrite(out, outbuf, st.next_out - outbuf))
            ERRlibc(fail, out);
        fi->sz += st.next_out - outbuf;
    } while (ret == BZ_FINISH_OK);
    if (ret != BZ_STREAM_END)
        ERRbz2(fail, in);
    BZ2_bzCompressEnd(&st);
    return 0;

fail:
    BZ2_bzCompressEnd(&st);
end:
    return 1;
}
# undef ERRbz2
#endif

#ifdef HAVE_LIBZ
static const char *gzerr(int e)
{
    switch (e)
    {
    case Z_NEED_DICT:
        return "file compressed with a private dictionary";
    case Z_ERRNO:
        return strerror(errno);
    case Z_STREAM_ERROR:
        return "internal error";
    case Z_DATA_ERROR:
        return "file is corrupted";
    case Z_MEM_ERROR:
        return "out of memory";
    case Z_BUF_ERROR:
        return "unexpected end of file";
    case Z_VERSION_ERROR:
        return "unsupported version of gzip";
    default:
        return "invalid error?!?";
    }
}

#define ERRgz(l,f) ERR(l,f,"%s\n", gzerr(ret))

static int read_gz(int in, int out, file_info *restrict fi, char *head)
{
    z_stream st;
    int ret = 0;
    ssize_t len;
    Bytef inbuf[BUFFER_SIZE], outbuf[BUFFER_SIZE];

    bzero(&st, sizeof st);
    if (inflateInit2(&st, 32))
        ERRoom(end, in);

    if (head)
    {
        if ((len = read(in, inbuf + MLEN, BUFFER_SIZE - MLEN)) == -1)
            ERRlibc(fail, in);
        st.avail_in = len + MLEN;
        st.next_in = inbuf;
        U64(inbuf) = U64(head);
        goto work;
    }

    while ((len = read(in, st.next_in = inbuf, BUFFER_SIZE)) > 0)
    {
        st.avail_in = len;
work:
        fi->sz += st.avail_in;
        do
        {
            // concatenated stream => reset stream
            if (ret == Z_STREAM_END)
                if (ret = inflateReset(&st))
                    ERRgz(fail, in);

            st.next_out  = outbuf;
            st.avail_out = sizeof outbuf;
            if ((ret = inflate(&st, Z_NO_FLUSH)) && ret != Z_STREAM_END)
                ERRgz(fail, in);

            if (rewrite(out, outbuf, st.next_out - outbuf))
                ERRlibc(fail, out);
            fi->sd += st.next_out - outbuf;

        } while (st.avail_in);
    }
    if (st.avail_in)
        ERRlibc(fail, in);

    // Flush the stream
    do
    {
        st.next_out  = outbuf;
        st.avail_out = sizeof outbuf;
        ret = inflate(&st, Z_FINISH);

        if (rewrite(out, outbuf, st.next_out - outbuf))
            ERRlibc(fail, out);
        fi->sd += st.next_out - outbuf;
    } while (!ret);
    if (ret != Z_STREAM_END)
        ERRgz(fail, in);
    inflateEnd(&st);
    return 0;

fail:
    inflateEnd(&st);
end:
    return 1;
}

static int write_gz(int in, int out, file_info *restrict fi, char *head)
{
    z_stream st;
    int ret;
    ssize_t len;
    Bytef inbuf[BUFFER_SIZE], outbuf[BUFFER_SIZE];

    bzero(&st, sizeof st);
    if ((ret = deflateInit2(&st, level?:6, Z_DEFLATED, 31, 9, 0)))
        ERRgz(end, in);

    while ((len = read(in, st.next_in = inbuf, BUFFER_SIZE)) > 0)
    {
        st.avail_in = len;
        fi->sd += len;
        do
        {
            st.next_out  = outbuf;
            st.avail_out = sizeof(outbuf);
            if ((ret = deflate(&st, Z_NO_FLUSH)))
                ERRgz(fail, in);

            if (rewrite(out, outbuf, st.next_out - outbuf))
                ERRlibc(fail, out);
            fi->sz += st.next_out - outbuf;
        } while (st.avail_in);
    }
    if (len)
        ERRlibc(fail, in);

    // Flush the stream
    do
    {
        st.next_out  = outbuf;
        st.avail_out = sizeof(outbuf);
        ret = deflate(&st, Z_FINISH);

        if (rewrite(out, outbuf, st.next_out - outbuf))
            ERRlibc(fail, out);
        fi->sz += st.next_out - outbuf;
    } while (!ret);
    if (ret != Z_STREAM_END)
        ERRgz(fail, in);
    deflateEnd(&st);
    return 0;

fail:
    deflateEnd(&st);
end:
    return 1;
}
# undef ERRgz
#endif

#ifdef HAVE_LIBLZMA
static const char *xzerr(lzma_ret e)
{
    switch (e)
    {
    case LZMA_MEM_ERROR:
        return "out of memory";
    case LZMA_MEMLIMIT_ERROR:
        return "memory usage limit was reached";
    case LZMA_FORMAT_ERROR:
        return "file format not recognized";
    case LZMA_OPTIONS_ERROR:
        return "invalid or unsupported options";
    case LZMA_DATA_ERROR:
        return "file is corrupted";
    case LZMA_BUF_ERROR:
        return "unexpected end of file";
    case LZMA_PROG_ERROR:
        return "internal error";
    default:
        return "invalid error?!?";
    }
}

#define ERRxz(l,f) ERR(l,f, "%s\n", xzerr(ret))

static int read_xz(int in, int out, file_info *restrict fi, char *head)
{
    uint8_t inbuf[BUFFER_SIZE], outbuf[BUFFER_SIZE];
    lzma_stream st = LZMA_STREAM_INIT;
    lzma_ret ret = 0;

    if (lzma_stream_decoder(&st, UINT64_MAX, LZMA_CONCATENATED))
        ERRoom(end, in);

    if (head)
    {
        if ((st.avail_in = read(in, inbuf + MLEN, BUFFER_SIZE - MLEN)) == -1)
            ERRlibc(fail, in);
        st.avail_in += MLEN;
        st.next_in = inbuf;
        U64(inbuf) = U64(head);
        goto work;
    }

    while ((st.avail_in = read(in, (uint8_t*)(st.next_in = inbuf), BUFFER_SIZE)) > 0)
    {
work:
        fi->sz += st.avail_in;
        do
        {
            st.next_out  = outbuf;
            st.avail_out = sizeof(outbuf);
            if ((ret = lzma_code(&st, LZMA_RUN)))
                ERRxz(fail, in);

            if (rewrite(out, outbuf, st.next_out - outbuf))
                ERRlibc(fail, out);
            fi->sd += st.next_out - outbuf;
        } while (st.avail_in);
    }
    if (st.avail_in)
        ERRlibc(fail, in);

    // Flush the stream
    do
    {
        st.next_out  = outbuf;
        st.avail_out = sizeof(outbuf);
        ret = lzma_code(&st, LZMA_FINISH);

        if (rewrite(out, outbuf, st.next_out - outbuf))
            ERRlibc(fail, out);
        fi->sd += st.next_out - outbuf;
    } while (!ret);
    if (ret != LZMA_STREAM_END)
        ERRxz(fail, in);
    lzma_end(&st);
    return 0;

fail:
    lzma_end(&st);
end:
    return 1;
}

static int write_xz(int in, int out, file_info *restrict fi, char *head)
{
    uint8_t inbuf[BUFFER_SIZE], outbuf[BUFFER_SIZE];
    lzma_stream st = LZMA_STREAM_INIT;
    lzma_ret ret = 0;

    int xzlevel = level?:6;
    if (xzlevel == 1) // xz level 1 is boring, 0 stands out
        xzlevel = 0;
    if (lzma_easy_encoder(&st, xzlevel, LZMA_CHECK_CRC64))
        ERRoom(end, in);

    while ((st.avail_in = read(in, (uint8_t*)(st.next_in = inbuf), BUFFER_SIZE)) > 0)
    {
        fi->sd += st.avail_in;
        do
        {
            st.next_out  = outbuf;
            st.avail_out = sizeof(outbuf);
            if ((ret = lzma_code(&st, LZMA_RUN)))
                ERRxz(fail, in);

            if (rewrite(out, outbuf, st.next_out - outbuf))
                ERRlibc(fail, out);
            fi->sz += st.next_out - outbuf;
        } while (st.avail_in);
    }
    if (st.avail_in)
        ERRlibc(fail, in);

    // Flush the stream
    do
    {
        st.next_out  = outbuf;
        st.avail_out = sizeof(outbuf);
        ret = lzma_code(&st, LZMA_FINISH);

        if (rewrite(out, outbuf, st.next_out - outbuf))
            ERRlibc(fail, out);
        fi->sz += st.next_out - outbuf;
    } while (!ret);
    if (ret != LZMA_STREAM_END)
        ERRxz(fail, in);
    lzma_end(&st);
    return 0;

fail:
    lzma_end(&st);
end:
    return 1;
}
# undef ERRxz
#endif

#ifdef HAVE_LIBZSTD
#define ERRzstd(l,f) ERR(l,f, "%s\n", ZSTD_getErrorName(r))

static int read_zstd(int in, int out, file_info *restrict fi, char *head)
{
    int err = 1;
    ZSTD_inBuffer  zin;
    ZSTD_outBuffer zout;
    size_t const inbufsz  = ZSTD_DStreamInSize();
    size_t r;
    zin.src = malloc(inbufsz);
    zout.size = ZSTD_DStreamOutSize();
    zout.dst = malloc(zout.size);

    if (!zin.src || !zout.dst)
        ERRoom(end, in);

    ZSTD_DStream* const stream = ZSTD_createDStream();
    if (!stream)
        ERRoom(end, in);
    if (ZSTD_isError(r = ZSTD_initDStream(stream)))
        ERRzstd(fail, in);

    int end_of_frame = 0; // empty file is an error

    if (head)
    {
        if ((r = read(in, (char*)zin.src + MLEN, inbufsz - MLEN)) == -1)
            ERRlibc(fail, in);
        r += MLEN;
        U64(zin.src) = U64(head);
        goto work;
    }

    while ((r = read(in, (void*)zin.src, inbufsz)))
    {
        if (r == -1)
            ERRlibc(fail, in);
work:
        fi->sz += r;
        zin.size = r;
        zin.pos = 0;
        while (zin.pos < zin.size)
        {
            zout.pos = 0;
            if (ZSTD_isError(r = ZSTD_decompressStream(stream, &zout, &zin)))
                ERRzstd(fail, in);
            end_of_frame = !r;
            fi->sd += zout.pos;
            if (rewrite(out, zout.dst, zout.pos))
                ERRlibc(fail, out);
        }
    }

    if (!fi->sz) // old versions of libzstd give a weird error for empty files
        ERRueof(fail, in);

    // flush
    if (!end_of_frame)
    {
        zout.pos = 0;
        if (ZSTD_isError(r = ZSTD_decompressStream(stream, &zout, &zin)))
            ERRzstd(fail, in);
        fi->sd += zout.pos;
        if (rewrite(out, zout.dst, zout.pos))
            ERRlibc(fail, out);
        // write first, fail later -- hopefully salvaging some data
        if (r)
            ERRueof(fail, in);
    }

    err = 0;
fail:
    ZSTD_freeDStream(stream);
end:
    free((void*)zin.src);
    free(zout.dst);
    return err;
}

static int write_zstd(int in, int out, file_info *restrict fi, char *head)
{
    int err = 1;
    ZSTD_inBuffer  zin;
    ZSTD_outBuffer zout;
    size_t const inbufsz  = ZSTD_CStreamInSize();
    size_t r;
    zin.src = malloc(inbufsz);
    zout.size = ZSTD_CStreamOutSize();
    zout.dst = malloc(zout.size);

    if (!zin.src || !zout.dst)
        ERRoom(end, in);

    ZSTD_CStream* const stream = ZSTD_createCStream();
    if (!stream)
        ERRoom(end, in);
    // unlike all other compressors, zstd levels go 1..19 (..22 as "extreme")
    int zlevel = ((level?:2) - 1) * 18 / 8 + 1;
    assert(zlevel <= 19);
    if (ZSTD_isError(r = ZSTD_initCStream(stream, zlevel)))
        ERRzstd(fail, in);
    ZSTD_CCtx_setParameter(stream, ZSTD_c_checksumFlag, 1);

    while ((r = read(in, (void*)zin.src, inbufsz)))
    {
        if (r == -1)
            ERRlibc(fail, in);
        fi->sd += r;
        zin.size = r;
        zin.pos = 0;
        while (zin.pos < zin.size)
        {
            zout.pos = 0;
            if (ZSTD_isError(r = ZSTD_compressStream(stream, &zout, &zin)))
                ERRzstd(fail, in);
            if (rewrite(out, zout.dst, zout.pos))
                ERRlibc(fail, out);
            fi->sz += zout.pos;
        }
    }

    zout.pos = 0;
    if (ZSTD_isError(r = ZSTD_endStream(stream, &zout)))
        ERRzstd(fail, in);
    if (rewrite(out, zout.dst, zout.pos))
        ERRlibc(fail, out);
    fi->sz += zout.pos;

    err = 0;
fail:
    ZSTD_freeCStream(stream);
end:
    free((void*)zin.src);
    free(zout.dst);
    return err;
}
# undef ERRzstd
#endif

static int cat(int in, int out, file_info *restrict fi, char *head)
{
    if (out == -1)
        return 0;

    if (head)
    {
        if (rewrite(out, head, MLEN))
            ERRlibc(end, out);
        fi->sz = fi->sd = MLEN;
    }

    ssize_t r;
#ifdef HAVE_COPY_FILE_RANGE
    while ((r = copy_file_range(in, 0, out, 0, PTRDIFF_MAX, 0)) > 0)
    {
        fi->sz += r;
        fi->sd += r;
    }
    if (!r)
        return 0;
    if (errno != ENOSYS && errno != EINVAL && errno != EXDEV) // EXDEV regressed in 5.19
        ERRlibc(end, in);
#endif

    char buf[BUFFER_SIZE];
    while ((r = read(in, buf, sizeof buf)) > 0)
    {
        if (rewrite(out, buf, r))
            ERRlibc(end, out);
        fi->sz += r;
        fi->sd += r;
    }
    if (r)
        ERRlibc(end, in);
    return 0;

end:
    return 1;
}

compress_info compressors[]={
#ifdef HAVE_LIBZSTD
{"zstd", ".zst",  write_zstd},
#endif
#ifdef HAVE_LIBBZ3
{"bzip3", ".bz3", write_bz3},
#endif
#ifdef HAVE_LIBLZMA
{"xz", ".xz",  write_xz},
#endif
#ifdef HAVE_LIBZ
{"gzip", ".gz",  write_gz},
#endif
#ifdef HAVE_LIBBZ2
{"bzip2", ".bz2", write_bz2},
#endif
{0, 0, 0},
};

compress_info decompressors[]={
#ifdef HAVE_LIBZSTD
{"zstd", ".zst",  read_zstd, {0x28,0xb5,0x2f,0xfd}, {0xff,0xff,0xff,0xff}},
#endif
#ifdef HAVE_LIBBZ3
{"bzip3", ".bz3", read_bz3, "BZ3v1", {0xff,0xff,0xff,0xff,0xff}},
#endif
#ifdef HAVE_LIBLZMA
{"xz", ".xz",  read_xz, {0xfd,0x37,0x7a,0x58,0x5a}, {0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xf0}},
#endif
#ifdef HAVE_LIBZ
{"gzip", ".gz",  read_gz, {0x1f,0x8b,8}, {0xff,0xff,0xff,0xe0}},
#endif
#ifdef HAVE_LIBBZ2
{"bzip2", ".bz2", read_bz2, "BZh01AY&", {0xff,0xff,0xff,0xf0,0xff,0xff,0xff,0xff}},
// empty file has no BlockHeader
{"", "/", read_bz2, "BZh0\x17rE8", {0xff,0xff,0xff,0xf0,0xff,0xff,0xff,0xff}},
#endif
{0, 0, 0},
};

int match_suffix(const char *txt, const char *ext)
{
    int tl,el;

    tl=strlen(txt);
    el=strlen(ext);
    if (tl<=el)
        return 0;
    txt+=tl-el;
    return !strncmp(txt, ext, el);
}

compress_info *comp_by_ext(const char *name, compress_info *ci)
{
    for (;ci->name;ci++)
        if (match_suffix(name, ci->ext))
            return ci;
    return 0;
}

compress_info *comp_by_name(const char *name, compress_info *ci)
{
    for (;ci->name;ci++)
        if (!strcmp(name, ci->name) || !strcmp(name, ci->ext+1))
            return ci;
    return 0;
}

static bool verify_magic(const char *bytes, const compress_info *comp)
{
    return (U64(bytes) & U64(comp->magicmask)) == U64(comp->magic);
}

bool decomp(bool can_cat, int in, int out, file_info*restrict fi)
{
    char buf[MLEN];
    ssize_t r = read(in, buf, MLEN);
    if (r == -1)
        ERRlibc(err, in);
    if (r < MLEN) // shortest legal file is 9 bytes (zstd w/o checksum)
    {
        if (!can_cat)
            ERR(err, in, "not a compressed file");
        if (rewrite(out, buf, r))
            ERRlibc(err, out);
        fi->sd = fi->sz = r;
        return 0;
    }

    for (const compress_info *ci = decompressors; ci->comp; ci++)
        if (verify_magic(buf, ci))
            return ci->comp(in, out, fi, buf);

    if (can_cat)
        return cat(in, out, fi, buf);

    ERR(err, in, "not a compressed file");

err:
    return 1;
}
