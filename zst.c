#include <dirent.h>
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

static void do_file(int dir, const char *name, const char *path, int fd)
{
    printf("%s｢%s｣\n", path, name);
    if (fd)
        close(fd);
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

    if (optind >= argc)
        do_file(0, 0, 0, 0);
    else
        for (; optind < argc; optind++)
            do_dir(AT_FDCWD, argv[optind], "");

    return err;
}
