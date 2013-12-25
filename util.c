#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <err.h>

void *xrealloc (void *p, size_t n) {
	p = realloc (p, n);
	if (!p) err (1, "failed to allocate memory");
	return p;
}

char *xstrndup (const char *s, size_t n) {
	char *t;
	if (!s) return 0;
	t = strndup (s, n);
	if (!t) err (1, "failed to allocate memory");
	return t;
}

char *xstrdup (const char *s) {
	return xstrndup (s, -1);
}

size_t xread (int fd, void *x, size_t n) {
	ssize_t l, m;
	for (m = 0; m < n; m += l) {
		l = read (fd, x, n - m);
		if (l <  0) err (1, "failed to read");
		if (l == 0) break;
	}
	return m;
}
