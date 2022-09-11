#include "config.h"
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

#define ERRORMSG(x) fprintf(stderr, (x))
#define _(x) (x)

#define BUFFER_SIZE 32768

#ifdef HAVE_LIBBZ2
static void read_bz2(int in, int out, const char *arg)
{
    BZFILE* b;
    int     nBuf;
    char    buf[BUFFER_SIZE];
    int     bzerror;

    b = BZ2_bzReadOpen(&bzerror, fdopen(in,"rb"), 0, 0, NULL, 0);
    if (bzerror != BZ_OK)
    {
        BZ2_bzReadClose(&bzerror, b);
        // error
        ERRORMSG(_("Invalid/corrupt .bz2 file.\n"));
        close(out);
        return;
    }

    bzerror = BZ_OK;
    while (bzerror == BZ_OK)
    {
        nBuf = BZ2_bzRead(&bzerror, b, buf, BUFFER_SIZE);
        if (write(out, buf, nBuf)!=nBuf)
        {
            BZ2_bzReadClose(&bzerror, b);
            close(out);
            return;
        }
    }
    if (bzerror != BZ_STREAM_END)
    {
        BZ2_bzReadClose(&bzerror, b);
        // error
        ERRORMSG("\033[0m");
        ERRORMSG(_("bzip2: Error during decompression.\n"));
    }
    else
        BZ2_bzReadClose(&bzerror, b);
    close(out);
}

static void write_bz2(int in, int out, const char *arg)
{
    BZFILE* b;
    int     nBuf;
    char    buf[BUFFER_SIZE];
    int     bzerror;

    b = BZ2_bzWriteOpen(&bzerror, fdopen(out,"wb"), 9, 0, 0);
    if (bzerror != BZ_OK)
    {
        BZ2_bzWriteClose(&bzerror, b, 0,0,0);
        // error
        // the writer will get smitten with sigpipe
        close(in);
        return;
    }

    bzerror = BZ_OK;
    while ((nBuf=read(in, buf, BUFFER_SIZE))>0)
    {
        BZ2_bzWrite(&bzerror, b, buf, nBuf);
        if (bzerror!=BZ_OK)
        {
            BZ2_bzWriteClose(&bzerror, b, 0,0,0);
            close(in);
            return;
        }
    }
    BZ2_bzWriteClose(&bzerror, b, 0,0,0);
    close(in);
}
#endif

#ifdef HAVE_LIBZ
static void read_gz(int in, int out, const char *arg)
{
    gzFile  g;
    int     nBuf;
    char    buf[BUFFER_SIZE];

    g=gzdopen(in, "rb");
    if (!g)
    {
        ERRORMSG(_("Invalid/corrupt .gz file.\n"));
        close(in);
        close(out);
        return;
    }
    while ((nBuf=gzread(g, buf, BUFFER_SIZE))>0)
    {
        if (write(out, buf, nBuf)!=nBuf)
        {
            gzclose(g);
            close(out);
            return;
        }
    }
    if (nBuf)
    {
        ERRORMSG("\033[0m");
        ERRORMSG(_("gzip: Error during decompression.\n"));
    }
    gzclose(g);
    close(out);
}

static void write_gz(int in, int out, const char *arg)
{
    gzFile  g;
    int     nBuf;
    char    buf[BUFFER_SIZE];

    g=gzdopen(out, "wb9");
    if (!g)
    {
        close(in);
        close(out);
        return;
    }
    while ((nBuf=read(in, buf, BUFFER_SIZE))>0)
    {
        if (gzwrite(g, buf, nBuf)!=nBuf)
        {
            gzclose(g);
            close(in);
            return;
        }
    }
    gzclose(g);
    close(in);
}
#endif

#ifdef HAVE_LIBLZMA
static void read_xz(int in, int out, const char *arg)
{
    uint8_t inbuf[BUFFER_SIZE], outbuf[BUFFER_SIZE];
    lzma_stream xz = LZMA_STREAM_INIT;

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

        if (write(out, outbuf, xz.next_out - outbuf) != xz.next_out - outbuf)
            goto xz_read_lzma_end;
    }

    // Flush the stream
    lzma_ret ret;
    do
    {
        xz.next_out  = outbuf;
        xz.avail_out = sizeof(outbuf);
        ret = lzma_code(&xz, LZMA_FINISH);

        if (write(out, outbuf, xz.next_out - outbuf) != xz.next_out - outbuf)
            goto xz_read_lzma_end;
    }
    while (ret == LZMA_OK);

xz_read_lzma_end:
    lzma_end(&xz);

xz_read_end:
    close(in);
    close(out);
}

static void write_xz(int in, int out, const char *arg)
{
    uint8_t inbuf[BUFFER_SIZE], outbuf[BUFFER_SIZE];
    lzma_stream xz = LZMA_STREAM_INIT;

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

        if (write(out, outbuf, xz.next_out - outbuf) != xz.next_out - outbuf)
            goto xz_write_lzma_end;
    }

    // Flush the stream
    lzma_ret ret;
    do
    {
        xz.next_out  = outbuf;
        xz.avail_out = sizeof(outbuf);
        ret = lzma_code(&xz, LZMA_FINISH);

        if (write(out, outbuf, xz.next_out - outbuf) != xz.next_out - outbuf)
            goto xz_write_lzma_end;
    }
    while (ret == LZMA_OK);

xz_write_lzma_end:
    lzma_end(&xz);

xz_write_end:
    close(in);
    close(out);
}
#endif

#ifdef HAVE_LIBZSTD
static void read_zstd(int in, int out, const char *arg)
{
    ZSTD_inBuffer  zin;
    ZSTD_outBuffer zout;
    size_t const inbufsz  = ZSTD_DStreamInSize();
    zin.src = malloc(inbufsz);
    zout.size = ZSTD_DStreamOutSize();
    zout.dst = malloc(zout.size);

    if (!zin.src || !zout.dst)
        goto zstd_r_no_stream;

    ZSTD_DStream* const stream = ZSTD_createDStream();
    if (!stream)
        goto zstd_r_no_stream;
    if (ZSTD_isError(ZSTD_initDStream(stream)))
        goto zstd_r_error;

    size_t s;
    while ((s = read(in, (void*)zin.src, inbufsz)) > 0)
    {
        zin.size = s;
        zin.pos = 0;
        while (zin.pos < zin.size)
        {
            zout.pos = 0;
            size_t w = ZSTD_decompressStream(stream, &zout, &zin);
            if (ZSTD_isError(w))
                goto zstd_r_error;
            if (write(out, zout.dst, zout.pos) != (ssize_t)zout.pos)
                goto zstd_r_error;
        }
    }

zstd_r_error:
    ZSTD_freeDStream(stream);
zstd_r_no_stream:
    free((void*)zin.src);
    free(zout.dst);
    close(in);
    close(out);
}

static void write_zstd(int in, int out, const char *arg)
{
    ZSTD_inBuffer  zin;
    ZSTD_outBuffer zout;
    size_t const inbufsz  = ZSTD_CStreamInSize();
    zin.src = malloc(inbufsz);
    zout.size = ZSTD_CStreamOutSize();
    zout.dst = malloc(zout.size);

    if (!zin.src || !zout.dst)
        goto zstd_w_no_stream;

    ZSTD_CStream* const stream = ZSTD_createCStream();
    if (!stream)
        goto zstd_w_no_stream;
    if (ZSTD_isError(ZSTD_initCStream(stream, 3)))
        goto zstd_w_error;

    size_t s;
    while ((s = read(in, (void*)zin.src, inbufsz)) > 0)
    {
        zin.size = s;
        zin.pos = 0;
        while (zin.pos < zin.size)
        {
            zout.pos = 0;
            size_t w = ZSTD_compressStream(stream, &zout, &zin);
            if (ZSTD_isError(w))
                goto zstd_w_error;
            if (write(out, zout.dst, zout.pos) != (ssize_t)zout.pos)
                goto zstd_w_error;
        }
    }

    zout.pos = 0;
    ZSTD_endStream(stream, &zout);
    // no way to handle an error here
    write(out, zout.dst, zout.pos);

zstd_w_error:
    ZSTD_freeCStream(stream);
zstd_w_no_stream:
    free((void*)zin.src);
    free(zout.dst);
    close(in);
    close(out);
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
