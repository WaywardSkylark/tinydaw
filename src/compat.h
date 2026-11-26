/* tinydaw: the few things that differ between Linux and Windows. Everything else in the
 * program is plain C plus SDL, libsndfile and libsamplerate, which run on both. */
#ifndef COMPAT_H
#define COMPAT_H

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#define PATH_SEPS "/\\"
#define make_dir(p) _mkdir(p)
#define file_exists(p) (_access((p), 0) == 0)
#define dir_writable(p) (_access((p), 2) == 0)
#else
#include <sys/stat.h>
#include <unistd.h>
#define PATH_SEPS "/"
#define make_dir(p) mkdir((p), 0755)
#define file_exists(p) (access((p), F_OK) == 0)
#define dir_writable(p) (access((p), W_OK) == 0)
#endif

/* The part of a path after the last separator. */
static inline const char *path_base(const char *path)
{
    const char *best = path;
    for (const char *s = path; *s; s++)
        if (strchr(PATH_SEPS, *s)) best = s + 1;
    return best;
}

/* An absolute version of path (malloc'd), or NULL. */
static inline char *full_path(const char *path)
{
#ifdef _WIN32
    return _fullpath(NULL, path, 0);
#else
    return realpath(path, NULL);
#endif
}

/* rename that replaces an existing file, as it does on Linux (Windows' rename refuses) */
#ifdef _WIN32
#include <windows.h>
static inline int replace_file(const char *from, const char *to)
{
    return MoveFileExA(from, to, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) ? 0 : -1;
}
#else
static inline int replace_file(const char *from, const char *to) { return rename(from, to); }
#endif

/* mkdir -p */
static inline void make_dirs(const char *path)
{
    char buf[1024];
    snprintf(buf, sizeof buf, "%s", path);
    for (char *s = buf + 1; *s; s++) {
        if (!strchr(PATH_SEPS, *s) || s[-1] == ':') continue;   /* not after a drive letter */
        char c = *s;
        *s = 0;
        make_dir(buf);
        *s = c;
    }
    make_dir(buf);
}

static inline const char *home_dir(void)
{
    const char *h = getenv("HOME");
#ifdef _WIN32
    if (!h || !h[0]) h = getenv("USERPROFILE");
#endif
    return h && h[0] ? h : ".";
}

/* Where things live: "music" (projects and exports), "config" (pad bindings) and "data"
 * (autosave). Linux follows the XDG conventions, Windows uses %APPDATA%/%LOCALAPPDATA%. */
static inline void user_dir(const char *kind, char *buf, size_t n)
{
    const char *home = home_dir();
    if (!strcmp(kind, "music")) {
        snprintf(buf, n, "%s/Music", home);
        return;
    }
    int config = !strcmp(kind, "config");
#ifdef _WIN32
    const char *base = getenv(config ? "APPDATA" : "LOCALAPPDATA");
    if (base && base[0]) snprintf(buf, n, "%s/tinydaw", base);
    else snprintf(buf, n, "%s/tinydaw", home);
#else
    const char *xdg = getenv(config ? "XDG_CONFIG_HOME" : "XDG_DATA_HOME");
    if (xdg && xdg[0]) snprintf(buf, n, "%s/tinydaw", xdg);
    else snprintf(buf, n, "%s/%s/tinydaw", home, config ? ".config" : ".local/share");
#endif
}

#endif
