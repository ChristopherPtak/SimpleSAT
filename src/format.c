
#include "format.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "constants.h"
#include "error.h"
#include "solver.h"

Error read_problem(Solver *solver, FILE *stream)
{
    Error err;
    int pos;
    int i;

    char c;
    char buffer[256];

    int n_vars;
    int n_clauses;

    /*
     * 1. Read comment lines
     */

    do {
        // Read lines until one of them is not a comment
        if (fgets(buffer, 255, stream) == NULL) {
            fprintf(stderr, PROGRAM_NAME ": Expected problem line\n");
            err = ERROR_INVALID_FORMAT;
            goto cleanup;
        }
    } while (buffer[0] == 'c');

    /*
     * 2. Parse problem line
     */

    // Make sure the next line is the problem line
    if (buffer[0] != 'p') {
        fprintf(stderr, PROGRAM_NAME ": Expected problem line\n");
        err = ERROR_INVALID_FORMAT;
        goto cleanup;
    }

    // Parse the beginning of the problem line
    if (sscanf(buffer, "p cnf %d %d%n", &n_vars, &n_clauses, &pos) != 2) {
        fprintf(stderr, PROGRAM_NAME ": Invalid problem line\n");
        err = ERROR_INVALID_FORMAT;
        goto cleanup;
    }

    // Make sure the rest of the line is empty
    while (buffer[pos] != '\0') {
        if (! isspace(buffer[pos++])) {
            fprintf(stderr, PROGRAM_NAME ": Invalid problem line\n");
            err = ERROR_INVALID_FORMAT;
            goto cleanup;
        }
    }

    // Make sure the values read are valid
    if (n_vars <= 0) {
        fprintf(stderr, PROGRAM_NAME ": Invalid number of variables\n");
        err = ERROR_INVALID_FORMAT;
        goto cleanup;
    } else if (n_clauses <= 0) {
        fprintf(stderr, PROGRAM_NAME ": Invalid number of clauses\n");
        err = ERROR_INVALID_FORMAT;
        goto cleanup;
    }

    // Finally, initialize the Solver
    create_solver(solver, n_vars, n_clauses);

    /*
     * 3. Read the clauses
     */

    for (i = 0; i < n_clauses; ++i) {

        ClauseState *cstate = &solver->clauses[i];

        for (;;) {

            int repr;

            // Read an integer
            if (fscanf(stream, " %d", &repr) != 1) {
                fprintf(stderr, PROGRAM_NAME ": Expected more clauses\n");
                err = ERROR_INVALID_FORMAT;
                goto cleanup_solver;
            }

            if (repr == 0) {
                break;
            }

            add_literal_to_clause(solver, cstate, lit_from_int(repr));
        }
    }

    // Make sure this is the end of the input
    for (;;) {
        c = fgetc(stream);
        if (c == EOF) {
            break;
        } else if (! isspace(c)) {
            fprintf(stderr, PROGRAM_NAME ": Expected end of input\n");
            err = ERROR_INVALID_FORMAT;
        }
    }

    return ERROR_OK;

cleanup_solver:
    delete_solver(solver);

cleanup:
    return err;
}

void write_solution(const Solver *solver, FILE *stream)
{
    Literal lit;
    unsigned int column;
    double start_time;
    double stop_time;
    double elapsed_time;

    /*
     * 1. Write performance info
     */

    start_time = (double) solver->start_time;
    stop_time = (double) solver->stop_time;
    elapsed_time = (stop_time - start_time) / CLOCKS_PER_SEC;

    fprintf(stream, "c Generated by " PROGRAM_NAME_FANCY);
    fprintf(stream, " " PROGRAM_VERSION "\n");
    fprintf(stream, "c\n");
    fprintf(stream, "c Performance statistics\n");
    fprintf(stream, "c ----------------------\n");
    fprintf(stream, "c Elapsed time:       %f (s)\n", elapsed_time);
    fprintf(stream, "c Attempted branches: %d\n", solver->t_branches);
    fprintf(stream, "c Unit propagations:  %d\n", solver->t_unit_props);
    fprintf(stream, "c\n");

    /*
     * 2. Write solution line
     */

    switch (solver->solution) {

    case SOLUTION_SATISFIABLE:
        fprintf(stream, "s SATISFIABLE\n");
        break;

    case SOLUTION_UNSATISFIABLE:
        fprintf(stream, "s UNSATISFIABLE\n");
        break;

    case SOLUTION_UNKNOWN:
        fprintf(stream, "s UNKNOWN\n");
        break;
    }

    /*
     * 3. Write variable assignments
     */

    if (solver->solution == SOLUTION_SATISFIABLE) {

        column = 2;
        fprintf(stream, "v");

        for (lit = 0; lit < (solver->n_vars << 1); ++lit) {

            char buffer[80];
            size_t numlen;

            if (solver->lits[lit].fixed &&
                solver->lits[lit].assigned) {

                sprintf(buffer, " %d", int_from_lit(lit));
                numlen = strlen(buffer);
                if (column + numlen > 79) {
                    fprintf(stream, "\nv");
                    column = 1;
                }

                fprintf(stream, "%s", buffer);
                column += numlen;
            }
        }

        if (column + 2 > 79) {
            fprintf(stream, "\nv 0\n");
        } else {
            fprintf(stream, " 0\n");
        }
    }
}

