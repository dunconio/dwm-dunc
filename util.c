/* See LICENSE file for copyright and license details. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "patches.h"
#if PATCH_IPC
#include <errno.h>
#include <sys/stat.h>
#endif // PATCH_IPC

#include <time.h>
#include "util.h"

char *
expandenv(const char *string)
{
	size_t len = strlen(string), bufsize = 0;
	char *buffer = (char *)malloc((len + 1) * sizeof(char));
	int count = 0, index = 0;

	// working copy of source string;
	strncpy(buffer, string, len);

	// count the number of occurrences of ${...}
	// and track the size of the string for the next buffer;
	char *match, *walk = buffer, *bracket;
	while ((match = strchr(walk, '$'))) {
		if (match[1] == '{' && (bracket = strchr(match, '}'))) {
			bufsize += (bracket - match + 2);
			walk = bracket + 1;
			count++;
		}
		else walk = match + 1;
	}

	if (count == 0) {
		// no variables to expand, return copy of the source string;
		return buffer;
	}

	// buffer to hold all env variable names;
	char *vars = (char *)malloc(bufsize * sizeof(const char **));
	// array to hold links to pairs of variable name with its expanded value;
	char **env = (char **)malloc(2 * count * sizeof(const char **));

	walk = buffer;
	char *varptr = vars;
	while ((match = strchr(walk, '$'))) {
		if (match[1] == '{' && (bracket = strchr(match, '}'))) {
			strncpy(varptr, match, bracket - match + 1);
			bracket[0] = '\0';
			env[index++] = varptr;
			env[index++] = getenv(match + 2);
			bracket[0] = '}';
			varptr += bracket - match + 2;
			walk = bracket + 1;
		}
		else walk = match + 1;
	}

	// iterate through the array of variable name/value pairs
	// performing search & replace on the buffered source string;
	char *output;
	for (index = 0; index < count; index += 2) {
		output = str_replace(buffer, env[index], env[index + 1]);
		if (output) {
			free(buffer);
			buffer = output;
		}
	}

	free(vars);
	free(env);
	return output;
}

// You must free the result if result is non-NULL.
char *str_replace(char *orig, char *rep, char *with) {
    char *result; // the return string
    char *ins;    // the next insert point
    char *tmp;    // varies
    size_t len_rep;  // length of rep (the string to remove)
    size_t len_with; // length of with (the string to replace rep with)
    size_t len_front; // distance between rep and end of last rep
    int count;    // number of replacements

    // sanity checks and initialization
    if (!orig || !rep)
        return NULL;
    len_rep = strlen(rep);
    if (len_rep == 0)
        return NULL; // empty rep causes infinite loop during count
    if (!with)
        with = "";
    len_with = strlen(with);

    // count the number of replacements needed
    ins = orig;
    for (count = 0; (tmp = strstr(ins, rep)); ++count) {
        ins = tmp + len_rep;
    }

    tmp = result = malloc(strlen(orig) + (len_with - len_rep) * count + 1);

    if (!result)
        return NULL;

    // first time through the loop, all the variable are set correctly
    // from here on,
    //    tmp points to the end of the result string
    //    ins points to the next occurrence of rep in orig
    //    orig points to the remainder of orig after "end of rep"
    while (count--) {
        ins = strstr(orig, rep);
        len_front = ins - orig;
        tmp = strncpy(tmp, orig, len_front) + len_front;
        tmp = strcpy(tmp, with) + len_with;
        orig += len_front + len_rep; // move to next "end of rep"
    }
    strcpy(tmp, orig);
    return result;
}

void
logdatetime(FILE *fd)
{
	static time_t last = 0;
	time_t t = time(NULL);
	struct tm tm = *localtime(&t);
	int changed = 1;
	if (last) {
		struct tm l = *localtime(&last);
		if (l.tm_sec == tm.tm_sec && l.tm_min == tm.tm_min && l.tm_hour == tm.tm_hour && l.tm_mday == tm.tm_mday && l.tm_mon == tm.tm_mon && l.tm_year == tm.tm_year)
			changed = 0;
	}
	if (changed)
		fprintf(fd, "%d-%02d-%02d %02d:%02d:%02d:\n	", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
	else
		fprintf(fd, "	");
	last = t;
}

void
die(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);

	if (fmt[0] && fmt[strlen(fmt)-1] == ':') {
		fputc(' ', stderr);
		perror(NULL);
	} else {
		fputc('\n', stderr);
	}

	exit(1);
}

void *
ecalloc(size_t nmemb, size_t size)
{
	void *p;

	if (!(p = calloc(nmemb, size)))
		die("calloc:");
	return p;
}

#if PATCH_IPC

int
normalizepath(const char *path, char **normal)
{
	size_t len = strlen(path);
	*normal = (char *)malloc((len + 1) * sizeof(char));
	const char *walk = path;
	const char *match;
	size_t newlen = 0;

	while ((match = strchr(walk, '/'))) {
		// Copy everything between match and walk
		strncpy(*normal + newlen, walk, match - walk);
		newlen += match - walk;
		walk += match - walk;

		// Skip all repeating slashes
		while (*walk == '/')
			walk++;

		// If not last character in path
		if (walk != path + len)
			(*normal)[newlen++] = '/';
	}

	(*normal)[newlen++] = '\0';

	newlen += strlen(walk);
	*normal = (char *)realloc(*normal, newlen * sizeof(char));

	// Copy remaining path
	strcat(*normal, walk);

	return 0;
}

int
parentdir(const char *path, char **parent)
{
	char *normal;
	char *walk;

	normalizepath(path, &normal);

	// Pointer to last '/'
	if (!(walk = strrchr(normal, '/'))) {
		free(normal);
		return -1;
	}

	// Get path up to last '/'
	size_t len = walk - normal;
	*parent = (char *)malloc((len + 1) * sizeof(char));

	// Copy path up to last '/'
	strncpy(*parent, normal, len);
	// Add null char
	(*parent)[len] = '\0';

	free(normal);

	return 0;
}

int
mkdirp(const char *path)
{
	char *normal;
	char *walk;
	size_t normallen;

	normalizepath(path, &normal);
	normallen = strlen(normal);
	walk = normal;

	while (walk < normal + normallen + 1) {
		// Get length from walk to next /
		size_t n = strcspn(walk, "/");

		// Skip path /
		if (n == 0) {
			walk++;
			continue;
		}

		// Length of current path segment
		size_t curpathlen = walk - normal + n;
		char curpath[curpathlen + 1];
		struct stat s;

		// Copy path segment to stat
		strncpy(curpath, normal, curpathlen);
		strcpy(curpath + curpathlen, "");
		int res = stat(curpath, &s);

		if (res < 0) {
			if (errno == ENOENT) {
				DEBUG("Making directory %s\n", curpath);
				if (mkdir(curpath, 0700) < 0) {
					fprintf(stderr, "Failed to make directory %s\n", curpath);
					perror("");
					free(normal);
					return -1;
				}
			} else {
				fprintf(stderr, "Error statting directory %s\n", curpath);
				perror("");
				free(normal);
				return -1;
			}
		}

		// Continue to next path segment
		walk += n;
	}

	free(normal);

	return 0;
}

int
nullterminate(char **str, size_t *len)
{
	if ((*str)[*len - 1] == '\0')
		return 0;

	(*len)++;
	*str = (char*)realloc(*str, *len * sizeof(char));
	(*str)[*len - 1] = '\0';

	return 0;
}
#endif // PATCH_IPC
