
#include <assert.h>
#include <ctype.h>
#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/*************
 * Constants *
 *************/

#define SIMPLESAT_VERSION "0.0.1"

/*********************
 * Utility functions *
 *********************/

static void *xmalloc(size_t);
static void *xrealloc(void *, size_t);

#define CREATE_ARRAY(ARRAY, ISIZE) \
    ARRAY = xmalloc((ISIZE) * sizeof(*ARRAY))
#define RESIZE_ARRAY(ARRAY, NSIZE) \
    ARRAY = xrealloc(ARRAY, (NSIZE) * sizeof(*ARRAY))
#define DELETE_ARRAY(ARRAY) free(ARRAY);

/***************************
 * CLI function prototypes *
 ***************************/

typedef enum
{
    ERROR_OK = 0,
    ERROR_INVALID_USAGE,
    ERROR_INVALID_FORMAT,
    ERROR_FILE_ACCESS
}
Error;

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

static Error parse_options(Options *, int, char **);

static Error solve_problem(const Options *);
static void show_help(void);
static void show_version(void);

/***********************************
 * Solver structures and functions *
 ***********************************/

typedef unsigned int Literal;

static Literal negate(Literal);
static Literal lit_from_int(int);
static int int_from_lit(Literal);

typedef struct
{
    // Array of literals in this clause
    unsigned int n_lits;
    unsigned int c_lits;
    Literal *lits;

    // Assignment counts allow detection of SAT/UNSAT
    // faster than examining the literals every time
    unsigned int n_assigned_true;
    unsigned int n_assigned_false;
    unsigned int n_free_lits;
}
ClauseState;

static void create_clause_state(ClauseState *);
static void delete_clause_state(ClauseState *);

static void add_literal(ClauseState *, Literal);

typedef struct
{
    // Record assigned value
    bool fixed;
    bool assigned;

    // Overall favorability of this literal as a branch choice
    unsigned int score;

    // Array of clauses containing this literal
    unsigned int n_cont_clauses;
    unsigned int c_cont_clauses;
    ClauseState **cont_clauses;
}
LitState;

static void create_lit_state(LitState *);
static void delete_lit_state(LitState *);

// Adds the clause to the array in the LitState
// Does NOT modify the ClauseState
static void add_cont_clause(LitState *, ClauseState *);

typedef enum
{
    SOLUTION_UNKNOWN,
    SOLUTION_SATISFIABLE,
    SOLUTION_UNSATISFIABLE
}
Solution;

typedef struct
{
    // Problem state
    unsigned int n_vars;
    unsigned int n_clauses;
    LitState *lits;
    ClauseState *clauses;

    // Used during solver operation
    unsigned int n_sat_clauses;
    unsigned int n_unsat_clauses;
    unsigned int n_units;
    unsigned int n_assigned;
    Literal *unit_stack;
    Literal *assigned;

    // Solution state
    Solution solution;

    // Performance statistics
    clock_t start_time;
    clock_t stop_time;
    unsigned int t_branches;
    unsigned int t_unit_props;
}
Solver;

static void create_solver(Solver *, unsigned int, unsigned int);
static void delete_solver(Solver *);

static void add_literal_to_clause(Solver *, ClauseState *, Literal);

static Literal choose_branch(Solver *);
static void update_scores(Solver *);

static void add_true_assignment(Solver *, ClauseState *);
static void add_false_assignment(Solver *, ClauseState *);
static void undo_true_assignment(Solver *, ClauseState *);
static void undo_false_assignment(Solver *, ClauseState *);

static Literal get_unit(const Solver *, const ClauseState *);

static void make_assignment(Solver *, Literal);
static void undo_assignment(Solver *, Literal);

static Solution search_assignments(Solver *);
static Solution try_assignment(Solver *, Literal);

static bool all_satisfied(const Solver *);
static bool any_contradiction(const Solver *);

/***************************
 * DIMACS format functions *
 ***************************/

static Error read_problem(Solver *, FILE *);
static void write_solution(const Solver *, FILE *);

/********************
 * Main entry point *
 ********************/

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

/****************************
 * Function implementations *
 ****************************/

static void *xmalloc(size_t size)
{
    void *ptr = malloc(size);

    if (ptr == NULL) {
        fprintf(stderr, "Memory allocation failure\n");
        abort();
    }

    return ptr;
}

static void *xrealloc(void *ptr, size_t new_size)
{
    void *new_ptr = realloc(ptr, new_size);

    if (new_ptr == NULL) {
        fprintf(stderr, "Memory allocation failure\n");
        abort();
    }

    return new_ptr;
}

static Error parse_options(Options *opts, int argc, char **argv)
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
                    fprintf(stderr, "Expected argument after %s\n", arg);
                    return ERROR_INVALID_USAGE;
                }
            } else {
                fprintf(stderr, "Invalid argument %s\n", arg);
                return ERROR_INVALID_USAGE;
            }
        } else {
            // Arguments that do not start with "-"
            // are taken as input filenames
            if (opts->infile == NULL) {
                opts->infile = arg;
            } else {
                fprintf(stderr, "Extra argument %s\n", arg);
                return ERROR_INVALID_USAGE;
            }
        }
    }

    return ERROR_OK;
}

static Error solve_problem(const Options *opts)
{
    Error err = ERROR_OK;
    Solver solver;

    if (opts->infile != NULL) {

        FILE *stream = fopen(opts->infile, "r");

        if (stream == NULL) {
            fprintf(stderr, "Unable to open file %s\n", opts->infile);
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
            fprintf(stderr, "Unable to open file %s\n", opts->outfile);
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

static void show_help(void)
{
    const char *help_text =
        "Usage: simplesat [options] <file>\n"
        "Options:\n"
        "  --help     Show this help text\n"
        "  --version  Show the program version\n"
        "  -o <file>  Set the output file\n";

    fputs(help_text, stdout);
}

static void show_version(void)
{
    fputs("SimpleSAT " SIMPLESAT_VERSION "\n", stdout);
}

static Literal negate(Literal lit)
{
    // Flip the least significant bit
    return lit ^ 1;
}

static Literal lit_from_int(int repr)
{
    assert(repr != 0);
    if (repr > 0) {
        return (repr - 1) << 1;
    } else {
        return ((-repr - 1) << 1) | 1;
    }
}

static int int_from_lit(Literal lit)
{
    if (lit & 1) {
        return -((lit >> 1) + 1);
    } else {
        return (lit >> 1) + 1;
    }
}

static void create_clause_state(ClauseState *cstate)
{
    cstate->n_lits = 0;
    cstate->c_lits = 4;
    CREATE_ARRAY(cstate->lits, 4);

    cstate->n_assigned_true = 0;
    cstate->n_assigned_false = 0;
    cstate->n_free_lits = 0;
}

static void delete_clause_state(ClauseState *cstate)
{
    DELETE_ARRAY(cstate->lits);
}

static void add_literal(ClauseState *cstate, Literal lit)
{
    if (cstate->n_lits == cstate->c_lits) {
        cstate->c_lits *= 2;
        RESIZE_ARRAY(cstate->lits, cstate->c_lits);
    }

    cstate->lits[cstate->n_lits++] = lit;
    cstate->n_free_lits += 1;
}

static void create_lit_state(LitState *lstate)
{
    lstate->fixed = false;
    lstate->assigned = false;

    lstate->score = 0;

    lstate->n_cont_clauses = 0;
    lstate->c_cont_clauses = 16;
    CREATE_ARRAY(lstate->cont_clauses, 16);
}

static void delete_lit_state(LitState *lstate)
{
    DELETE_ARRAY(lstate->cont_clauses);
}

static void add_cont_clause(LitState *lstate, ClauseState *cstate)
{
    if (lstate->n_cont_clauses == lstate->c_cont_clauses) {
        lstate->c_cont_clauses *= 2;
        RESIZE_ARRAY(lstate->cont_clauses, lstate->c_cont_clauses);
    }

    lstate->cont_clauses[lstate->n_cont_clauses++] = cstate;
}

static void create_solver(Solver *solver,
                          unsigned int num_vars,
                          unsigned int num_clauses)
{
    unsigned int i;
    unsigned int max_unit_props;

    // There must be at least one variable
    // However there CAN be zero clauses
    assert(num_vars > 0);

    solver->n_vars = num_vars;
    CREATE_ARRAY(solver->lits, num_vars << 1);
    for (i = 0; i < (num_vars << 1); ++i) {
        create_lit_state(&solver->lits[i]);
    }

    solver->n_clauses = num_clauses;
    CREATE_ARRAY(solver->clauses, num_clauses);
    for (i = 0; i < num_clauses; ++i) {
        create_clause_state(&solver->clauses[i]);
    }

    solver->n_sat_clauses = 0;
    solver->n_unsat_clauses = 0;

    solver->n_units = 0;
    max_unit_props = (solver->n_vars << 1) + solver->n_clauses;
    CREATE_ARRAY(solver->unit_stack, max_unit_props);

    solver->n_assigned = 0;
    CREATE_ARRAY(solver->assigned, solver->n_vars << 1);

    solver->solution = SOLUTION_UNKNOWN;
    solver->start_time = 0.0;
    solver->stop_time = 0.0;
    solver->t_branches = 0;
    solver->t_unit_props = 0;
}

static void delete_solver(Solver *solver)
{
    unsigned int i;

    for (i = 0; i < (solver->n_vars << 1); ++i) {
        delete_lit_state(&solver->lits[i]);
    }

    for (i = 0; i < solver->n_clauses; ++i) {
        delete_clause_state(&solver->clauses[i]);
    }

    DELETE_ARRAY(solver->lits);
    DELETE_ARRAY(solver->clauses);
    DELETE_ARRAY(solver->unit_stack);
    DELETE_ARRAY(solver->assigned);
}

static void add_literal_to_clause(Solver *solver,
                                  ClauseState *cstate,
                                  Literal lit)
{
    unsigned int i;

    // Make sure the clause has at most one copy of each literal.
    // This property is required for the correctness of optimizations
    // in later solver functions.
    for (i = 0; i < cstate->n_lits; ++i) {
        if (cstate->lits[i] == lit) {
            return;
        }
    }

    add_literal(cstate, lit);
    add_cont_clause(&solver->lits[lit], cstate);
}

static Literal choose_branch(Solver *solver)
{
    Literal lit;
    Literal best_lit;
    unsigned int score;
    unsigned int best_score;

    assert(solver->n_assigned != solver->n_vars);
    assert(solver->n_sat_clauses != solver->n_clauses);

    update_scores(solver);

    // Should always be overwritten
    best_lit = 0;
    best_score = 0;

    for (lit = 0; lit < (solver->n_vars << 1); lit += 2) {

        unsigned int a;
        unsigned int b;

        // Skip assigned literals
        if (solver->lits[lit].fixed) {
            continue;
        }

        // Calculate the score for this literal pair
        a = solver->lits[lit].score;
        b = solver->lits[negate(lit)].score;
        score = (a + 1) * (b + 1); 

        if (score > best_score) {
            best_score = score;
            // Choose the higher-scoring of the pair
            if (a >= b) {
                best_lit = lit;
            } else {
                best_lit = negate(lit);
            }
        }
    }

    return best_lit;
}

static void update_scores(Solver *solver)
{
    Literal lit;
    unsigned int i;

    // Set all scores to zero
    for (lit = 0; lit < (solver->n_vars << 1); ++lit) {
        solver->lits[lit].score = 0;
    }

    for (lit = 0; lit < (solver->n_vars << 1); ++lit) {

        LitState *lstate = &solver->lits[lit];

        // Skip literals that have been given a value
        if (lstate->fixed) {
            continue;
        }

        for (i = 0; i < lstate->n_cont_clauses; ++i) {

            ClauseState *cstate = lstate->cont_clauses[i];

            // Skip clauses that have been satisfied
            if (cstate->n_assigned_true != 0) {
                continue;
            }

            switch (cstate->n_free_lits) {

            case 2:
                lstate->score += 4;
                break;

            case 3:
                lstate->score += 2;
                break;

            // All further cases must be 4 or more
            default:
                lstate->score += 1;
                break;

            }
        }
    }
}

static void add_true_assignment(Solver *solver, ClauseState *cstate)
{
    assert(cstate->n_free_lits > 0);

    // If this clause is just now being satisfied,
    // increment the number of satisfied clauses
    if (cstate->n_assigned_true == 0) {
        solver->n_sat_clauses += 1;
    }

    cstate->n_assigned_true += 1;
    cstate->n_free_lits -= 1;
}

static void add_false_assignment(Solver *solver, ClauseState *cstate)
{
    assert(cstate->n_free_lits > 0);

    // If this clause is just now becoming a contradiction,
    // increment the number of contradictions
    if (cstate->n_assigned_true == 0 && cstate->n_free_lits == 1) {
        solver->n_unsat_clauses += 1;
    }

    cstate->n_assigned_false += 1;
    cstate->n_free_lits -= 1;

    // If this is a new unit clause, add it to the stack
    if (cstate->n_assigned_true == 0 && cstate->n_free_lits == 1) {
        Literal unit = get_unit(solver, cstate);
        solver->unit_stack[solver->n_units++] = unit;
    }
}

static void undo_true_assignment(Solver *solver, ClauseState *cstate)
{
    assert(cstate->n_free_lits < cstate->n_lits);

    cstate->n_assigned_true -= 1;
    cstate->n_free_lits += 1;

    // If this clause just stopped being satisfied,
    // decrement the number of satisfied clauses
    if (cstate->n_assigned_true == 0) {
        solver->n_sat_clauses -= 1;
    }
}

static void undo_false_assignment(Solver *solver, ClauseState *cstate)
{
    assert(cstate->n_free_lits < cstate->n_lits);

    cstate->n_assigned_false -= 1;
    cstate->n_free_lits += 1;

    // If this clause just stopped being a contradiction,
    // decrement the number of contradictions
    if (cstate->n_assigned_true == 0 && cstate->n_free_lits == 1) {
        solver->n_unsat_clauses -= 1;
    }
}

static Literal get_unit(const Solver *solver, const ClauseState *cstate)
{
    unsigned int i;
    Literal lit;

    // The correctness of this function depends on the guarantee that each
    // clause contains at most one copy of each literal. If it is possible
    // for a clause to contain two of the same literal, then a situation may
    // arise in which `get_unit` is called on a clause where all literals are
    // false, but the number of free literals has not been decremented yet.

    assert(cstate->n_free_lits == 1);
    assert(cstate->n_assigned_true == 0);

    for (i = 0; i < cstate->n_lits; ++i) {
        lit = cstate->lits[i];
        // The first unassigned literal must be the unit
        if (solver->lits[lit].fixed == false) {
            return lit;
        }
    }

    // Should be unreachable
    return -1;
}

static void make_assignment(Solver *solver, Literal lit)
{
    unsigned int i;
    LitState *lstate = &solver->lits[lit];
    LitState *nlstate = &solver->lits[negate(lit)];

    // Make sure the variable is not assigned
    assert(lstate->fixed == false);
    assert(nlstate->fixed == false);

    // This must happen before unit propagation
    // to ensure the correct literal is selected
    lstate->fixed = true;
    lstate->assigned = true;
    nlstate->fixed = true;
    nlstate->assigned = false;

    // Add a true assignment to each clause in which
    // this literal appears unnegated
    for (i = 0; i < lstate->n_cont_clauses; ++i) {
        ClauseState *cstate = lstate->cont_clauses[i];
        add_true_assignment(solver, cstate);
    }

    // Add a false assignment to each clause in which
    // this literal appears negated
    for (i = 0; i < nlstate->n_cont_clauses; ++i) {
        ClauseState *cstate = nlstate->cont_clauses[i];
        add_false_assignment(solver, cstate);
    }
}

static void undo_assignment(Solver *solver, Literal lit)
{
    unsigned int i;
    LitState *lstate = &solver->lits[lit];
    LitState *nlstate = &solver->lits[negate(lit)];

    // Make sure the variable is assigned true
    assert(lstate->fixed == true);
    assert(lstate->assigned == true);
    assert(nlstate->fixed == true);
    assert(nlstate->assigned == false);

    // Undo this variable's true assignments
    for (i = 0; i < lstate->n_cont_clauses; ++i) {
        ClauseState *cstate = lstate->cont_clauses[i];
        undo_true_assignment(solver, cstate);
    }

    // Undo this variable's false assignments
    for (i = 0; i < nlstate->n_cont_clauses; ++i) {
        ClauseState *cstate = nlstate->cont_clauses[i];
        undo_false_assignment(solver, cstate);
    }

    lstate->fixed = false;
    nlstate->fixed = false;
}

static Solution search_assignments(Solver *solver)
{
    Solution solution;
    Literal branch;

    if (any_contradiction(solver)) {
        return SOLUTION_UNSATISFIABLE;
    } else if (all_satisfied(solver)) {
        return SOLUTION_SATISFIABLE;
    }

    branch = choose_branch(solver);

    solution = try_assignment(solver, branch);
    if (solution != SOLUTION_UNSATISFIABLE) {
        return solution;
    }

    solution = try_assignment(solver, negate(branch));
    if (solution != SOLUTION_UNSATISFIABLE) {
        return solution;
    }

    return SOLUTION_UNSATISFIABLE;
}

static Solution try_assignment(Solver *solver, Literal branch)
{
    Solution solution;
    unsigned int prev_n_assigned;

    /*
     * 1. Assign branch and do unit propagation
     */

    prev_n_assigned = solver->n_assigned;

    solver->t_branches += 1;
    solver->assigned[solver->n_assigned++] = branch;
    make_assignment(solver, branch);

    while (solver->n_units > 0) {
        Literal unit = solver->unit_stack[--solver->n_units];
        if (! solver->lits[unit].fixed) {
            solver->t_unit_props += 1;
            solver->assigned[solver->n_assigned++] = unit;
            make_assignment(solver, unit);
        } else if (! solver->lits[unit].assigned) {
            // If a false unit has been derived,
            // the formula is unsatisfiable
            solution = SOLUTION_UNSATISFIABLE;
            solver->n_units = 0;
            goto backtrack;
        }
    }

    /*
     * 2. Recursively search for a valid assignment.
     */

    solution = search_assignments(solver);
    if (solution == SOLUTION_UNSATISFIABLE) {
    backtrack:
        while (solver->n_assigned > prev_n_assigned) {
            Literal lit = solver->assigned[--solver->n_assigned];
            undo_assignment(solver, lit);
        }
        assert(solver->n_assigned == prev_n_assigned);
    }

    return solution;
}

static bool all_satisfied(const Solver *solver)
{
    return solver->n_sat_clauses == solver->n_clauses;
}

static bool any_contradiction(const Solver *solver)
{
    return solver->n_unsat_clauses > 0;
}

static Error read_problem(Solver *solver, FILE *stream)
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
            fprintf(stderr, "Expected problem line\n");
            err = ERROR_INVALID_FORMAT;
            goto cleanup;
        }
    } while (buffer[0] == 'c');

    /*
     * 2. Parse problem line
     */

    // Make sure the next line is the problem line
    if (buffer[0] != 'p') {
        fprintf(stderr, "Expected problem line\n");
        err = ERROR_INVALID_FORMAT;
        goto cleanup;
    }

    // Parse the beginning of the problem line
    if (sscanf(buffer, "p cnf %d %d%n", &n_vars, &n_clauses, &pos) != 2) {
        fprintf(stderr, "Invalid problem line\n");
        err = ERROR_INVALID_FORMAT;
        goto cleanup;
    }

    // Make sure the rest of the line is empty
    while (buffer[pos] != '\0') {
        if (! isspace(buffer[pos++])) {
            fprintf(stderr, "Invalid problem line\n");
            err = ERROR_INVALID_FORMAT;
            goto cleanup;
        }
    }

    // Make sure the values read are valid
    if (n_vars <= 0) {
        fprintf(stderr, "Number of variables must be positive\n");
        err = ERROR_INVALID_FORMAT;
        goto cleanup;
    } else if (n_clauses <= 0) {
        fprintf(stderr, "Number of clauses must be positive\n");
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
                fprintf(stderr, "Expected more clauses\n");
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
            fprintf(stderr, "Expected end of input\n");
            err = ERROR_INVALID_FORMAT;
        }
    }

    return ERROR_OK;

cleanup_solver:
    delete_solver(solver);

cleanup:
    return err;
}

static void write_solution(const Solver *solver, FILE *stream)
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

    fprintf(stream, "c Generated by SimpleSAT " SIMPLESAT_VERSION "\n");
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

