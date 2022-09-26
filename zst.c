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

#define die(...) do {fprintf(stderr, __VA_ARGS__); exit(1);} while(0)
#define PN "zst"

static bool cat;
static bool force;
static bool keep;
static bool quiet;
static bool recurse;
static int level;
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
    printf("%s｢%s｣\n", path, name);
    int out = -1;
    char *name2 = 0;
    compress_info *fcomp = comp;
    if (fd <= 0 || cat)
        out = 1;
    else
    {
        if (op)
        {
            fcomp = comp_from_ext(name, decompressors);
            if (!fcomp)
            {
                fprintf(stderr, PN ": %s: unknown suffix -- ignored\n", name);
                if (!err)
                    err = 2;
                goto closure;
            }
            name2 = strndup(name, strlen(name) - strlen(fcomp->ext));
        }
        else
            asprintf(&name2, "%s%s", name, comp->ext);
        out = openat(dir, ".", O_TMPFILE|O_WRONLY, 0666);
        if (out == -1)
        {
            // TODO: fallback
            fprintf(stderr, PN ": open: %s: %m\n", name2);
            goto closure;
        }
    }

    if (fcomp->comp(fd, out, 0))
        err = 1;
    else if (out > 2)
    {
        if (flink(dir, out, name2))
        {
            fprintf(stderr, PN ": can't link %s: %m\n", name2);
            err = 1;
            goto closure;
        }
        if (!keep && unlinkat(dir, name, 0))
        {
            fprintf(stderr, PN ": can't remove %s: %m\n", name);
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

#define fail(txt, ...) (fprintf(stderr, PN ": " txt, __VA_ARGS__), err=1, close(dirfd), (void)0)

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
    int opt;
    while ((opt = getopt(argc, argv, "cdfklnqrth123456789")) != -1)
        switch (opt)
        {
        case 'c':
            cat = 1;
            break;
        case 'd':
            op = 'd';
            break;
        case 'f':
            force = 1;
            break;
        case 'k':
            keep = 1;
            break;
        case 'l':
            die(PN ": -l: unsupported\n");
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
        case '1' ... '9':
            level = opt - '0';
            break;
        default:
            // error message already given
            exit(1);
        }

    comp = comp_from_ext("@.zst", op? decompressors : compressors);
    if (!comp)
        abort();

    if (optind >= argc)
        do_file(0, 0, 0, 0);
    else
        for (; optind < argc; optind++)
            do_dir(AT_FDCWD, argv[optind], "");

    return err;
}
