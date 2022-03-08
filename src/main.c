
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "options.h"
#include "format.h"
#include "solver.h"

static Error solve_problem(const Options *);

int main(int argc, char **argv)
{
    Error err;
    Options opts;

    err = parse_options(&opts, argc, argv);
    if (err) {
        goto cleanup;
    }

    switch (opts.action) {

    case ACTION_SOLVE_PROBLEM:
        err = solve_problem(&opts);
        if (err) goto cleanup;
        break;

    case ACTION_SHOW_HELP:
        show_help();
        break;

    case ACTION_SHOW_VERSION:
        show_version();
        break;
    }

cleanup:

    if (err == ERROR_INVALID_USAGE) {
        fputs("Try --help for usage\n", stderr);
    }

    if (err) {
        return EXIT_FAILURE;
    } else {
        return EXIT_SUCCESS;
    }
}

static Error solve_problem(const Options *opts)
{
    Error err = ERROR_OK;
    Solver solver;

    if (opts->infile != NULL) {

        FILE *stream = fopen(opts->infile, "r");

        if (stream == NULL) {
            fprintf(stderr, "simplesat: %s: %s\n",
                    opts->infile, strerror(errno));
            err = ERROR_FILE_ACCESS;
            goto cleanup;
        }

        err = read_problem(&solver, stream);
        fclose(stream);

    } else {
        err = read_problem(&solver, stdin);
    }

    if (err) {
        goto cleanup;
    }

    solver.start_time = clock();
    solver.solution = search_assignments(&solver);
    solver.stop_time = clock();

    if (opts->outfile != NULL) {

        FILE *stream = fopen(opts->outfile, "w");

        if (stream == NULL) {
            fprintf(stderr, "simplesat: %s: %s\n",
                    opts->outfile, strerror(errno));
            err = ERROR_FILE_ACCESS;
            goto cleanup_solver;
        }

        write_solution(&solver, stream);
        fclose(stream);

    } else {
        write_solution(&solver, stdout);
    }

cleanup_solver:
    delete_solver(&solver);

cleanup:
    return err;
}

