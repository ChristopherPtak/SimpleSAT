
#include "solver.h"

#include <assert.h>
#include <stdbool.h>
#include <time.h>

#include "error.h"
#include "utils.h"

Literal negate(Literal lit)
{
    // Flip the least significant bit
    return lit ^ 1;
}

Literal lit_from_int(int repr)
{
    assert(repr != 0);
    if (repr > 0) {
        return (repr - 1) << 1;
    } else {
        return ((-repr - 1) << 1) | 1;
    }
}

int int_from_lit(Literal lit)
{
    if (lit & 1) {
        return -((lit >> 1) + 1);
    } else {
        return (lit >> 1) + 1;
    }
}

void create_clause_state(ClauseState *cstate)
{
    cstate->n_lits = 0;
    cstate->c_lits = 4;
    CREATE_ARRAY(cstate->lits, 4);

    cstate->n_assigned_true = 0;
    cstate->n_assigned_false = 0;
    cstate->n_free_lits = 0;
}

void delete_clause_state(ClauseState *cstate)
{
    DELETE_ARRAY(cstate->lits);
}

void add_literal(ClauseState *cstate, Literal lit)
{
    if (cstate->n_lits == cstate->c_lits) {
        cstate->c_lits *= 2;
        RESIZE_ARRAY(cstate->lits, cstate->c_lits);
    }

    cstate->lits[cstate->n_lits++] = lit;
    cstate->n_free_lits += 1;
}

void create_lit_state(LitState *lstate)
{
    lstate->fixed = false;
    lstate->assigned = false;

    lstate->score = 0;

    lstate->n_cont_clauses = 0;
    lstate->c_cont_clauses = 16;
    CREATE_ARRAY(lstate->cont_clauses, 16);
}

void delete_lit_state(LitState *lstate)
{
    DELETE_ARRAY(lstate->cont_clauses);
}

void add_cont_clause(LitState *lstate, ClauseState *cstate)
{
    if (lstate->n_cont_clauses == lstate->c_cont_clauses) {
        lstate->c_cont_clauses *= 2;
        RESIZE_ARRAY(lstate->cont_clauses, lstate->c_cont_clauses);
    }

    lstate->cont_clauses[lstate->n_cont_clauses++] = cstate;
}

void create_solver(Solver *solver,
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

void delete_solver(Solver *solver)
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

void add_literal_to_clause(Solver *solver,
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

Literal choose_branch(Solver *solver)
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

void update_scores(Solver *solver)
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

void add_true_assignment(Solver *solver, ClauseState *cstate)
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

void add_false_assignment(Solver *solver, ClauseState *cstate)
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

void undo_true_assignment(Solver *solver, ClauseState *cstate)
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

void undo_false_assignment(Solver *solver, ClauseState *cstate)
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

Literal get_unit(const Solver *solver, const ClauseState *cstate)
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

void make_assignment(Solver *solver, Literal lit)
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

void undo_assignment(Solver *solver, Literal lit)
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

Solution search_assignments(Solver *solver)
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

Solution try_assignment(Solver *solver, Literal branch)
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

bool all_satisfied(const Solver *solver)
{
    return solver->n_sat_clauses == solver->n_clauses;
}

bool any_contradiction(const Solver *solver)
{
    return solver->n_unsat_clauses > 0;
}

