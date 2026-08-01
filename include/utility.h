#ifndef UTILITY_H
#define UTILITY_H

char *strconcat(const char *src, int n, ...);

char *resolve_path(const char *path);

int copy(const char *src, const char *dst);

#endif
