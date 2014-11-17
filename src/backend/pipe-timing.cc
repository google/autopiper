/*
 * Copyright 2014 Google Inc. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "backend/pipe-timing.h"
#include "backend/timing-dag.h"
#include "backend/compiler.h"

#include <vector>
#include <map>

using namespace autopiper;
using namespace std;

// see http://cs.brown.edu/~jes/book/pdfs/ModelsOfComputation_Chapter2.pdf

namespace {

int Log2(int n) {
    int ret = 0;
    while (n > 1) {
        n >>= 1;
        ret++;
    }
    return ret;
}

int AddStages(int n) {
    return 4*Log2(n) + 2;
}

int MulStages(int n) {
    int s = log2(n) + 1;
    return  3*s + AddStages(n);
}

int DivStages(int n, int dividend, int divisor) {
    // If the user doesn't design a state machine-based divider manually,
    // we're not going to do it for them; assume a fully-combinational design
    // with radix 2. There's a comparator and subtractor at each stage (for
    // each output bit).
    (void)divisor;
    return n * 2*AddStages(dividend);
}

int BarrelShifter(int input_width, int shiftamt_width) {
    (void)input_width;
    // Tree of MUXes, |shiftamt_width| 2-input MUXes deep; each 2-input MUX
    // results in two logic layers
    return 2 * shiftamt_width;
}

}  // anonymous namespace

int StandardTimingModel::Delay(const IRStmt* stmt) const {
    switch (stmt->type) {
        case IRStmtExpr:
            switch (stmt->op) {
                case IRStmtOpNone:
                    return 0;
                case IRStmtOpConst:
                    return 0;
                case IRStmtOpAdd:
                case IRStmtOpSub:
                    return AddStages(stmt->width);
                case IRStmtOpMul:
                    return MulStages(stmt->width);
                case IRStmtOpDiv:
                    return DivStages(stmt->width,
                            stmt->args[0]->width,
                            stmt->args[1]->width);
                case IRStmtOpRem:
                    return DivStages(stmt->width,
                            stmt->args[0]->width,
                            stmt->args[1]->width);
                case IRStmtOpAnd:
                case IRStmtOpOr:
                case IRStmtOpNot:
                    // Simple logic layers.
                    return 1;
                case IRStmtOpXor:
                    // XOR is equivalent to two logic layers (A&~B | ~A&B).
                    return 2;
                case IRStmtOpLsh:
                case IRStmtOpRsh:
                    // If constant shift amount, then zero delay; else a
                    // barrel shifter is implied.
                    if (stmt->args[1]->type == IRStmtExpr &&
                        stmt->args[1]->op == IRStmtOpConst)
                        return 0;
                    return BarrelShifter(stmt->args[0]->width,
                                         stmt->args[1]->width);
                case IRStmtOpBitslice:
                    // Always const bitslice -- just wires.
                    return 0;
                case IRStmtOpConcat:
                    // Always const concat -- just wires.
                    return 0;
                case IRStmtOpSelect:
                    // two-input MUX has two layers: A&~Sel | B&Sel
                    return 2;
                case IRStmtOpCmpLT:
                case IRStmtOpCmpLE:
                case IRStmtOpCmpGT:
                case IRStmtOpCmpGE:
                    // subtractor, taking the sign bit as the result
                    return AddStages(stmt->width);
                case IRStmtOpCmpEQ:
                case IRStmtOpCmpNE:
                    // XORs per bit, plus a tree of ANDs
                    return 2 + Log2(stmt->width);
                default:
                    return 0;
            }
            break;
        default:
            // TODO: other primitives: array reads/writes, ...
            return 0;
    }
}

int StandardTimingModel::DelayPerStage() const {
    return kGatesPerStage;
}

namespace {
// Fits interface required by TimingDAG and reports errors to given
// ErrorCollector.
struct TimingErrorCollector {
    TimingErrorCollector(ErrorCollector* coll)
        : coll_(coll) {}

    void ReportError(const IRStmt* stmt, const IRTimeVar* var,
                     const std::string& message) {
        std::string annotated_message;
        Location loc;
        if (stmt) {
            annotated_message += strprintf("Node %%%d: ", stmt->valnum);
            loc = stmt->location;
        }
        if (var) {
            annotated_message += strprintf("Timing var '%s': ", var->name.c_str());
        }
        annotated_message += message;
        coll_->ReportError(loc, ErrorCollector::ERROR, annotated_message);
    }
 private:
    ErrorCollector* coll_;
};
}

bool PipeTimer::TimePipe(PipeSys* sys, ErrorCollector* coll) const {
    // Build timing DAG nodes
    TimingDAG<IRStmt, IRTimeVar> dag;
    for (auto& pipe : sys->pipes) {
        for (auto* stmt : pipe->stmts) {
            dag.AddNode(stmt, model_->Delay(stmt));
        }
    }
    // Add edges between nodes for dataflow dependences and pipedag edges, and
    // attach timing vars.
    for (auto& pipe : sys->pipes) {
        for (auto* stmt : pipe->stmts) {
            // Add edges for dataflow dependences
            for (auto* arg : stmt->args) {
                dag.AddEdge(arg, stmt);
            }
            for (auto* arg : stmt->pipedag_deps) {
                dag.AddEdge(arg, stmt);
            }
            if (stmt->valid_in) {
                dag.AddEdge(stmt->valid_in, stmt);
            }
            // Add timing var, if any
            if (stmt->timevar) {
                dag.AddVar(stmt, stmt->timevar, stmt->time_offset);
            }
            // TODO: mark as 'lifted' any nodes that the user lifts.
        }
    }

    // Solve!
    TimingErrorCollector err(coll);
    if (!dag.Solve(model_->DelayPerStage(), &err)) {
        // The error collector adapter should already have reported the errors
        // to `coll`.
        return false;
    }

    // Retrieve stage information from timing DAG and create PipeStages as
    // appropriate.
    //
    // Note that we start at stage 1 here, leaving stage 0 free for "insert X
    // into prior stage"-type transforms (e.g., stall logic generation) without
    // descending into negative-numbered stages.
    for (int stage = 0; stage < dag.StageCount(); stage++) {
        int stage_number = stage + 1;
        for (const auto* c_node : dag.NodesInStage(stage)) {
            IRStmt* node = const_cast<IRStmt*>(c_node);
            Pipe* pipe = node->pipe;
            // Find last stage in pipe; while < current stage, add a stage.
            // In this way, each Pipe ends up with a contiguous sequence of
            // PipeStages for the range of global pipestages over which it has
            // nodes (statements).
            while (pipe->stages.empty() ||
                   pipe->stages.back()->stage < stage_number) {
                std::unique_ptr<PipeStage> new_stage(new PipeStage());
                new_stage->stage = pipe->stages.size();
                new_stage->pipe = pipe;
                pipe->stages.push_back(move(new_stage));
            }
            PipeStage* pipestage = pipe->stages.back().get();
            pipestage->stmts.push_back(node);
            node->stage = pipestage;
        }
    }

    return true;
}
