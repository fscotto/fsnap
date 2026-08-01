#ifndef UTILITY_H
#define UTILITY_H

#include <unistd.h>
#include <limits.h>

#ifdef PATH_MAX
static long pathmax = PATH_MAX;
#else
static long pathmax = 1024;
#endif

char *strconcat(const char *src, int n, ...);

char *resolve_path(const char *path);

#endif
