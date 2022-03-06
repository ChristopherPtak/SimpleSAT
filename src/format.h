
#ifndef SIMPLESAT_FORMAT_H
#define SIMPLESAT_FORMAT_H

#include <stdio.h>

#include "error.h"
#include "solver.h"

Error read_problem(Solver *, FILE *);
void write_solution(const Solver *, FILE *);

#endif

