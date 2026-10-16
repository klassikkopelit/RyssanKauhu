/*
 * files.c - case-insensitive access to the original DOS game files.
 * DOS filenames are case-insensitive; the game asks for "graph.dat" while
 * the file on disk is GRAPH.DAT. Windows and (usually) macOS don't care,
 * Linux does.
 */
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "files.h"

#ifdef _WIN32
#  define PATH_SEP '\\'
#else
#  include <dirent.h>
#  define PATH_SEP '/'
#endif

static char game_dir[1024] = "";

/* Sets the directory the game files are read from, adding a trailing path separator. */
void files_set_dir(const char *dir)
{
    size_t n;
    if (!dir || !*dir) { game_dir[0] = 0; return; }
    strncpy(game_dir, dir, sizeof game_dir - 2);
    game_dir[sizeof game_dir - 2] = 0;
    n = strlen(game_dir);
    if (n && game_dir[n - 1] != '/' && game_dir[n - 1] != '\\') { game_dir[n] = PATH_SEP; game_dir[n + 1] = 0; }
}

/* Returns the current game directory (with trailing separator, or empty). */
const char *files_get_dir(void) { return game_dir; }

#ifndef _WIN32
/* Compares two strings case-insensitively; returns 1 if they are equal. */
static int ci_equal(const char *a, const char *b)
{
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
        a++; b++;
    }
    return *a == *b;
}
#endif

/* Resolve name inside dir to the real on-disk spelling. */
static int resolve(const char *dir, const char *name, char *out, size_t outsz)
{
    FILE *f;
    snprintf(out, outsz, "%s%s", dir, name);
    f = fopen(out, "rb");
    if (f) { fclose(f); return 1; }
#ifndef _WIN32
    {
        DIR *d = opendir(*dir ? dir : ".");
        struct dirent *e;
        if (!d) return 0;
        while ((e = readdir(d)) != NULL) {
            if (ci_equal(e->d_name, name)) {
                snprintf(out, outsz, "%s%s", dir, e->d_name);
                closedir(d);
                return 1;
            }
        }
        closedir(d);
    }
#endif
    return 0;
}

/* Returns 1 if the file name exists in dir (matched case-insensitively). */
int game_file_exists(const char *dir, const char *name)
{
    char p[1200];
    char d[1024];
    size_t n;
    strncpy(d, dir ? dir : "", sizeof d - 2);
    d[sizeof d - 2] = 0;
    n = strlen(d);
    if (n && d[n - 1] != '/' && d[n - 1] != '\\') { d[n] = PATH_SEP; d[n + 1] = 0; }
    return resolve(d, name, p, sizeof p);
}

/* Opens a file in the game directory, matching its name case-insensitively. */
FILE *game_fopen(const char *name, const char *mode)
{
    char p[1200];
    if (resolve(game_dir, name, p, sizeof p)) return fopen(p, mode);
    return NULL;
}

/* Reads a whole game file into a malloc'd buffer and stores its size in *len.
   Returns NULL on failure. */
unsigned char *game_load(const char *name, size_t *len)
{
    FILE *f = game_fopen(name, "rb");
    unsigned char *buf;
    long n;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 0) { fclose(f); return NULL; }
    buf = (unsigned char *)malloc((size_t)n + 1);
    if (!buf) { fclose(f); return NULL; }
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) { free(buf); fclose(f); return NULL; }
    fclose(f);
    if (len) *len = (size_t)n;
    return buf;
}
