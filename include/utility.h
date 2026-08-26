#ifndef UTILITY_H
#define UTILITY_H

/* Concatenate src with n additional strings. Returns a newly allocated
   string, or NULL on allocation failure. */
char *strconcat(const char *src, int n, ...);

/* Resolve path to an absolute canonical form. Returns a newly allocated
   string, or NULL on failure (errno set by realpath). */
char *resolve_path(const char *path);

/* Copy file contents from src to dst.
   Returns 0 on success, -1 on failure. */
int copy(const char *src, const char *dst);

/* Count lines in a file.
   Returns the line count on success, -1 on failure. */
int count_lines(const char *file);

#endif
