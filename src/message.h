#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#define Debug(fmt,args...)                     \
    do {                                    \
        if (is_DebugEnabled())         \
            printf("%s: %s(): "fmt, "[DEBUG]",__func__, ##args);\
    } while (0)

static inline bool
is_DebugEnabled()
{
    const char *env = getenv("DEBUG");
    if (env && strlen(env)) {
        return true;
    }
    return false;
}