
#ifndef SIMPLESAT_OPTIONS_H
#define SIMPLESAT_OPTIONS_H

#include "error.h"

typedef struct
{
    const char *infile;
    const char *outfile;

    enum
    {
        ACTION_SOLVE_PROBLEM,
        ACTION_SHOW_HELP,
        ACTION_SHOW_VERSION
    }
    action;
}
Options;

Error parse_options(Options *, int, char **);

void show_help(void);
void show_version(void);

#endif

