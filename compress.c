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

#define ERRoom(l) do {fprintf(stderr, "%s: %s%s: Out of memory.\n", exe, path, name);goto l;} while (0)
#define ERRueof(l) do {fprintf(stderr, "%s: %s%s: unexpected end of file\n", exe, path, name);goto l;} while (0)
#define ERRlibc(l) do {fprintf(stderr, "%s: %s%s: %m\n", exe, path, name);goto l;} while (0)

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

#define ERRbz2(l) do {fprintf(stderr, "%s: %s%s: %s\n", exe, path, name, bzerr(bzerror));goto l;} while (0)

static int read_bz2(int in, int out, const char *path, const char *name)
{
    BZFILE* b;
    FILE*   f = 0;
    int     nBuf;
    char    buf[BUFFER_SIZE];
    int     bzerror;

    if ((in = dupa(in)) == -1)
        ERRlibc(end);
    f = fdopen(in, "rb");
    b = BZ2_bzReadOpen(&bzerror, f, 0, 0, NULL, 0);
    if (bzerror)
        ERRbz2(end);

    bzerror = BZ_OK;
    while (bzerror == BZ_OK)
    {
        nBuf = BZ2_bzRead(&bzerror, b, buf, BUFFER_SIZE);
        if (out!=-1 && rewrite(out, buf, nBuf))
            ERRlibc(fail);
    }
    if (bzerror != BZ_STREAM_END)
        ERRbz2(fail);
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

static int write_bz2(int in, int out, const char *path, const char *name)
{
    BZFILE* b;
    FILE*   f = 0;
    int     nBuf;
    char    buf[BUFFER_SIZE];
    int     bzerror;

    if ((out = dupa(out)) == -1)
        ERRlibc(end);
    f = fdopen(out, "wb");
    b = BZ2_bzWriteOpen(&bzerror, f, level?:9, 0, 0);
    if (bzerror)
        ERRbz2(end);

    bzerror = BZ_OK;
    while ((nBuf = read(in, buf, BUFFER_SIZE)) > 0)
    {
        BZ2_bzWrite(&bzerror, b, buf, nBuf);
        if (bzerror)
            ERRbz2(fail);
    }
    if (nBuf)
        ERRlibc(fail);
    BZ2_bzWriteClose(&bzerror, b, 0,0,0);
    if (bzerror)
        ERRbz2(end);
    if (fclose(f))
    {
        f = 0;
        ERRlibc(end);
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
static int read_gz(int in, int out, const char *path, const char *name)
{
    gzFile  g;
    int     r;
    char    buf[BUFFER_SIZE];

    if ((in = dupa(in)) == -1)
        ERRlibc(end);
    if (!(g = gzdopen(in, "rb")))
        ERRoom(end);
    while ((r = gzread(g, buf, BUFFER_SIZE)) > 0)
    {
        if (out!=-1 && rewrite(out, buf, r))
            ERRlibc(fail);
    }
    if (r)
    {
        fprintf(stderr, "%s: %s%s: %s\n", exe, path, name, gzerror(g, 0));
        goto fail;
    }

    if ((r = gzclose(g)))
    {
        // other errors shouldn't happen here
        if (r == Z_BUF_ERROR)
            fprintf(stderr, "%s: %s%s: unexpected end of file\n", exe, path, name);
        else
            fprintf(stderr, "%s: %s%s: gzclose returned %d\n", exe, path, name, r);
        return 1;
    }
    return 0;

fail:
    gzclose(g);
end:
    return 1;
}

static int write_gz(int in, int out, const char *path, const char *name)
{
    gzFile  g;
    int     r;
    char    buf[BUFFER_SIZE];

    if ((out = dupa(out)) == -1)
        ERRlibc(end);
    char mode[4] = "6wb";
    if (level)
        mode[0] = level + '0';
    g = gzdopen(out, mode);
    if (!g)
        ERRlibc(end);
    while ((r = read(in, buf, BUFFER_SIZE)) > 0)
    {
        if (gzwrite(g, buf, r) != r)
        {
            int err = 0;
            const char *msg = gzerror(g, &err);
            if (err == -1)
                ERRlibc(fail);
            fprintf(stderr, "%s: %s%s: %s\n", exe, path, name, msg);
            goto fail;
        }
    }
    if (r)
        ERRlibc(fail);
    if ((r = gzclose(g)))
    {
        if (r == -1)
            ERRlibc(end);
        fprintf(stderr, "%s: %s%s: gzclose returned %d\n", exe, path, name, r);
        return 1;
    }
    return 0;

fail:
    gzclose(g);
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

#define ERRxz(l) do {fprintf(stderr, "%s: %s%s: %s\n", exe, path, name, xzerr(ret));goto l;} while (0)

static int read_xz(int in, int out, const char *path, const char *name)
{
    uint8_t inbuf[BUFFER_SIZE], outbuf[BUFFER_SIZE];
    lzma_stream xz = LZMA_STREAM_INIT;
    lzma_ret ret = 0;

    if (lzma_stream_decoder(&xz, UINT64_MAX, LZMA_CONCATENATED))
        ERRoom(end);

    xz.avail_in  = 0;

    while (xz.avail_in
           || (xz.avail_in = read(in, (uint8_t*)(xz.next_in = inbuf), BUFFER_SIZE)) > 0)
    {
        xz.next_out  = outbuf;
        xz.avail_out = sizeof(outbuf);
        if ((ret = lzma_code(&xz, LZMA_RUN)))
            ERRxz(fail);

        if (out!=-1 && rewrite(out, outbuf, xz.next_out - outbuf))
            ERRlibc(fail);
    }
    if (xz.avail_in)
        ERRlibc(fail);

    // Flush the stream
    do
    {
        xz.next_out  = outbuf;
        xz.avail_out = sizeof(outbuf);
        ret = lzma_code(&xz, LZMA_FINISH);

        if (out!=-1 && rewrite(out, outbuf, xz.next_out - outbuf))
            ERRlibc(fail);
    }
    while (!ret);
    if (ret != LZMA_STREAM_END)
        ERRxz(fail);
    lzma_end(&xz);
    return 0;

fail:
    lzma_end(&xz);
end:
    return 1;
}

static int write_xz(int in, int out, const char *path, const char *name)
{
    uint8_t inbuf[BUFFER_SIZE], outbuf[BUFFER_SIZE];
    lzma_stream xz = LZMA_STREAM_INIT;
    lzma_ret ret = 0;

    if (lzma_easy_encoder(&xz, level?:6, LZMA_CHECK_CRC64))
        ERRoom(end);

    xz.avail_in  = 0;

    while (xz.avail_in
           || (xz.avail_in = read(in, (uint8_t*)(xz.next_in = inbuf), BUFFER_SIZE)) > 0)
    {
        xz.next_out  = outbuf;
        xz.avail_out = sizeof(outbuf);
        if ((ret = lzma_code(&xz, LZMA_RUN)))
            ERRxz(fail);

        if (rewrite(out, outbuf, xz.next_out - outbuf))
            ERRlibc(fail);
    }
    if (xz.avail_in)
        ERRlibc(fail);

    // Flush the stream
    do
    {
        xz.next_out  = outbuf;
        xz.avail_out = sizeof(outbuf);
        ret = lzma_code(&xz, LZMA_FINISH);

        if (rewrite(out, outbuf, xz.next_out - outbuf))
            ERRlibc(fail);
    }
    while (!ret);
    if (ret != LZMA_STREAM_END)
        ERRxz(fail);
    lzma_end(&xz);
    return 0;

fail:
    lzma_end(&xz);
end:
    return 1;
}
#endif

#ifdef HAVE_LIBZSTD
#define ERRzstd(l) do {fprintf(stderr, "%s: %s%s: %s\n", exe, path, name, ZSTD_getErrorName(r));goto l;} while (0)

static int read_zstd(int in, int out, const char *path, const char *name)
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
        ERRoom(end);

    ZSTD_DStream* const stream = ZSTD_createDStream();
    if (!stream)
        ERRoom(end);
    if (ZSTD_isError(r = ZSTD_initDStream(stream)))
        ERRzstd(fail);

    int end_of_frame = 0; // empty file is an error
    while ((r = read(in, (void*)zin.src, inbufsz)))
    {
        if (r == -1)
            ERRlibc(fail);
        zin.size = r;
        zin.pos = 0;
        while (zin.pos < zin.size)
        {
            zout.pos = 0;
            if (ZSTD_isError(r = ZSTD_decompressStream(stream, &zout, &zin)))
                ERRzstd(fail);
            end_of_frame = !r;
            if (out!=-1 && rewrite(out, zout.dst, zout.pos))
                ERRlibc(fail);
        }
    }

    // flush
    if (!end_of_frame)
    {
        zout.pos = 0;
        if (ZSTD_isError(r = ZSTD_decompressStream(stream, &zout, &zin)))
            ERRzstd(fail);
        if (out!=-1 && rewrite(out, zout.dst, zout.pos))
            ERRlibc(fail);
        // write first, fail later -- hopefully salvaging some data
        if (r)
            ERRueof(fail);
    }

    err = 0;
fail:
    ZSTD_freeDStream(stream);
end:
    free((void*)zin.src);
    free(zout.dst);
    return err;
}

static int write_zstd(int in, int out, const char *path, const char *name)
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
        ERRoom(end);

    ZSTD_CStream* const stream = ZSTD_createCStream();
    if (!stream)
        ERRoom(end);
    // unlike all other compressors, zstd levels go 1..19 (..22 as "extreme")
    level = (level - 1) * 18 / 8 + 1;
    assert(level <= 19);
    if (ZSTD_isError(r = ZSTD_initCStream(stream, level?:3)))
        ERRzstd(fail);

    while ((r = read(in, (void*)zin.src, inbufsz)))
    {
        if (r == -1)
            ERRlibc(fail);
        zin.size = r;
        zin.pos = 0;
        while (zin.pos < zin.size)
        {
            zout.pos = 0;
            if (ZSTD_isError(r = ZSTD_compressStream(stream, &zout, &zin)))
                ERRzstd(fail);
            if (rewrite(out, zout.dst, zout.pos))
                ERRlibc(fail);
        }
    }

    zout.pos = 0;
    if (ZSTD_isError(r = ZSTD_endStream(stream, &zout)))
        ERRzstd(fail);
    if (rewrite(out, zout.dst, zout.pos))
        ERRlibc(fail);

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
