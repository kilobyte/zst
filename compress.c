#include "config.h"
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
#include "prefix.h"
#include "zst.h"

#define ERRORMSG(x) fprintf(stderr, (x))
#define _(x) (x)

#define BUFFER_SIZE 32768

#define ERRoom(l) do {fprintf(stderr, "%s: %s%s: Out of memory.\n", exe, path, name);goto l;} while (0)
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
static int read_bz2(int in, int out, const char *path, const char *name)
{
    BZFILE* b;
    int     nBuf;
    char    buf[BUFFER_SIZE];
    int     bzerror;

    b = BZ2_bzReadOpen(&bzerror, fdopen(dupa(in),"rb"), 0, 0, NULL, 0);
    if (bzerror != BZ_OK)
    {
        BZ2_bzReadClose(&bzerror, b);
        // error
        ERRORMSG(_("Invalid/corrupt .bz2 file.\n"));
        return 1;
    }

    bzerror = BZ_OK;
    while (bzerror == BZ_OK)
    {
        nBuf = BZ2_bzRead(&bzerror, b, buf, BUFFER_SIZE);
        if (out!=-1 && rewrite(out, buf, nBuf))
        {
            BZ2_bzReadClose(&bzerror, b);
            return 1;
        }
    }
    if (bzerror != BZ_STREAM_END)
    {
        BZ2_bzReadClose(&bzerror, b);
        // error
        ERRORMSG("\033[0m");
        ERRORMSG(_("bzip2: Error during decompression.\n"));
        return 1;
    }
    BZ2_bzReadClose(&bzerror, b);
    return bzerror != BZ_OK;
}

static int write_bz2(int in, int out, const char *path, const char *name)
{
    BZFILE* b;
    int     nBuf;
    char    buf[BUFFER_SIZE];
    int     bzerror;

    b = BZ2_bzWriteOpen(&bzerror, fdopen(dupa(out),"wb"), 9, 0, 0);
    if (bzerror != BZ_OK)
    {
        BZ2_bzWriteClose(&bzerror, b, 0,0,0);
        return 1;
    }

    bzerror = BZ_OK;
    while ((nBuf=read(in, buf, BUFFER_SIZE))>0)
    {
        BZ2_bzWrite(&bzerror, b, buf, nBuf);
        if (bzerror != BZ_OK)
        {
            BZ2_bzWriteClose(&bzerror, b, 0,0,0);
            return 1;
        }
    }
    BZ2_bzWriteClose(&bzerror, b, 0,0,0);
    return bzerror != BZ_OK;
}
#endif

#ifdef HAVE_LIBZ
static int read_gz(int in, int out, const char *path, const char *name)
{
    gzFile  g;
    int     nBuf;
    char    buf[BUFFER_SIZE];

    g=gzdopen(dupa(in), "rb");
    if (!g)
    {
        ERRORMSG(_("Invalid/corrupt .gz file.\n"));
        return 1;
    }
    while ((nBuf=gzread(g, buf, BUFFER_SIZE))>0)
    {
        if (out!=-1 && rewrite(out, buf, nBuf))
        {
            gzclose(g);
            return 1;
        }
    }
    if (nBuf)
    {
        ERRORMSG("\033[0m");
        ERRORMSG(_("gzip: Error during decompression.\n"));
    }
    gzclose(g);
    return !!nBuf;
}

static int write_gz(int in, int out, const char *path, const char *name)
{
    gzFile  g;
    int     nBuf;
    char    buf[BUFFER_SIZE];

    g=gzdopen(dupa(out), "wb9");
    if (!g)
        return 1;
    while ((nBuf=read(in, buf, BUFFER_SIZE))>0)
    {
        if (gzwrite(g, buf, nBuf)!=nBuf)
        {
            gzclose(g);
            return 1;
        }
    }
    return !!gzclose(g);
}
#endif

#ifdef HAVE_LIBLZMA
static int read_xz(int in, int out, const char *path, const char *name)
{
    uint8_t inbuf[BUFFER_SIZE], outbuf[BUFFER_SIZE];
    lzma_stream xz = LZMA_STREAM_INIT;
    lzma_ret ret = 0;

    if (lzma_stream_decoder(&xz, UINT64_MAX, LZMA_CONCATENATED) != LZMA_OK)
        goto xz_read_end;

    xz.avail_in  = 0;

    while (xz.avail_in
           || (xz.avail_in = read(in, (uint8_t*)(xz.next_in = inbuf), BUFFER_SIZE)) > 0)
    {
        xz.next_out  = outbuf;
        xz.avail_out = sizeof(outbuf);
        if (lzma_code(&xz, LZMA_RUN) != LZMA_OK)
            goto xz_read_lzma_end;

        if (out!=-1 && rewrite(out, outbuf, xz.next_out - outbuf))
            goto xz_read_lzma_end;
    }

    // Flush the stream
    do
    {
        xz.next_out  = outbuf;
        xz.avail_out = sizeof(outbuf);
        ret = lzma_code(&xz, LZMA_FINISH);

        if (out!=-1 && rewrite(out, outbuf, xz.next_out - outbuf))
            goto xz_read_lzma_end;
    }
    while (ret == LZMA_OK);

xz_read_lzma_end:
    lzma_end(&xz);

xz_read_end:
    return ret != LZMA_STREAM_END;
}

static int write_xz(int in, int out, const char *path, const char *name)
{
    uint8_t inbuf[BUFFER_SIZE], outbuf[BUFFER_SIZE];
    lzma_stream xz = LZMA_STREAM_INIT;
    lzma_ret ret = 0;

    if (lzma_easy_encoder(&xz, 6, LZMA_CHECK_CRC64) != LZMA_OK)
        goto xz_write_end;

    xz.avail_in  = 0;

    while (xz.avail_in
           || (xz.avail_in = read(in, (uint8_t*)(xz.next_in = inbuf), BUFFER_SIZE)) > 0)
    {
        xz.next_out  = outbuf;
        xz.avail_out = sizeof(outbuf);
        if (lzma_code(&xz, LZMA_RUN) != LZMA_OK)
            goto xz_write_lzma_end;

        if (rewrite(out, outbuf, xz.next_out - outbuf))
            goto xz_write_lzma_end;
    }

    // Flush the stream
    do
    {
        xz.next_out  = outbuf;
        xz.avail_out = sizeof(outbuf);
        ret = lzma_code(&xz, LZMA_FINISH);

        if (rewrite(out, outbuf, xz.next_out - outbuf))
            goto xz_write_lzma_end;
    }
    while (ret == LZMA_OK);

xz_write_lzma_end:
    lzma_end(&xz);

xz_write_end:
    return ret == LZMA_STREAM_END;
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
        ERRoom(zstd_r_no_stream);

    ZSTD_DStream* const stream = ZSTD_createDStream();
    if (!stream)
        ERRoom(zstd_r_no_stream);
    if (ZSTD_isError(r = ZSTD_initDStream(stream)))
        ERRzstd(zstd_r_error);

    while ((r = read(in, (void*)zin.src, inbufsz)))
    {
        if (r == -1)
            ERRlibc(zstd_r_error);
        zin.size = r;
        zin.pos = 0;
        while (zin.pos < zin.size)
        {
            zout.pos = 0;
            if (ZSTD_isError(r = ZSTD_decompressStream(stream, &zout, &zin)))
                ERRzstd(zstd_r_error);
            if (out!=-1 && rewrite(out, zout.dst, zout.pos))
                ERRlibc(zstd_r_error);
        }
    }

    err = 0;
zstd_r_error:
    ZSTD_freeDStream(stream);
zstd_r_no_stream:
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
        ERRoom(zstd_w_no_stream);

    ZSTD_CStream* const stream = ZSTD_createCStream();
    if (!stream)
        ERRoom(zstd_w_no_stream);
    if (ZSTD_isError(r = ZSTD_initCStream(stream, 3)))
        ERRzstd(zstd_w_error);

    while ((r = read(in, (void*)zin.src, inbufsz)))
    {
        if (r == -1)
            ERRlibc(zstd_w_error);
        zin.size = r;
        zin.pos = 0;
        while (zin.pos < zin.size)
        {
            zout.pos = 0;
            if (ZSTD_isError(r = ZSTD_compressStream(stream, &zout, &zin)))
                ERRzstd(zstd_w_error);
            if (rewrite(out, zout.dst, zout.pos))
                ERRlibc(zstd_w_error);
        }
    }

    zout.pos = 0;
    if (ZSTD_isError(r = ZSTD_endStream(stream, &zout)))
        ERRzstd(zstd_w_error);
    if (rewrite(out, zout.dst, zout.pos))
        ERRlibc(zstd_w_error);

    err = 0;
zstd_w_error:
    ZSTD_freeCStream(stream);
zstd_w_no_stream:
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

compress_info *comp_from_ext(const char *name, compress_info *ci)
{
    for (;ci->name;ci++)
        if (match_suffix(name, ci->ext))
            return ci;
    return 0;
}
