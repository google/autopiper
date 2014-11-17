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

#ifndef _AUTOPIPER_PIPE_H_
#define _AUTOPIPER_PIPE_H_

#include "backend/ir.h"

#include <vector>
#include <memory>

namespace autopiper {

// A Pipe is a single pipeline containing lowered IRStmts. It refers to the
// original Program's IRStmts; no copies are made.
struct Pipe {
    Pipe() {
        parent = NULL;
        spawn = NULL;
        entry = NULL;
    }

    // BBs are used during lowering but are not valid/used once statements are
    // placed in |stmts| after predication.
    IRBB* entry;  // target of spawn
    std::vector<const IRBB*> roots;  // BBs without predecessors
    std::vector<IRBB*> bbs;

    // Statements are placed in |stmts| in a valid dataflow order, but are
    // unconstrained except for pipedag edges.
    std::vector<IRStmt*> stmts;
    std::vector<std::pair<IRStmt*,IRStmt*>> backedges;  // pointers to the If ops and their targets

    Pipe* parent;     // spawn parent
    std::vector<Pipe*> children;  // spawned (direct) children
    PipeSys* sys;     // pipesys: whole tree of pipes
    IRStmt* spawn;    // spawning statement

    std::vector<std::unique_ptr<PipeStage>> stages;

    std::string ToString() const;
};

// A PipeStage collects all nodes together that are logically in the same stage
// of a single Pipe. (Note that this is slightly different from what ends up in
// the timing DAG: the timing DAG computes timing across *all* pipes, since
// inter-pipe dependences may impact timing in a single pipe, while for most
// other operations we care only about what's in a single pipe.)
struct PipeStage {
    PipeStage()
        : stage(0), stall(nullptr) {}

    int stage;  // global stage number, starting from 0.
    std::vector<IRStmt*> stmts;

    // stall signal, if any (this stage retains its content and deasserts valid
    // downstream)
    IRStmt* stall;

    // Kills that kill the whole stage, across the input valid-cut. This does
    // not include any killyoungers -- those are accounted for directly in
    // AssignKills() -- but can include inputs from other transforms, e.g.
    // kill_if condition clone insertion.
    std::vector<IRStmt*> kills;

    Pipe* pipe;
};

struct PipeSys {
    IRProgram* program;
    std::vector<std::unique_ptr<Pipe>> pipes;

    std::string ToString() const;
};

}  // namespace autopiper

#endif
