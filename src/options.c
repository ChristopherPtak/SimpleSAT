
#include "options.h"

#include <stdio.h>
#include <string.h>

#include "constants.h"
#include "error.h"

Error parse_options(Options *opts, int argc, char **argv)
{
    int i;
    const char *arg;

    opts->infile = NULL;
    opts->outfile = NULL;
    opts->action = ACTION_SOLVE_PROBLEM;

    for (i = 1; i < argc; ++i) {
        arg = argv[i];
        if (strncmp(arg, "-", 1) == 0) {
            // Arguments that start with "-"
            // are taken as config options
            if (strcmp(arg, "--help") == 0) {
                opts->action = ACTION_SHOW_HELP;
            } else if (strcmp(arg, "--version") == 0) {
                opts->action = ACTION_SHOW_VERSION;
            } else if (strcmp(arg, "-o") == 0) {
                if (++i != argc) {
                    opts->outfile = argv[i];
                } else {
                    fprintf(stderr,
                            PROGRAM_NAME ": %s: Expected argument\n",
                            arg);
                    return ERROR_INVALID_USAGE;
                }
            } else {
                fprintf(stderr, PROGRAM_NAME ": %s: Invalid argument\n", arg);
                return ERROR_INVALID_USAGE;
            }
        } else {
            // Arguments that do not start with "-"
            // are taken as input filenames
            if (opts->infile == NULL) {
                opts->infile = arg;
            } else {
                fprintf(stderr, PROGRAM_NAME ": %s: Extra argument\n", arg);
                return ERROR_INVALID_USAGE;
            }
        }
    }

    return ERROR_OK;
}

void show_help(void)
{
    const char *help_text =
        "Usage: " PROGRAM_NAME " [options] <file>\n"
        "Options:\n"
        "  --help     Show this help text\n"
        "  --version  Show the program version\n"
        "  -o <file>  Set the output file\n";

    fputs(help_text, stdout);
}

void show_version(void)
{
    fputs(PROGRAM_NAME_FANCY " " PROGRAM_VERSION "\n", stdout);
}

