/* See LICENSE file for copyright and license details. */

#define MAX(A, B)               ((A) > (B) ? (A) : (B))
#define MIN(A, B)               ((A) < (B) ? (A) : (B))
#define BETWEEN(X, A, B)        ((A) <= (X) && (X) <= (B))

#if DEBUGGING
#define DEBUG(...) { logdatetime(stderr); fprintf(stderr, "debug: "); fprintf(stderr, __VA_ARGS__); }
#else
#define DEBUG(...)
#endif

void logdatetime(FILE *fd);

void die(const char *fmt, ...);
void *ecalloc(size_t nmemb, size_t size);

#if PATCH_IPC
char *expandenv(const char *string);
int normalizepath(const char *path, char **normal);
int mkdirp(const char *path);
int parentdir(const char *path, char **parent);
int nullterminate(char **str, size_t *len);
#endif // PATCH_IPC
