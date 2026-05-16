
#ifndef __UTILS_H
#define __UTILS_H

#include "types.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

inline static void ToWideString(PWCHAR wszDest, char *sz)
{
    for (SIZE i = 0; sz[i] != 0; i++)
        wszDest[i] = sz[i];
}

inline static char *strdup(const char *c)
{
    char *szDup = malloc(strlen(c) + 1);

    if (szDup != NULL)
       strcpy(szDup, c);

    return szDup;
}

inline static QWORD strlenW(PWCHAR wsz)
{
    QWORD i;
    for (i = 0; wsz[i] != 0; i++);
    return i;
}

inline static void Error(const char *szError, ...)
{
    printf("Error: ");
    __builtin_va_list args;
    __builtin_va_start(args, szError);
    vprintf(szError, args);
    __builtin_va_end(args);
    printf("\n");
    exit(1);
}

inline static LONGLONG TimeStamp()
{
    return (LONGLONG) time(NULL) - 62261827200LL;
}

#endif /// __UTILS_H
