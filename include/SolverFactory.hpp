#pragma once

#include "ICPUSolver.hpp"
#include <memory>

namespace GreedyTangle {

std::unique_ptr<ICPUSolver> CreateSolver(SolverMode mode);

}
