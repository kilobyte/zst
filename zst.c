#define _GNU_SOURCE
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "compress.h"
#include "zst.h"

#ifndef HAVE_STAT64
# define stat64 stat
# define fstat64 fstat
#endif
#ifndef O_LARGEFILE
# define O_LARGEFILE 0
#endif

#define die(...) do {fprintf(stderr, __VA_ARGS__); exit(1);} while(0)
#define ARRAYSZ(x) (sizeof(x) / sizeof((x)[0]))
#define warn(msg, ...) do {if (!quiet) {fprintf(stderr, "%s: " msg, exe, __VA_ARGS__); if (!err) err=2;}} while(0)

const char *exe;

static bool cat;
bool force;
static bool keep;
static bool quiet;
static bool verbose;
static bool recurse;
int level;
static int op;
static int err;

static compress_info *comp;

static int flink(int dir, int fd, const char *newname)
{
    char proclink[26];
    sprintf(proclink, "/proc/self/fd/%d", fd);
    int ret = linkat(AT_FDCWD, proclink, dir, newname, AT_SYMLINK_FOLLOW);
#ifdef AT_EMPTY_PATH
    if (ret && errno==ENOENT)
        ret = linkat(fd, "", dir, newname, AT_EMPTY_PATH);
#endif
    return ret;
}

#define FAIL(msg, ...) do {fprintf(stderr, "%s: " msg, exe, __VA_ARGS__); err=1; goto closure;} while(0)
static void do_file(int dir, const char *name, const char *path, int fd, struct stat64 *restrict st)
{
    int out = -1;
    bool notmp = 0;
    char *name2 = 0;
    compress_info *fcomp = comp;

    if (!op && fd>0 && comp_by_ext(name, compressors) && !force)
    {
        warn("%s: already has a compression suffix -- unchanged\n", name);
        close(fd);
        return;
    }
    if (op && fd>0 && !(fcomp = comp_by_ext(name, decompressors))
        && !(cat && force))
    {
        warn("%s: unknown suffix -- ignored\n", name);
        close(fd);
        return;
    }

    if (op == 't')
        out = -1;
    else if (fd <= 0 || cat)
    {
        out = 1;
        if (!op && !force & isatty(1))
            die("%s: refusing to write compressed data to a terminal, use -f to force.\n", exe);
    }
    else
    {
        if (op)
            name2 = strndup(name, strlen(name) - strlen(fcomp->ext));
        else
            asprintf(&name2, "%s%s", name, comp->ext);
        if (!force)
        {
            if (!faccessat(dir, name2, F_OK, AT_EACCESS))
                FAIL("%s%s already exists.\n", path, name2);
            if (errno != ENOENT)
                FAIL("%s%s: %m\n", path, name2);
        }
#ifdef O_TMPFILE
        out = openat(dir, ".", O_TMPFILE|O_WRONLY|O_CLOEXEC|O_LARGEFILE, 0666);
#endif
        if (out == -1)
        {
            notmp = 1;
            out = openat(dir, name2, O_CREAT|O_TRUNC|O_WRONLY|O_CLOEXEC|O_LARGEFILE|O_NOFOLLOW, 0600);
            if (out == -1)
                FAIL("can't create %s%s: %m\n", path, name2);
        }
    }

    file_info fi;
    fi.path = path;
    fi.name_in = name;
    fi.name_out = name2;
    fi.sz = fi.sd = 0;

    if (op? decomp(cat && force, fd, out, &fi) : fcomp->comp(fd, out, &fi, 0))
    {
        err = 1;
        goto closure;
    }

    if (out > 2)
    {
        if (st)
        {
            struct timespec ts[2];
            ts[0] = st->st_atim;
            ts[1] = st->st_mtim;
            futimens(out, ts);
            // ignore errors

            int perms = st->st_mode & 07777;
            if (fchown(out, st->st_uid, st->st_gid))
                perms &= 0777;
            fchmod(out, perms);
        }

        if (!notmp && flink(dir, out, name2))
        {
            if (errno==EEXIST && force)
            {
                if (unlinkat(dir, name2, 0))
                    FAIL("can't remove %s%s: %m\n", path, name2);

                if (!flink(dir, out, name2))
                    goto flink_ok;
            }

            FAIL("can't link %s%s: %m\n", path, name2);
        }

        notmp = 0;
flink_ok:
        if (!keep && unlinkat(dir, name, 0))
            FAIL("can't remove %s%s: %m\n", path, name);
    }

    if (verbose)
    {
        if (fi.sd)
            fprintf(stderr, "%s%s: %llu %s %llu (%llu%%)\n", path, name, fi.sd,
                op? "←" : "→", fi.sz, (200 * fi.sz / fi.sd + 1) / 2);
                // round to closest percent
        else
            fprintf(stderr, "%s%s: 0 %s %llu (header)\n", path, name,
                op? "←" : "→", fi.sz);
    }

closure:
    if (notmp)
        if (unlinkat(dir, name2, 0))
            fprintf(stderr, "%s: can't remove temporary file %s%s: %m\n", exe, path, name2);
    if (fd > 2)
        close(fd);
    if (out > 2)
        close(out);
    if (name2)
        free(name2);
}

// may be actually a file
static void do_dir(int dir, const char *name, const char *path)
{
    int dirfd = openat(dir, name, O_RDONLY|O_NONBLOCK|O_CLOEXEC|O_LARGEFILE);
    if (dirfd == -1)
        FAIL("can't read %s%s: %m\n", path, name);

    struct stat64 sb;
    if (fstat64(dirfd, &sb))
        FAIL("can't stat %s%s: %m\n", path, name);

    if (S_ISREG(sb.st_mode))
        return do_file(dir, name, path, dirfd, &sb);
    if (!recurse)
    {
        warn("%s%s is not a regular file -- ignored\n", path, name);
        close(dirfd);
        return;
    }
    if (!S_ISDIR(sb.st_mode))
    {
        warn("%s%s is not a directory or a regular file -- ignored\n", path, name);
        close(dirfd);
        return;
    }

    char *newpath = alloca(strlen(path) + strlen(name) + 2);
    if (!newpath)
        FAIL("out of stack in %s\n", path);
    sprintf(newpath, "%s%s/", path, name);

    DIR *d = fdopendir(dirfd);
    if (!d)
        FAIL("can't list %s%s: %m\n", path, name);

    struct dirent *de;
    while ((de = readdir(d)))
    {
        // "." or ".."
        if (de->d_name[0]=='.' && (!de->d_name[1] || de->d_name[1]=='.' && !de->d_name[2]))
            continue;

        if (de->d_type!=DT_DIR && de->d_type!=DT_REG && de->d_type!=DT_UNKNOWN)
        {
            warn("%s%s is not a directory or a regular file -- ignored\n", path, de->d_name);
            continue;
        }

        do_dir(dirfd, de->d_name, newpath);
    }

    closedir(d);
    return;

closure:
    close(dirfd);
}

static const char* guess_prog(void)
{
    const char *progs[][3] =
    {
        {"gzip",  "gunzip",  "gzcat"},
        {"bzip2", "bunzip2", "bzcat"},
        {"xz",    "unxz",    "xzcat"},
        {"zstd",  "unzstd",  "zstdcat"},
        {"zst",   "unzst",   "zstcat"},
        {"bzip3", "bunzip3", "bz3cat"},
        {"lz4",   "unlz4",   "lz4cat"},
        {"lzop",  0,         0},
        {"brotli","unbrotli",0},
        {"lzip",  0,         0},
        {"pack",  0,         0},
        {"compress", "uncompress", 0},
    };

    for (int i=0; i<ARRAYSZ(progs); i++)
        for (int j=0; j<3; j++)
            if (progs[i][j] && !strcmp(exe, progs[i][j]))
            {
                op = j? 'd' : 0;
                cat = j == 2;
                return progs[i][0];
            }

    return "zstd";
}

int main(int argc, char **argv)
{
    exe = strrchr(argv[0], '/');
    exe = exe? exe+1 : argv[0];

    if (match_suffix(exe, "less"))
    {
        putenv("LESSOPEN=||-zst -cdfq -- %s");
        argv[0] = "/usr/bin/less";
        execve(*argv, argv, environ);
        die("%s: couldn't exec %s: %m\n", exe, *argv);
    }

    const char *prog = guess_prog();

    int opt;
    static const struct option opts[] =
    {
        {"fast",		0, 0, '1'},
        {"best",		0, 0, '9'},
    };
    while ((opt = getopt_long(argc, argv, "cdzfklnqvrthF:0123456789", opts, 0)) != -1)
        switch (opt)
        {
        case 'c':
            cat = 1;
            break;
        case 'd':
            op = 'd';
            break;
        case 'z':
            op = 0;
            break;
        case 'f':
            force = 1;
            break;
        case 'k':
            keep = 1;
            break;
        case 'l':
            die("%s: -l: unsupported\n", exe);
        case 'n':
            // silently ignored
            break;
        case 'q':
            quiet = 1;
            break;
        case 'v':
            verbose = 1;
            break;
        case 'r':
            recurse = 1;
            break;
        case 't':
            op = 't';
            break;
        case 'F':
            prog = optarg;
            break;
        case '0':
            level = 1;
            break;
        case '1' ... '9':
            level = opt - '0';
            break;
        default:
            // error message already given
            exit(1);
        }

    comp = comp_by_name(prog, op? decompressors : compressors);
    if (!comp)
        die("%s: no such format known '%s'\n", exe, prog);

    if (optind >= argc)
        do_file(-1, "stdin", "", 0, 0);
    else
        for (; optind < argc; optind++)
            do_dir(AT_FDCWD, argv[optind], "");

    return err;
}
