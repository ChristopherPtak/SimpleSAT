
#ifndef SIMPLESAT_SOLVER_H
#define SIMPLESAT_SOLVER_H

#include <stdbool.h>
#include <time.h>

typedef unsigned int Literal;

Literal negate(Literal);
Literal lit_from_int(int);
int int_from_lit(Literal);

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

void create_clause_state(ClauseState *);
void delete_clause_state(ClauseState *);

void add_literal(ClauseState *, Literal);

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

void create_lit_state(LitState *);
void delete_lit_state(LitState *);

// Adds the clause to the array in the LitState
// Does NOT modify the ClauseState
void add_cont_clause(LitState *, ClauseState *);

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

void create_solver(Solver *, unsigned int, unsigned int);
void delete_solver(Solver *);

void add_literal_to_clause(Solver *, ClauseState *, Literal);

Literal choose_branch(Solver *);
void update_scores(Solver *);

void add_true_assignment(Solver *, ClauseState *);
void add_false_assignment(Solver *, ClauseState *);
void undo_true_assignment(Solver *, ClauseState *);
void undo_false_assignment(Solver *, ClauseState *);

Literal get_unit(const Solver *, const ClauseState *);

void make_assignment(Solver *, Literal);
void undo_assignment(Solver *, Literal);

Solution search_assignments(Solver *);
Solution try_assignment(Solver *, Literal);

bool all_satisfied(const Solver *);
bool any_contradiction(const Solver *);

#endif

