#include "config.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
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

#define BUFFER_SIZE 32768

#define ERRoom(l,f) do {fprintf(stderr, "%s: %s%s: Out of memory.\n", exe, fi->path, fi->name_##f);goto l;} while (0)
#define ERRueof(l,f) do {fprintf(stderr, "%s: %s%s: unexpected end of file\n", exe, fi->path, fi->name_##f);goto l;} while (0)
#define ERRlibc(l,f) do {fprintf(stderr, "%s: %s%s: %m\n", exe, fi->path, fi->name_##f);goto l;} while (0)

static int dupa(int fd)
{
    return fcntl(fd, F_DUPFD_CLOEXEC, 0);
}

static int rewrite(int fd, const void *buf, size_t len)
{
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
    default:
        return "invalid error?!?";
    }
}

#define ERRbz2(l,f) do {fprintf(stderr, "%s: %s%s: %s\n", exe, fi->path, fi->name_##f, bzerr(bzerror));goto l;} while (0)

static int read_bz2(int in, int out, file_info *restrict fi)
{
    BZFILE* b;
    FILE*   f = 0;
    int     nBuf;
    char    buf[BUFFER_SIZE];
    int     bzerror;

    if ((in = dupa(in)) == -1)
        ERRlibc(end, in);
    f = fdopen(in, "rb");
    b = BZ2_bzReadOpen(&bzerror, f, 0, 0, NULL, 0);
    if (bzerror)
        ERRbz2(end, in);

    bzerror = BZ_OK;
    while (bzerror == BZ_OK)
    {
        nBuf = BZ2_bzRead(&bzerror, b, buf, BUFFER_SIZE);
        if (out!=-1 && rewrite(out, buf, nBuf))
            ERRlibc(fail, out);
    }
    if (bzerror != BZ_STREAM_END)
        ERRbz2(fail, in);
    BZ2_bzReadClose(&bzerror, b);
    fclose(f);
    return 0;

fail:
    BZ2_bzReadClose(&bzerror, b);
end:
    if (f)
        fclose(f);
    return 1;
}

static int write_bz2(int in, int out, file_info *restrict fi)
{
    BZFILE* b;
    FILE*   f = 0;
    int     nBuf;
    char    buf[BUFFER_SIZE];
    int     bzerror;

    if ((out = dupa(out)) == -1)
        ERRlibc(end, out);
    f = fdopen(out, "wb");
    b = BZ2_bzWriteOpen(&bzerror, f, level?:9, 0, 0);
    if (bzerror)
        ERRbz2(end, out);

    bzerror = BZ_OK;
    while ((nBuf = read(in, buf, BUFFER_SIZE)) > 0)
    {
        BZ2_bzWrite(&bzerror, b, buf, nBuf);
        if (bzerror)
            ERRbz2(fail, out);
    }
    if (nBuf)
        ERRlibc(fail, in);
    BZ2_bzWriteClose(&bzerror, b, 0,0,0);
    if (bzerror)
        ERRbz2(end, out);
    if (fclose(f))
    {
        f = 0;
        ERRlibc(end, out);
    }
    return 0;

fail:
    BZ2_bzWriteClose(&bzerror, b, 0,0,0);
end:
    if (f)
        fclose(f);
    return 1;
}
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

#define ERRgz(l,f) do {fprintf(stderr, "%s: %s%s: %s\n", exe, fi->path, fi->name_##f, gzerr(ret));goto l;} while (0)

static int read_gz(int in, int out, file_info *restrict fi)
{
    z_stream st;
    int ret;
    Bytef inbuf[BUFFER_SIZE], outbuf[BUFFER_SIZE];

    bzero(&st, sizeof st);
    if (inflateInit2(&st, 32))
        ERRoom(end, in);

    while (st.avail_in
           || (st.avail_in = read(in, st.next_in = inbuf, BUFFER_SIZE)) > 0)
    {
        st.next_out  = outbuf;
        st.avail_out = sizeof outbuf;
        if ((ret = inflate(&st, Z_NO_FLUSH)) && ret != Z_STREAM_END)
            ERRgz(fail, in);

        if (out!=-1 && rewrite(out, outbuf, st.next_out - outbuf))
            ERRlibc(fail, out);
    }
    if (st.avail_in)
        ERRlibc(fail, in);

    // Flush the stream
    do
    {
        st.next_out  = outbuf;
        st.avail_out = sizeof outbuf;
        ret = inflate(&st, Z_FINISH);

        if (out!=-1 && rewrite(out, outbuf, st.next_out - outbuf))
            ERRlibc(fail, out);
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

static int write_gz(int in, int out, file_info *restrict fi)
{
    z_stream st;
    int ret;
    Bytef inbuf[BUFFER_SIZE], outbuf[BUFFER_SIZE];

    bzero(&st, sizeof st);
    if ((ret = deflateInit2(&st, level?:6, Z_DEFLATED, 31, 9, 0)))
        ERRgz(end, in);

    while (st.avail_in
           || (st.avail_in = read(in, st.next_in = inbuf, BUFFER_SIZE)) > 0)
    {
        st.next_out  = outbuf;
        st.avail_out = sizeof(outbuf);
        if ((ret = deflate(&st, Z_NO_FLUSH)))
            ERRgz(fail, in);

        if (rewrite(out, outbuf, st.next_out - outbuf))
            ERRlibc(fail, out);
    }
    if (st.avail_in)
        ERRlibc(fail, in);

    // Flush the stream
    do
    {
        st.next_out  = outbuf;
        st.avail_out = sizeof(outbuf);
        ret = deflate(&st, Z_FINISH);

        if (rewrite(out, outbuf, st.next_out - outbuf))
            ERRlibc(fail, out);
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

#define ERRxz(l,f) do {fprintf(stderr, "%s: %s%s: %s\n", exe, fi->path, fi->name_##f, xzerr(ret));goto l;} while (0)

static int read_xz(int in, int out, file_info *restrict fi)
{
    uint8_t inbuf[BUFFER_SIZE], outbuf[BUFFER_SIZE];
    lzma_stream xz = LZMA_STREAM_INIT;
    lzma_ret ret = 0;

    if (lzma_stream_decoder(&xz, UINT64_MAX, LZMA_CONCATENATED))
        ERRoom(end, in);

    xz.avail_in  = 0;

    while (xz.avail_in
           || (xz.avail_in = read(in, (uint8_t*)(xz.next_in = inbuf), BUFFER_SIZE)) > 0)
    {
        xz.next_out  = outbuf;
        xz.avail_out = sizeof(outbuf);
        if ((ret = lzma_code(&xz, LZMA_RUN)))
            ERRxz(fail, in);

        if (out!=-1 && rewrite(out, outbuf, xz.next_out - outbuf))
            ERRlibc(fail, out);
    }
    if (xz.avail_in)
        ERRlibc(fail, in);

    // Flush the stream
    do
    {
        xz.next_out  = outbuf;
        xz.avail_out = sizeof(outbuf);
        ret = lzma_code(&xz, LZMA_FINISH);

        if (out!=-1 && rewrite(out, outbuf, xz.next_out - outbuf))
            ERRlibc(fail, out);
    } while (!ret);
    if (ret != LZMA_STREAM_END)
        ERRxz(fail, in);
    lzma_end(&xz);
    return 0;

fail:
    lzma_end(&xz);
end:
    return 1;
}

static int write_xz(int in, int out, file_info *restrict fi)
{
    uint8_t inbuf[BUFFER_SIZE], outbuf[BUFFER_SIZE];
    lzma_stream xz = LZMA_STREAM_INIT;
    lzma_ret ret = 0;

    if (lzma_easy_encoder(&xz, level?:6, LZMA_CHECK_CRC64))
        ERRoom(end, in);

    xz.avail_in  = 0;

    while (xz.avail_in
           || (xz.avail_in = read(in, (uint8_t*)(xz.next_in = inbuf), BUFFER_SIZE)) > 0)
    {
        xz.next_out  = outbuf;
        xz.avail_out = sizeof(outbuf);
        if ((ret = lzma_code(&xz, LZMA_RUN)))
            ERRxz(fail, in);

        if (rewrite(out, outbuf, xz.next_out - outbuf))
            ERRlibc(fail, out);
    }
    if (xz.avail_in)
        ERRlibc(fail, in);

    // Flush the stream
    do
    {
        xz.next_out  = outbuf;
        xz.avail_out = sizeof(outbuf);
        ret = lzma_code(&xz, LZMA_FINISH);

        if (rewrite(out, outbuf, xz.next_out - outbuf))
            ERRlibc(fail, out);
    } while (!ret);
    if (ret != LZMA_STREAM_END)
        ERRxz(fail, in);
    lzma_end(&xz);
    return 0;

fail:
    lzma_end(&xz);
end:
    return 1;
}
#endif

#ifdef HAVE_LIBZSTD
#define ERRzstd(l,f) do {fprintf(stderr, "%s: %s%s: %s\n", exe, fi->path, fi->name_##f, ZSTD_getErrorName(r));goto l;} while (0)

static int read_zstd(int in, int out, file_info *restrict fi)
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
    while ((r = read(in, (void*)zin.src, inbufsz)))
    {
        if (r == -1)
            ERRlibc(fail, in);
        zin.size = r;
        zin.pos = 0;
        while (zin.pos < zin.size)
        {
            zout.pos = 0;
            if (ZSTD_isError(r = ZSTD_decompressStream(stream, &zout, &zin)))
                ERRzstd(fail, in);
            end_of_frame = !r;
            if (out!=-1 && rewrite(out, zout.dst, zout.pos))
                ERRlibc(fail, out);
        }
    }

    // flush
    if (!end_of_frame)
    {
        zout.pos = 0;
        if (ZSTD_isError(r = ZSTD_decompressStream(stream, &zout, &zin)))
            ERRzstd(fail, in);
        if (out!=-1 && rewrite(out, zout.dst, zout.pos))
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

static int write_zstd(int in, int out, file_info *restrict fi)
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
    level = (level - 1) * 18 / 8 + 1;
    assert(level <= 19);
    if (ZSTD_isError(r = ZSTD_initCStream(stream, level?:3)))
        ERRzstd(fail, in);

    while ((r = read(in, (void*)zin.src, inbufsz)))
    {
        if (r == -1)
            ERRlibc(fail, in);
        zin.size = r;
        zin.pos = 0;
        while (zin.pos < zin.size)
        {
            zout.pos = 0;
            if (ZSTD_isError(r = ZSTD_compressStream(stream, &zout, &zin)))
                ERRzstd(fail, in);
            if (rewrite(out, zout.dst, zout.pos))
                ERRlibc(fail, out);
        }
    }

    zout.pos = 0;
    if (ZSTD_isError(r = ZSTD_endStream(stream, &zout)))
        ERRzstd(fail, in);
    if (rewrite(out, zout.dst, zout.pos))
        ERRlibc(fail, out);

    err = 0;
fail:
    ZSTD_freeCStream(stream);
end:
    free((void*)zin.src);
    free(zout.dst);
    return err;
}
#endif

compress_info compressors[]={
#ifdef HAVE_LIBZ
{"gzip", ".gz",  write_gz},
#endif
#ifdef HAVE_LIBBZ2
{"bzip2", ".bz2", write_bz2},
#endif
#ifdef HAVE_LIBLZMA
{"xz", ".xz",  write_xz},
#endif
#ifdef HAVE_LIBZSTD
{"zstd", ".zst",  write_zstd},
#endif
{0, 0, 0},
};

compress_info decompressors[]={
#ifdef HAVE_LIBZ
{"gzip", ".gz",  read_gz},
#endif
#ifdef HAVE_LIBBZ2
{"bzip2", ".bz2", read_bz2},
#endif
#ifdef HAVE_LIBLZMA
{"xz", ".xz",  read_xz},
#endif
#ifdef HAVE_LIBZSTD
{"zstd", ".zst",  read_zstd},
#endif
{0, 0, 0},
};

static int match_suffix(const char *txt, const char *ext)
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
