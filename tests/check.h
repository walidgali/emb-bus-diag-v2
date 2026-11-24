#ifndef CHECK_H
#define CHECK_H
#include <stdio.h>
#include <math.h>

/* assert() is compiled out with -DNDEBUG; these checks never are. */
static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond) do { g_checks++; if (!(cond)) { g_failures++; \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_NEAR(a, b, eps) CHECK(fabs((double)(a) - (double)(b)) <= (eps))

#define CHECK_DONE(name) do { \
    printf("%s: %d checks, %d failures\n", name, g_checks, g_failures); \
    return g_failures ? 1 : 0; } while (0)
#endif
