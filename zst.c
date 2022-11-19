#define _GNU_SOURCE
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "compress.h"
#include "zst.h"

#define die(...) do {fprintf(stderr, __VA_ARGS__); exit(1);} while(0)
#define ARRAYSZ(x) (sizeof(x) / sizeof((x)[0]))

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
    if (ret && errno==ENOENT)
        ret = linkat(fd, "", dir, newname, AT_EMPTY_PATH);
    return ret;
}

#define FAIL(msg, ...) do {fprintf(stderr, "%s: " msg, exe, __VA_ARGS__); err=1; goto closure;} while(0)
static void do_file(int dir, const char *name, const char *path, int fd, struct stat64 *restrict st)
{
    int out = -1;
    bool notmp = 0;
    char *name2 = 0;
    compress_info *fcomp = comp;

    if (op && fd>0 && !(fcomp = comp_by_ext(name, decompressors)))
    {
        if (quiet)
            return; // no exit code, either
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
        if (!force)
        {
            if (!faccessat(dir, name2, F_OK, AT_EACCESS))
                FAIL("%s%s already exists.\n", path, name2);
            if (errno != ENOENT)
                FAIL("%s%s: %m\n", path, name2);
        }
        out = openat(dir, ".", O_TMPFILE|O_WRONLY|O_CLOEXEC|O_LARGEFILE, 0666);
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

    if ((op && cat)? decomp(fcomp, fd, out, &fi) : fcomp->comp(fd, out, &fi, 0))
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

#define fail(txt, ...) (fprintf(stderr, "%s: " txt, exe, __VA_ARGS__), err=1, close(dirfd), (void)0)

// may be actually a file
static void do_dir(int dir, const char *name, const char *path)
{
    int dirfd = openat(dir, name, O_RDONLY|O_NONBLOCK|O_CLOEXEC|O_LARGEFILE);
    if (dirfd == -1)
        return fail("can't read %s%s: %m\n", path, name);

    struct stat64 sb;
    if (fstat64(dirfd, &sb))
        return fail("can't stat %s%s: %m\n", path, name);

    if (S_ISREG(sb.st_mode))
        return do_file(dir, name, path, dirfd, &sb);
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
        return fail("can't list %s%s: %m\n", path, name);

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

static const char* guess_prog(void)
{
    const char *progs[][3] =
    {
        {"gzip",  "gunzip",  "gzcat"},
        {"bzip2", "bunzip2", "bzcat"},
        {"xz",    "unxz",    "xzcat"},
        {"zstd",  "unzstd",  "zstdcat"},
        {"bzip3", "bunzip3", "bz3cat"},
        {"lz4",   "unlz4",   "lz4cat"},
        {"lzop",  "",        ""},
        {"compress", "uncompress", ""},
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

    const char *prog = guess_prog();

    int opt;
    while ((opt = getopt(argc, argv, "cdzfklnqvrthF:123456789")) != -1)
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
