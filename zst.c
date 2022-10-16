#define _GNU_SOURCE
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "compress.h"
#include "zst.h"

#define die(...) do {fprintf(stderr, __VA_ARGS__); exit(1);} while(0)

const char *exe;

static bool cat;
static bool force;
static bool keep;
static bool quiet;
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
    if (ret && errno==ENOENT)
        ret = linkat(fd, "", dir, newname, AT_EMPTY_PATH);
    return ret;
}

static void do_file(int dir, const char *name, const char *path, int fd)
{
    fprintf(stderr, "%s｢%s｣\n", path, name);
    int out = -1;
    char *name2 = 0;
    compress_info *fcomp = comp;

    if (op && fd>0 && !(fcomp = comp_by_ext(name, decompressors)))
    {
        fprintf(stderr, "%s: %s: unknown suffix -- ignored\n", exe, name);
        if (!err)
            err = 2;
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
        out = openat(dir, ".", O_TMPFILE|O_WRONLY, 0666);
        if (out == -1)
        {
            // TODO: fallback
            fprintf(stderr, "%s: open: %s: %m\n", exe, name2);
            goto closure;
        }
    }

    if (fcomp->comp(fd, out, path, name))
        err = 1;
    else if (out > 2)
    {
        if (flink(dir, out, name2))
        {
            fprintf(stderr, "%s: can't link %s: %m\n", exe, name2);
            err = 1;
            goto closure;
        }
        if (!keep && unlinkat(dir, name, 0))
        {
            fprintf(stderr, "%s: can't remove %s: %m\n", exe, name);
            err = 1;
        }
    }

closure:
    if (fd > 2)
        close(fd);
    if (out > 2)
        close(out);
    if (name2)
        free(name2);
}

#define fail(txt, ...) (fprintf(stderr, "%s: " txt, exe, __VA_ARGS__), err=1, close(dirfd), (void)0)

// may be actually a file
static void do_dir(int dir, const char *name, const char *path)
{
    int dirfd = openat(dir, name, O_RDONLY|O_NONBLOCK|O_CLOEXEC);
    if (dirfd == -1)
        return fail("open(%s%s): %m\n", path, name);

    struct stat sb;
    if (fstat(dirfd, &sb))
        return fail("stat(%s%s): %m\n", path, name);

    if (S_ISREG(sb.st_mode))
        return do_file(dir, name, path, dirfd);
    if (!recurse)
        return fail("%s%s is not a regular file - ignored\n", path, name);
    if (!S_ISDIR(sb.st_mode))
        return fail("%s%s is not a directory or a regular file - ignored\n", path, name);

    char *newpath = alloca(strlen(path) + strlen(name) + 2);
    if (!newpath)
        return fail("out of stack in %s\n", path);
    sprintf(newpath, "%s%s/", path, name);

    DIR *d = fdopendir(dirfd);
    if (!d)
        return fail("fdopendir(%s%s): %m\n", path, name);

    struct dirent *de;
    while ((de = readdir(d)))
    {
        if (de->d_name[0]=='.' && (!de->d_name[1] || de->d_name[1]=='.' && !de->d_name[2]))
            continue;

        if (de->d_type!=DT_DIR && de->d_type!=DT_REG && de->d_type!=DT_UNKNOWN)
            continue;

        do_dir(dirfd, de->d_name, newpath);
    }

    closedir(d);
}

int main(int argc, char **argv)
{
    exe = strrchr(argv[0], '/');
    exe = exe? exe+1 : argv[0];

    const char *prog = "zstd";

    int opt;
    while ((opt = getopt(argc, argv, "cdzfklnqrthF:123456789")) != -1)
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
        case 'r':
            recurse = 1;
            break;
        case 't':
            op = 't';
            break;
        case 'F':
            prog = optarg;
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
        do_file(-1, "stdin", "", 0);
    else
        for (; optind < argc; optind++)
            do_dir(AT_FDCWD, argv[optind], "");

    return err;
}
