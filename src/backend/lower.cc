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

#include "backend/pipe.h"
#include "backend/ir.h"
#include "backend/compiler.h"
#include "common/util.h"
#include "backend/rpo.h"
#include "backend/predicate.h"
#include "backend/ir-build.h"
#include "backend/pipe-timing.h"

#include <algorithm>
#include <map>
#include <vector>
#include <queue>
#include <deque>
#include <set>

using namespace autopiper;
using namespace std;

namespace {

bool FindPipes(IRProgram* program,
               PipeSys* sys,
               IRBB* entry,
               ErrorCollector* coll) {
    // We extract pipelines one at a time. Each pipeline starts with an entry
    // point (this is initially the program entry point), and all reachable
    // BBs are merged into the pipe. Spawned BBs are added to the entrypoint
    // set for later processing. We enforce that each pipeline can have only
    // one spawn-point.
    queue<unique_ptr<Pipe>> to_process;

    unique_ptr<Pipe> entry_pipe(new Pipe());
    entry_pipe->entry = entry;
    entry_pipe->sys = sys;
    to_process.push(move(entry_pipe));

    while (!to_process.empty()) {
        unique_ptr<Pipe> pipe = move(to_process.front());
        to_process.pop();

        queue<IRBB*> bbs;
        bbs.push(pipe->entry);

        while (!bbs.empty()) {
            IRBB* bb = bbs.front();
            bbs.pop();
            if (bb->pipe == pipe.get()) continue;
            if (bb->pipe != NULL) {
                coll->ReportError(bb->location, ErrorCollector::ERROR,
                        strprintf("BB '%s' is already part of another pipeline, spawned from stmt %%%d",
                            bb->label.c_str(),
                            bb->pipe->spawn ? bb->pipe->spawn->valnum : -1));
                return false;
            }

            bb->pipe = pipe.get();
            pipe->bbs.push_back(bb);
            for (auto& stmt : bb->stmts) {
                if (stmt->type == IRStmtIf || stmt->type == IRStmtJmp) {
                    for (auto* target : stmt->targets) {
                        if (target->pipe == pipe.get()) continue;
                        bbs.push(target);
                    }
                }
                if (stmt->type == IRStmtSpawn) {
                    IRBB* target = stmt->targets[0];
                    if (target->pipe != NULL) {
                        coll->ReportError(stmt->location, ErrorCollector::ERROR,
                                strprintf("Spawn to a target already in another pipe, spawned by valnum %%%d",
                                    target->pipe->spawn ? target->pipe->spawn->valnum : -1));
                        return false;
                    }
                    unique_ptr<Pipe> spawned(new Pipe());
                    spawned->entry = target;
                    spawned->parent = pipe.get();
                    spawned->parent->children.push_back(spawned.get());
                    spawned->sys = sys;
                    spawned->spawn = stmt.get();
                    to_process.push(move(spawned));
                }
            }
        }

        sys->pipes.push_back(move(pipe));
    }

    return true;
}

// Checks that uses of a chan are in the same txn tree (pipesys)
bool CheckChanUses(IRProgram* program,
                   PipeSys* sys,
                   ErrorCollector* coll) {
    for (auto& pipe : sys->pipes) {
        for (auto& bb : pipe->bbs) {
            for (auto& stmt : bb->stmts) {
                if (stmt->type == IRStmtChanRead &&
                    stmt->port->defs.empty()) {
                    coll->ReportError(stmt->location, ErrorCollector::ERROR,
                            strprintf("Statement %%%d uses chan '%s' with no writer",
                                stmt->valnum, stmt->port->name.c_str()));
                    return false;
                }
                if (stmt->type == IRStmtChanRead) {
                    for (auto* def : stmt->port->defs) {
                        if (def->bb->pipe->sys != sys) {
                            coll->ReportError(stmt->location, ErrorCollector::ERROR,
                                    strprintf("Statement %%%d uses chan '%s' outside of its "
                                              "defining spawn-tree.", stmt->valnum,
                                              stmt->port->name.c_str()));
                            return false;
                        }
                    }
                }
            }
        }
    }
    return true;
}

bool ComputeKillyoungerDom(IRProgram* program,
                           PipeSys* sys,
                           Pipe* pipe,
                           ErrorCollector* coll) {
    BBReversePostorder rpo;
    rpo.Compute( { pipe->entry } );

    // is a BB's out-edge dominated by a killyounger (where dominance counts
    // only forward edges)?
    map<const IRBB*, IRStmt*> out_killyounger;

    for (auto* bb : rpo.RPO()) {
        // Compute join over predecessors: only have a dominating killyounger
        // if every pred has that killyounger.
        IRStmt* join = NULL;
        bool first = true;
        for (auto* pred : rpo.Preds(bb)) {
            IRStmt* pred_dom = out_killyounger[pred];
            if (first) {
                join = pred_dom;
            } else {
                if (pred_dom != join) {
                    join = NULL;
                    break;
                }
            }
        }

        // scan forward to propagate dom-killyounger.
        IRStmt* dom = join;
        for (auto& stmt : bb->stmts) {
            stmt->dom_killyounger = dom;
            if (stmt->type == IRStmtKillYounger) dom = stmt.get();
        }

        out_killyounger[bb] = dom;
    }
    
    return true;
}

void ConvertBackedgePhis(
        IRProgram* program,
        Pipe* pipe,
        IRBB* source_bb,
        IRStmt* backedge_op, /* in its own BB */
        IRBB* dest_bb) {
    // Backedge is retargeted to restart header, which contains the necessary
    // restart value sources.
    unique_ptr<IRBB> restart_header(new IRBB());
    restart_header->label = backedge_op->bb->label + "_restart_";

    // Generate the backedge valid restart signal. This is linked up to the
    // backedge predicate during codegen.
    restart_header->restart_cond =
        AppendOwnedToVector(restart_header->stmts, new IRStmt());
    restart_header->restart_cond->valnum = program->GetValnum();
    restart_header->restart_cond->bb = restart_header.get();
    restart_header->restart_cond->type = IRStmtRestartValue;
    restart_header->restart_cond->width = 1;
    restart_header->restart_cond->is_valid_start = true;
    restart_header->restart_cond->valid_spine = true;
    restart_header->is_restart = true;

    // Generate the source for the restart header's condition/valid signal.
    IRStmt* restart_valid_src =
        PrependOwnedToVector(backedge_op->bb->stmts, new IRStmt());
    restart_valid_src->valnum = program->GetValnum();
    restart_valid_src->bb = backedge_op->bb;
    restart_valid_src->type = IRStmtRestartValueSrc;
    restart_valid_src->width = 1;
    backedge_op->bb->restart_pred_src = restart_valid_src;
    // arg is filled in during predication.

    restart_header->restart_cond->restart_arg = restart_valid_src;

    backedge_op->restart_target = restart_header.get();

    vector<unique_ptr<IRStmt>> restart_value_srcs;

    // Find phis in backedge dest with input from the source BB.
    for (auto& phi_stmt : dest_bb->stmts) {
        if (phi_stmt->type != IRStmtPhi) continue;
        for (unsigned i = 0; i < phi_stmt->args.size(); i++) {
            if (phi_stmt->targets[i] != source_bb) continue;

            IRStmt* restart_value =
                AppendOwnedToVector(restart_header->stmts, new IRStmt());
            restart_value->valnum = program->GetValnum();
            restart_value->type = IRStmtRestartValue;
            restart_value->width = phi_stmt->width;
            restart_value->bb = restart_header.get();

            IRStmt* restart_value_src =
                AppendOwnedToVector(restart_value_srcs, new IRStmt());
            restart_value_src->valnum = program->GetValnum();
            restart_value_src->type = IRStmtRestartValueSrc;
            restart_value_src->width = phi_stmt->width;
            restart_value_src->args.push_back(phi_stmt->args[i]);
            restart_value_src->arg_nums.push_back(phi_stmt->arg_nums[i]);
            restart_value_src->bb = backedge_op->bb;
            restart_value->restart_arg = restart_value_src;

            phi_stmt->args[i] = restart_value;
            phi_stmt->arg_nums[i] = restart_value->valnum;
            phi_stmt->targets[i] = restart_value->bb;
            phi_stmt->target_names[i] = restart_value->bb->label;
        }
    }

    // Generate a jump to restart point.
    IRStmt* jmp =
        AppendOwnedToVector(restart_header->stmts, new IRStmt());
    jmp->valnum = program->GetValnum();
    jmp->type = IRStmtJmp;
    jmp->target_names.push_back(dest_bb->label);
    jmp->targets.push_back(dest_bb);

    // Prepend the restart value src ops to the backedge BB.
    for (auto& stmt : backedge_op->bb->stmts) {
        restart_value_srcs.push_back(move(stmt));
    }
    swap(backedge_op->bb->stmts, restart_value_srcs);
    restart_value_srcs.clear();

    // Backedge op points to restart BB via its first target slot, but it does
    // *not* count as a control-flow op, so the backedge cycle is broken
    // (further stages need an acyclic CFG).
    backedge_op->targets.push_back(restart_header.get());

    pipe->bbs.push_back(restart_header.get());
    pipe->roots.push_back(restart_header.get());
    program->bbs.push_back(move(restart_header));
}

bool ConvertBackedges(IRProgram* program,
                      PipeSys* sys,
                      Pipe* pipe,
                      ErrorCollector* coll) {
    BBReversePostorder rpo;
    rpo.Compute( { pipe->entry } );

    // For each backedge, insert the appropriate barriers. We determine whether
    // the jump is dominated by a killyounger; if so, the killyounger and jump
    // are constrained to the same stage (new timing var); if not, the jump and
    // its backward target are constrained to the same stage.
    
    set<const IRBB*> seen;
    for (auto* bb : rpo.RPO()) {
        seen.insert(bb);

        // Find the Jmp or If terminating this BB, if any.
        IRStmt* term = nullptr;
        for (auto& stmt : bb->stmts) {
            if (stmt->type == IRStmtJmp || stmt->type == IRStmtIf) {
                term = stmt.get();
                break;
            }
        }

        // If no terminator, or either of the terminator's targets are not
        // backedges, we're not interested.
        if (!term) continue;
        // Find any out-edges that are backedges.
        for (unsigned i = 0; i < term->targets.size(); i++) {
            // Is this target a BB we've seen before in RPO? If so, backedge.
            if (seen.find(term->targets[i]) != seen.end()) {
                // Convert to a target to a new BB with a single 'backedge' op.
                unique_ptr<IRBB> backedge_bb(new IRBB());
                backedge_bb->stmts.emplace_back(new IRStmt());
                IRStmt* backedge_op = backedge_bb->stmts.back().get();
                backedge_op->bb = backedge_bb.get();
                pipe->bbs.push_back(backedge_bb.get());
                program->bbs.push_back(move(backedge_bb));

                backedge_op->valnum = program->GetValnum();
                backedge_op->bb->label = strprintf("__backedge_bb_%d",
                                                   backedge_op->valnum);
                backedge_op->type = IRStmtBackedge;
                backedge_op->dom_killyounger = term->dom_killyounger;

                // Insert a barrier either at the start of the backedge target
                // BB, if no dominating killyounger, or right before the most
                // recent killyounger, if so.
                IRStmt* backedge_barrier = NULL;
                if (backedge_op->dom_killyounger) {
                    backedge_barrier =
                        InsertOwnedBefore(
                                backedge_op->dom_killyounger->bb->stmts,
                                backedge_op->dom_killyounger,
                                new IRStmt());
                    backedge_barrier->bb = backedge_op->dom_killyounger->bb;
                } else {
                    backedge_barrier =
                        PrependOwnedToVector(term->targets[i]->stmts, new IRStmt());
                    backedge_barrier->bb = term->targets[i];
                }
                backedge_barrier->valnum = program->GetValnum();
                backedge_barrier->type = IRStmtTimingBarrier;

                // Create a timevar that ensures the backedge source and dest
                // are in the same stage.
                auto* timevar = program->GetTimeVar();
                timevar->name = strprintf("__backedge_timevar_%d",
                                          backedge_op->valnum);
                backedge_op->timevar = timevar;
                backedge_op->time_offset = 0;
                backedge_barrier->timevar = timevar;
                backedge_barrier->time_offset = 0;

                // This creates the backedge restart block and sets
                // backedge_op's restart_target appropriately.
                ConvertBackedgePhis(
                        program, pipe, term->bb, backedge_op, term->targets[i]);

                term->targets[i] = backedge_op->bb;
                term->target_names[i] = backedge_op->bb->label.c_str();
            }
        }
    }

    // Main root (entry point) comes *last* so that pipedag deps for restart
    // valids work.
    pipe->roots.push_back(pipe->entry);

    return true;
}

// Helper for if-conversion.
IRStmt* BuildPredicateExpr(const Predicate<IRStmt*>& pred,
                           IRBB* bb,
                           IRBBBuilder* builder) {
    vector<IRStmt*> term_values;
    for (auto& term : pred.Terms()) {
        vector<IRStmt*> factor_values;
        for (auto& factor : term.Factors()) {
            IRStmt* value = factor.first;
            bool polarity = factor.second;
            if (!polarity)
                value = builder->AddExpr(IRStmtOpNot, { value });
            factor_values.push_back(value);
        }
        term_values.push_back(builder->BuildTree(IRStmtOpAnd, factor_values));
    }

    IRStmt* value = builder->BuildTree(IRStmtOpOr, term_values);
    return value;
}

class PredicateMemoizer {
 public:
  PredicateMemoizer() {}

  IRStmt* GetPredStmt(IRBBBuilder* builder,
                      IRBB* bb,
                      const Predicate<IRStmt*>& pred) {
      auto i = stmts_.find(pred);
      if (i == stmts_.end()) {
          IRStmt* stmt = BuildPredicateExpr(pred, bb, builder);
          stmts_.insert(make_pair(pred, stmt));
          return stmt;
      } else {
          return i->second;
      }
  }

 private:
  typedef Predicate<IRStmt*> KeyType;
  map<KeyType, IRStmt*> stmts_;
};

IRStmt* BuildMuxTree(IRBBBuilder* builder,
                     const IRStmt* stmt,
                     PredicateMemoizer* memo) {
    assert(stmt->type == IRStmtPhi);
    // Build a MUX tree. At each level of the tree, when two values are joined,
    // we pick the second input's predicate.

    assert(stmt->args.size() > 0);
    
    // Compute pairs of (predicate, value) MUX inputs.
    vector<pair<Predicate<IRStmt*>, IRStmt*>> inputs;
    for (unsigned i = 0; i < stmt->args.size(); i++) {
        Predicate<IRStmt*> pred_val = stmt->args[i]->valid_out_pred;
        // Sometimes predicate joins are smart enough to figure out that a
        // certain BB is unreachable...
        if (pred_val.IsFalse()) continue;
        inputs.push_back(make_pair(pred_val, stmt->args[i]));
    }

    // Build layers of the tree.
    while (inputs.size() > 1) {
        vector<pair<Predicate<IRStmt*>, IRStmt*>> next;
        for (unsigned i = 0; i < inputs.size(); i += 2) {
            if (i == inputs.size() - 1) {
                // Carry the same predicate forward.
                next.push_back(inputs[i]);
            } else {
                // Choose an input-select predicate: prefer restart predicates
                // over not; prefer any predicates over tautologies (constant
                // 'true'); else, pick any. Higher layers ensure that any
                // choice we make here is valid (i.e., if two backedges arrive
                // at the same place, the one from a later pipestage must also
                // be 'killyounger', which will qualify/gate out the earlier
                // backedge predicate).
                Predicate<IRStmt*> sel_pred;
                if (inputs[i].first.IsBackedge())
                    sel_pred = inputs[i].first;
                else if (inputs[i+1].first.IsBackedge())
                    sel_pred = inputs[i+1].first;
                else if (inputs[i].first.IsTrue())
                    sel_pred = inputs[i+1].first;
                else
                    sel_pred = inputs[i].first;
                Predicate<IRStmt*> joined_pred = inputs[i].first.OrWith(inputs[i+1].first); // predicate on output
                IRStmt* sel_input = memo->GetPredStmt(builder, stmt->bb, sel_pred);
                assert(sel_input != nullptr);
                IRStmt* mux = builder->AddExpr(IRStmtOpSelect,
                                               { sel_input,
                                                 inputs[i].second,
                                                 inputs[i+1].second });
                next.push_back(make_pair(joined_pred, mux));
            }
        }
        swap(inputs, next);
        next.clear();
    }
    assert(inputs.size() == 1);
    return inputs[0].second;
}

IRStmt* FindReplacement(
        map<const IRStmt*, IRStmt*>& replacements,
        IRStmt* s) {
    auto i = replacements.find(s);
    if (i == replacements.end()) {
        return s;
    }
    return FindReplacement(replacements, i->second);
}

// Perform if-conversion on statements, and place them in the statement list.
bool IfConvert(IRProgram* program,
               PipeSys* sys,
               Pipe* pipe,
               ErrorCollector* coll) {

    // We perform "valid signal insertion" along all control-flow paths. A
    // statement propagates its input valid to its output valid, except:
    // - If statements add a condition to each side of the branch.
    // - Kill statements set valid to false.
    //
    // At merge points (joins), predicates are OR'd together.
    //
    // To optimize the generated code somewhat, we use the 'Predicate' class to
    // normalize predicate expressions to sum-of-products form, and to remove
    // redundant expressions (p + ~p -> true; p & ~p -> remove whole term).

    // For all BBs, initialize input predicates.
    for (auto* bb : pipe->bbs) {
        bb->in_pred = Predicate<IRStmt*>::True();
    }

    // For all existing roots that are not restarts, initialize predicate.
    for (const auto* const_bb : pipe->roots) {
        IRBB* bb = const_cast<IRBB*>(const_bb);
        if (bb->restart_cond) continue;
        IRStmt* entry_valid = PrependOwnedToVector(bb->stmts, new IRStmt());
        entry_valid->valnum = program->GetValnum();
        entry_valid->bb = bb;
        entry_valid->type = IRStmtRestartValue;
        entry_valid->width = 1;
        entry_valid->is_valid_start = true;
        entry_valid->valid_spine = true;
    }
    
    // For all 'valid start' entries, initialize predicate.
    for (auto* bb : pipe->bbs) {
        for (auto& stmt : bb->stmts) {
            if (stmt->is_valid_start) {
                // If this is the start of a valid-signal (e.g. in a restart
                // header), begin with this statement itself as a predicate. In
                // practice, this is a 'RestartValue' stmt that gets linked to
                // a RestartValueSrc for restart headers or constant 'true' for
                // entry points.
                stmt->valid_out_pred = Predicate<IRStmt*>::True().AndWith(stmt.get(), true);
                // Backedge flag on predicate prioritizes it in MUX-tree select
                // generation.
                if (bb->restart_cond) stmt->valid_out_pred.SetBackedge();
            }
        }
    }

    // Do a forward pass, propagating predicate across statements and
    // performing joins at each bb-in.
    BBReversePostorder rpo;
    rpo.Compute(pipe->roots);
    for (auto* _bb : rpo.RPO()) {
        IRBB* bb = const_cast<IRBB*>(_bb);

        // Compute in-valid based on predecessors.
        Predicate<IRStmt*> in_pred = Predicate<IRStmt*>::False();
        bool have_preds = false;
        for (auto* pred : rpo.Preds(bb)) {
            have_preds = true;
            // Find which successor we are to this bb, and take its output
            // along that edge.
            int which_succ = pred->WhichSucc(bb);
            if (which_succ == -1) continue;
            in_pred = in_pred.OrWith(pred->out_preds[which_succ]);
        }
        if (!have_preds) {
            in_pred = Predicate<IRStmt*>::True();
        }

        // Propagate valid signal across statements.
        for (auto& stmt : bb->stmts) {
            // Propagate last stmt's out to this stmt's in.
            stmt->valid_in_pred = in_pred;
            // Transfer func: branches, kills, and 'starts' are special.
            if (stmt->is_valid_start) {
                // Already set above.
            } else if (stmt->type == IRStmtKill) {
                stmt->valid_out_pred = Predicate<IRStmt*>::False();
            } else if (stmt->type == IRStmtKillIf) {
                // KillIf kills immediately (in this stage) and is also
                // replicated downstream at each successive pipestage, after
                // pipe timing occurs, to create the "continuous monitoring"
                // semantics. Here we just need to create a predicate that is
                // gated by the inverse of its arg.
                stmt->valid_out_pred =
                    stmt->valid_in_pred.AndWith(stmt->args[0], false);
            } else {
                stmt->valid_out_pred = stmt->valid_in_pred;
            }
            in_pred = stmt->valid_out_pred;
        }

        // Compute out-valids to all targets.
        const IRStmt* succ = bb->SuccStmt();
        if (succ != NULL) {
            if (succ->type == IRStmtJmp) {
                bb->out_preds.push_back(succ->valid_out_pred);
            } else if (succ->type == IRStmtIf) {
                bb->out_preds.push_back(
                        succ->valid_out_pred.AndWith(succ->args[0], true));
                bb->out_preds.push_back(
                        succ->valid_out_pred.AndWith(succ->args[0], false));
            }
        }
    }


    // Now do a pass over all preds and convert to concrete valid signals,
    // inserting valid-signal computation logic where necessary.
    PredicateMemoizer memo;
    for (auto* _bb : rpo.RPO()) {
        IRBB* bb = const_cast<IRBB*>(_bb);
        unique_ptr<IRBBBuilder> builder(new IRBBBuilder(program, bb));
        bb->in_valid = memo.GetPredStmt(builder.get(), bb, bb->in_pred);
        for (auto& stmt : bb->stmts) {
            IRStmt* s = stmt.get();
            s->valid_in = memo.GetPredStmt(builder.get(), bb, s->valid_in_pred);
            builder->AddStmt(move(stmt));
            s->valid_out = memo.GetPredStmt(builder.get(), bb, s->valid_out_pred);
        }
        for (auto& out_pred : bb->out_preds) {
            bb->out_valids.push_back(memo.GetPredStmt(builder.get(), bb, out_pred));
        }
        builder->ReplaceBB();
    }

    // Make a pass to set RestartValueSrc args on RestartValueSrcs that feed
    // backedge valids.
    for (auto* bb : pipe->bbs) {
        if (bb->restart_pred_src) {
            bb->restart_pred_src->args.push_back(bb->restart_pred_src->valid_in);
            bb->restart_pred_src->arg_nums.push_back(bb->restart_pred_src->valid_in->valnum);
        }
    }

    // Make a pass to propagate valid_spine bits.
    bool changed_valid_spine = true;
    while (changed_valid_spine) {
        changed_valid_spine = false;
        // Yes, this would be more efficient if we traversed stmt nodes in RPO.
        // This is quick and easy, though, and in practice the nodes are
        // *mostly* in the right order.
        for (auto* bb : pipe->bbs) {
            for (auto& stmt : bb->stmts) {
                bool old = stmt->valid_spine;
                for (auto* arg : stmt->args) {
                    if (arg->valid_spine) {
                        stmt->valid_spine = true;
                    }
                }
                if (stmt->valid_spine != old) {
                    changed_valid_spine = true;
                }
            }
        }
    }

    // Now convert all phi-nodes to MUXes.
    map<const IRStmt*, IRStmt*> replacements;
    for (auto* bb : pipe->bbs) {
        unique_ptr<IRBBBuilder> builder(new IRBBBuilder(program, bb));
        for (auto& stmt : bb->stmts) {
            if (stmt->type != IRStmtPhi) continue;
            replacements[stmt.get()] = BuildMuxTree(builder.get(), stmt.get(),
                                                    &memo);
        }
        builder->PrependToBB();
    }
    // Rewrite args using replaced IRStmts.
    for (auto& bb : pipe->bbs) {
        for (auto& stmt : bb->stmts) {
            for (unsigned i = 0; i < stmt->args.size(); i++) {
                stmt->args[i] = FindReplacement(replacements, stmt->args[i]);
                stmt->arg_nums[i] = stmt->args[i]->valnum;
            }
        }
    }

    // Remove phi nodes.
    for (auto* bb : pipe->bbs) {
        vector<unique_ptr<IRStmt>> stmts;
        for (auto& stmt : bb->stmts) {
            if (stmt->type == IRStmtPhi) continue;
            stmts.push_back(move(stmt));
        }
        bb->stmts.swap(stmts);
        stmts.clear();
    }

    return true;
}

// Produce a side-effect-dep graph from the CFG before we lose it: each
// side-effecting op depends either on the previous side-effecting op in its
// same BB, if it's not the first; or if it is the first, then on the last
// side-effecting op in each in-edge. We do this with a simple forward pass in
// reverse postorder.
bool BuildPipeDAG(IRProgram* program,
                  PipeSys* sys,
                  Pipe* pipe,
                  ErrorCollector* coll) {
    BBReversePostorder rpo;
    rpo.Compute( { pipe->entry } );

    // Map per block of outgoing side-effect spine. This is set of last
    // side-effecting ops that were seen on this block's postdom frontier,
    // i.e., these are all barriers that must come before any side-effecting op
    // in this BB. We keep a barrier set and not just a single pre-joined point
    // because we avoid generating extra join barriers where unnecessary (BBs
    // with no side-effects).
    map<const IRBB*, vector<IRStmt*>> last_side_effect;
    // Map of set of timing barriers that must come before any unconstrained
    // (side-effect-free) op after the given BB.
    map<const IRBB*, vector<IRStmt*>> last_timing_barriers;
    // Map of set of unconstrained (side-effect-free) ops at the end of the
    // given BB that must be constrained behind any new timing barriers.
    map<const IRBB*, vector<IRStmt*>> unconstrained_pure_ops;

    for (auto* bb : rpo.RPO()) {
        vector<IRStmt*> pred_barriers;
        vector<IRStmt*> timing_barriers;
        vector<IRStmt*> pure_ops;
        for (auto* pred : rpo.Preds(bb)) {
            pred_barriers.insert(pred_barriers.end(),
                    last_side_effect[pred].begin(),
                    last_side_effect[pred].end());
            timing_barriers.insert(timing_barriers.end(),
                    last_timing_barriers[pred].begin(),
                    last_timing_barriers[pred].end());
            pure_ops.insert(pure_ops.end(),
                    unconstrained_pure_ops[pred].begin(),
                    unconstrained_pure_ops[pred].end());
        }

        IRStmt* last = nullptr;
        IRStmt* last_timing_barrier = nullptr;
        if (rpo.Preds(bb).empty() && pipe->spawn) {
            last = pipe->spawn;
        }
        for (auto& stmt : bb->stmts) {
            if (!IRHasSideEffects(stmt->type)) {
                // Non-side-effecting ops are constrained to come after the
                // spawn point for this pipe, and after the last timing
                // barrier.
                if (pipe->spawn) {
                    stmt->pipedag_deps.push_back(pipe->spawn);
                }
                if (last_timing_barrier) {
                    stmt->pipedag_deps.push_back(last_timing_barrier);
                } else {
                    stmt->pipedag_deps = timing_barriers;
                }
                pure_ops.push_back(stmt.get());
                continue;
            }
            if (stmt->type == IRStmtIf || stmt->type == IRStmtJmp) continue;
            if (!last) {
                stmt->pipedag_deps = pred_barriers;
            } else {
                stmt->pipedag_deps.push_back(last);
            }
            last = stmt.get();
            if (stmt->type == IRStmtTimingBarrier) {
                last_timing_barrier = stmt.get();
                for (auto* op : pure_ops) {
                    stmt->pipedag_deps.push_back(op);
                }
                pure_ops.clear();
            }
        }

        if (last) {
            last_side_effect[bb].push_back(last);
        } else {
            last_side_effect[bb] = pred_barriers;
        }
        if (last_timing_barrier) {
            last_timing_barriers[bb].push_back(last_timing_barrier);
        } else {
            last_timing_barriers[bb] = timing_barriers;
        }
        unconstrained_pure_ops[bb] = pure_ops;
    }

    // Add pipe-DAG edges from chan defs to chan uses. (Note: ports are not
    // constrained here because reads/writes are "outside of normal time"
    // w.r.t. a transaction's flow down the pipe, i.e., they happen in whatever
    // stage they happen in.)
    for (auto* bb : pipe->bbs) {
        for (auto& stmt : bb->stmts) {
            if (IRReadsPort(stmt->type) && stmt->port &&
                stmt->port->type == IRPort::CHAN) {
                for (auto* def : stmt->port->defs) {
                    stmt->pipedag_deps.push_back(def);
                }
            }
        }
    }

    return true;
}

bool FlattenPipe(IRProgram* program,
                 PipeSys* sys,
                 Pipe* pipe,
                 ErrorCollector* coll) {
    // Remove all terminators and phi-nodes from BBs, and flatten stmts into
    // one list for the pipe.
    BBReversePostorder rpo;
    rpo.Compute(pipe->roots);
    for (auto* bb : pipe->bbs) {
        for (auto& stmt : bb->stmts) {
            switch (stmt->type) {
                case IRStmtIf:
                case IRStmtJmp:
                    break;
                default:
                    pipe->stmts.push_back(stmt.get());
                    stmt->pipe = pipe;
            }
        }
    }

    return true;
}

// DFS usd to extract slice.
void DoExtractSlice(IRStmt* stmt,
                    set<IRStmt*>* seen,
                    vector<IRStmt*>* postorder) {
    seen->insert(stmt);
    for (auto* arg : stmt->args) {
        if (seen->find(arg) != seen->end()) continue;
        DoExtractSlice(arg, seen, postorder);
    }
    postorder->push_back(stmt);
}

// Clones the backward slice of the 'kill_if' op given and returns all stmts.
// They can then be placed in a downstream stage of this pipe to produce a kill
// signal.
IRStmt* CloneKillIfSlice(IRProgram* program,
                         PipeSys* sys,
                         IRStmt* kill_if,
                         IRBB** out_bb,
                         ErrorCollector* coll) {
    // A 'kill_if' condition is meant to be a side-effectless condition that
    // can be evaluated repeatedly and can be placed arbitrarily in the pipe
    // without constraining other nodes. As such, it must only contain port
    // reads (not chan reads!) and expression statements. We enforce this as we
    // collect the backward slice by finding the transitive closure of
    // statement args.
    
    set<IRStmt*> seen;  // to avoid collecting a statement twice, if DAG (non-tree)
    vector<IRStmt*> stmts;
    // Run a simple DFS to extract the slice. stmts is filled with the
    // postorder result of the traversal. This is the order in which we want
    // to return the slice (*not* reverse postorder), since the edges we follow
    // are initially backward (edges from use to def).
    DoExtractSlice(kill_if, &seen, &stmts);
    
    // Check to make sure we have only port reads and expr nodes.
    for (auto* stmt : stmts) {
        if (stmt->type != IRStmtPortRead && stmt->type != IRStmtExpr &&
            stmt != kill_if) {
            coll->ReportError(stmt->location, ErrorCollector::ERROR,
                    strprintf("Statement %%%d is not a port read or expression "
                              "statement, hence is not allowed in the transitive "
                              "backward slice of kill_if node %%%d",
                              stmt->valnum, kill_if->valnum));
            return nullptr;
        }

    }
    
    // Clone: produce new valnums and build a mapping of old->new pointers
    map<IRStmt*, IRStmt*> replace_map;
    unique_ptr<IRBB> _cloned_bb(new IRBB());
    IRBB* cloned_bb = _cloned_bb.get();
    program->bbs.push_back(move(_cloned_bb));
    cloned_bb->label = strprintf("__cloned_kill_if_slice_%d",
                                 program->GetValnum());
    IRStmt* cloned_condition = nullptr;
    for (auto* stmt : stmts) {
        if (stmt == kill_if) {
            // The kill_if itself doesn't go in the cloned slice -- we just use
            // its condition.
            cloned_condition = stmt->args[0];
            continue;
        }
        unique_ptr<IRStmt> cloned_stmt(new IRStmt(*stmt));
        cloned_stmt->valnum = program->GetValnum();
        cloned_stmt->bb = cloned_bb;
        cloned_stmt->stage = nullptr;  // filled in by caller later.
        replace_map[stmt] = cloned_stmt.get();
        cloned_bb->stmts.push_back(move(cloned_stmt));
    }

    // Make a pass to replace args.
    for (auto& stmt : cloned_bb->stmts) {
        for (unsigned i = 0; i < stmt->args.size(); i++) {
            auto it = replace_map.find(stmt->args[i]);
            // We took the full backward slice, so all args should be part of
            // the cloned statement set. Thus this lookup should never fail.
            assert(it != replace_map.end());
            stmt->args[i] = it->second;
            stmt->arg_nums[i] = it->second->valnum;
        }
    }
    assert(replace_map.find(cloned_condition) != replace_map.end());
    cloned_condition = replace_map[cloned_condition];

    *out_bb = cloned_bb;
    return cloned_condition;
}

// Return all pipestages downstream of a kill_if. These are all stages in the
// kill_if's pipe and its child pipes (by the spawn tree).
vector<PipeStage*> KillIfDownstreamStages(IRStmt* kill_if_stmt) {
    vector<PipeStage*> ret;
    queue<Pipe*> q;  // BFS queue
    q.push(kill_if_stmt->pipe);
    while (!q.empty()) {
        Pipe* p = q.front();
        q.pop();
        for (auto* child : p->children) {
            q.push(child);
        }
        for (unsigned i = kill_if_stmt->stage->stage + 1;
             i < p->stages.size(); i++) {
            ret.push_back(p->stages[i].get());
        }
    }
    return ret;
}

bool PropagateKillIfDownstream(IRProgram* program,
                               PipeSys* sys,
                               IRStmt* kill_if_stmt,
                               ErrorCollector* coll) {
    // General strategy:
    // - We replicate the kill_if's monitored condition's logic to all
    //   downstream stages in its own pipe and in child pipes. The logic must
    //   have only port reads and expression nodes, i.e. no side-effects, so it
    //   is safe to replicate and evaluate anew in each stage.
    //       - Why downstream stages only, and only its own pipe and child pipes?
    //             - The semantics of a kill_if are that it follows
    //               control-flow, including across spawns.
    //             - Within its own stage, the valid_out of a kill_if goes
    //               false if the kill condition occurs, so everything naturally
    //               works out. Hence only worry about downstream stages.
    //             - To maintain the "continuous monitoring" semantics, though,
    //               we need to check once every time state changes, i.e., once
    //               per cycle, so we must insert clones in all downstream
    //               stages. Our cloned logic is qualified by (ANDed with) the
    //               valid_in of the original kill_if, staged downstream, so it
    //               can signal a kill only if control passed through the
    //               original kill_if, after which it is "attached" forever to
    //               the control flow.
    //            - But this is true only for the pipe itself and any pipes we
    //              spawned -- other unrelated pipes (ancestors or siblings) do
    //              not have the kill_if attached and should be left alone.)
    // - We generate the signal (original kill_if's valid_in) & (replicated
    //   kill_if condition's result) in each stage.
    // - We add this signal to a set of 'kill_if triggers' in the given pipestage.
    //   This set is mixed into the other kills that feed the stage-wide kill
    //   across the valid-signal cut in AssignKills() below.
    

    // Determine the set of pipes we propagate across: this pipe and any children.
    
    auto downstream_stages = KillIfDownstreamStages(kill_if_stmt);
    for (auto* stage : downstream_stages) {
        IRBB* cloned_bb;
        IRStmt* arg = CloneKillIfSlice(program, sys, kill_if_stmt, &cloned_bb, coll);
        for (auto& stmt : cloned_bb->stmts) {
            stmt->stage = stage;
            stmt->pipe = stage->pipe;
            stmt->valid_in = kill_if_stmt->valid_in;
            stage->pipe->stmts.push_back(stmt.get());
            stage->stmts.push_back(stmt.get());
        }

        // Add an AND: kill_if's valid_in & cloned kill_if arg.
        unique_ptr<IRStmt> kill_cond(new IRStmt());
        kill_cond->type = IRStmtExpr;
        kill_cond->op = IRStmtOpAnd;
        kill_cond->valnum = program->GetValnum();
        kill_cond->bb = cloned_bb;
        kill_cond->stage = stage;
        kill_cond->pipe = stage->pipe;

        kill_cond->args.push_back(arg);
        kill_cond->arg_nums.push_back(arg->valnum);
        kill_cond->args.push_back(kill_if_stmt->valid_in);
        kill_cond->arg_nums.push_back(kill_if_stmt->valid_in->valnum);

        stage->kills.push_back(kill_cond.get());

        stage->pipe->stmts.push_back(kill_cond.get());
        stage->stmts.push_back(kill_cond.get());
        cloned_bb->stmts.push_back(move(kill_cond));
    }

    return true;
}

// For each 'kill_if' op, leave the original but also clone the op and its
// backward slice to each stage downstream and add a 'kill' op per
// valid-crossing across the valid-cut.
bool InsertKillIfKills(IRProgram* program,
                       PipeSys* sys,
                       ErrorCollector* coll) {

    // Collect all kill_if statements.
    vector<IRStmt*> kill_if_stmts;
    for (auto& pipe : sys->pipes) {
        for (auto* stmt : pipe->stmts) {
            if (stmt->type == IRStmtKillIf) {
                kill_if_stmts.push_back(stmt);
            }
        }
    }

    // For each original kill_if, iterate down the pipe (i.e. along the
    // downstream valid-spines), inserting clones of the continuously-evaluated
    // kill condition at each new stage. Each clone of the kill_if DAG generates a
    // kill = valid_in & kill_condition, and this condition is added to a list
    // picked up by AssignKills below.
    for (auto* kill_if_stmt : kill_if_stmts) {
        // Trace the valid spine downstream, inserting new kills at each
        // pipestage crossing.
        if (!PropagateKillIfDownstream(program, sys, kill_if_stmt, coll)) {
            return false;
        }
    }

    return true;
}

// Creates a RestartValue / RestartValueSrc pair to port a signal across
// stages.
IRStmt* CreateNonStagedLink(IRProgram* prog, PipeStage* src_stage,
                            IRStmt* src_value, PipeStage* consumer_stage) {
    if (src_stage->stage == consumer_stage->stage) {
        return src_value;
    }

    IRStmt* ret;
    unique_ptr<IRStmt> restart_value(new IRStmt());
    restart_value->valnum = prog->GetValnum();
    restart_value->type = IRStmtRestartValue;
    restart_value->width = src_value->width;
    ret = restart_value.get();
    restart_value->pipe = consumer_stage->pipe;
    restart_value->stage = consumer_stage;
    restart_value->pipe->stmts.push_back(restart_value.get());
    restart_value->stage->stmts.push_back(restart_value.get());
    restart_value->stage->owned_stmts.push_back(move(restart_value));

    unique_ptr<IRStmt> restart_value_src(new IRStmt());
    ret->restart_arg = restart_value_src.get();
    restart_value_src->valnum = prog->GetValnum();
    restart_value_src->type = IRStmtRestartValueSrc;
    restart_value_src->width = src_value->width;
    restart_value_src->args.push_back(src_value);
    restart_value_src->arg_nums.push_back(src_value->valnum);
    restart_value_src->pipe = src_stage->pipe;
    restart_value_src->stage = src_stage;
    restart_value_src->pipe->stmts.push_back(restart_value_src.get());
    restart_value_src->stage->stmts.push_back(restart_value_src.get());
    restart_value_src->stage->owned_stmts.push_back(move(restart_value_src));


    return ret;
}

bool ConvertSingleWrites(IRProgram* program,
                         PipeSys* sys,
                         ErrorCollector* coll) {

    struct WrittenObj {
        int stage;  // stage for all ops not dom'd by a killyounger.
        int earliest_killstage; // earliest stage of op dom'd by a killyounger.
        IRPort* port;
        IRStorage* storage;
        vector<IRStmt*> stmts;

        PipeStage* write_stage;
        IRStmt* one_write;

        WrittenObj()
            : stage(-1), earliest_killstage(-1),
              port(nullptr), storage(nullptr),
              write_stage(nullptr)
        {}
    };

    map<void*, WrittenObj> written_objs;

    for (auto& pipe : sys->pipes) {
        for (auto* stmt : pipe->stmts) {

            // We consider port, chan, and reg writes (N.B.: *not* array
            // writes; those imply a separate write port for each write
            // IRStmt).
            void* written_obj = nullptr;
            const char* name = "";
            IRPort* port = nullptr;
            IRStorage* storage = nullptr;
            switch (stmt->type) {
                case IRStmtPortWrite:
                case IRStmtChanWrite:
                    written_obj = stmt->port;
                    name = stmt->port_name.c_str();
                    break;
                case IRStmtRegWrite:
                    written_obj = stmt->storage;
                    name = stmt->port_name.c_str();
                    break;
                default:
                    break;
            }

            if (written_obj) {
                auto it = written_objs.find(written_obj);
                // Do we already have a record of this written object? If so,
                // compare stage number (verify it's the same as other writers)
                // and add ourselves to the list of writers.
                if (it != written_objs.end()) {
                    if (!stmt->dom_killyounger ||
                         stmt->dom_killyounger->stage->stage != stmt->stage->stage) {
                        if (it->second.stage != -1 &&
                            stmt->stage->stage != it->second.stage) {
                            coll->ReportError(stmt->location, ErrorCollector::ERROR,
                                    strprintf(
                                        "Write to port/chan/reg %s occurs in stage "
                                        "%d and %d. All writes must be in one stage.",
                                        name,
                                        stmt->stage->stage, it->second.stage));
                            return false;
                        } else if (it->second.stage == -1) {
                            it->second.stage = stmt->stage->stage;
                            it->second.write_stage = stmt->stage;
                            it->second.one_write = stmt;
                        }
                    } else {
                        if (it->second.earliest_killstage == -1) {
                            it->second.earliest_killstage = stmt->stage->stage;
                            if (!it->second.write_stage) {
                                it->second.write_stage = stmt->stage;
                                it->second.one_write = stmt;
                            }
                        } else if (stmt->stage->stage < it->second.earliest_killstage) {
                            it->second.earliest_killstage = stmt->stage->stage;
                            if (!it->second.write_stage) {
                                it->second.write_stage = stmt->stage;
                                it->second.one_write = stmt;
                            }
                        }
                    }
                    it->second.stmts.push_back(stmt);
                } else {
                    WrittenObj record;
                    if (stmt->dom_killyounger &&
                        stmt->dom_killyounger->stage->stage == stmt->stage->stage) {
                        record.earliest_killstage = stmt->stage->stage;
                    } else {
                        record.stage = stmt->stage->stage;
                    }
                    record.write_stage = stmt->stage;
                    record.one_write = stmt;
                    record.port = port;
                    record.storage = storage;
                    record.stmts.push_back(stmt);
                    written_objs.insert(make_pair(written_obj, record));
                }
            }
        }
    }

    // Check that any writes dom'd by a killyounger occur in the same or later
    // stage as any writes not dom'd by a killyounger. The rationale here is
    // that we allow such writes (call them "killing writes") to come in later
    // stages because they will kill the valids on all writes in the nominal
    // write stage. For example, a reg may normally be written in stage 2, but
    // a killyounger/restart late in the pipe (stage 10, say) resets its value.
    // These killing writes are also exempt from the predicate-overlap check.
    // However, we *don't* need to treat them specially during the
    // written-value priority / selection logic, because (as mentioned) these
    // killing writes kill valids on all the writes in the nominal stage, and
    // kill the killing writes in earlier stages as well. (If there's more than
    // one killing write in the same stage that activates at the same time,
    // that's undefined behavior. TODO: maybe we should check that killyoungers
    // in the same stage after lowering don't have overlapping predicates.)
    for (auto& p : written_objs) {
        if (p.second.earliest_killstage != -1 &&
            p.second.stage != -1 &&
            p.second.earliest_killstage < p.second.stage) {
            coll->ReportError(p.second.stmts[0]->location, ErrorCollector::ERROR,
                    "The earliest write to port/chan/reg that is a 'killing write' "
                    "(a write dominated by a killyounger in the same stage) comes "
                    "before the nominal write stage (stage where all non-killing "
                    "writes occur) for this port/chan/reg. This is an error because "
                    "killing writes override the nominally-written value but this "
                    "killing write does not kill the ordinary writes.");
            return false;
        }
    }

    // For each written object, ensure that we got all defs, i.e. that there are
    // none in other pipe systems (processes).
    for (auto& p : written_objs) {
        if (p.second.port) {
            for (auto* def : p.second.port->defs) {
                if (def->pipe->sys != sys) {
                    coll->ReportError(def->location, ErrorCollector::ERROR,
                            "Write to port/chan appears in different "
                            "process than some other writer. All writers "
                            "must be located in the same top-level process "
                            "(entry function).");
                    return false;
                }
            }
        }
        if (p.second.storage) {
            for (auto* writer : p.second.storage->writers) {
                if (writer->pipe->sys != sys) {
                    coll->ReportError(writer->location, ErrorCollector::ERROR,
                            "Write to reg appears in different "
                            "process than some other writer. All writers "
                            "must be located in the same top-level process "
                            "(entry function).");
                    return false;
                }
            }
        }
    }

    // For each written object, ensure that predicates do not overlap (this is
    // equivalent to saying that no write dominates another write, i.e., that
    // there is only one write per path).
    for (auto& p : written_objs) {
        for (auto* stmt : p.second.stmts) {
            for (auto* other : p.second.stmts) {
                if (stmt == other) continue;
                if ((stmt->dom_killyounger &&
                     stmt->dom_killyounger->stage->stage == stmt->stage->stage) ||
                    (other->dom_killyounger &&
                     other->dom_killyounger->stage->stage == other->stage->stage))
                    continue;
                if (!stmt->valid_in_pred.AndWith(
                            other->valid_in_pred).IsFalse()) {
                    coll->ReportError(other->location, ErrorCollector::ERROR,
                            "Two writes to one port/chan/reg have overlapping "
                            "predicates. Only one write may occur on a given "
                            "path.");
                    return false;
                }
            }
        }
    }

    // Now, for each written object with more than one writer, generate a
    // sequence of select operations and rewrite the source of the write.
    for (auto& p : written_objs) {
        if (p.second.stmts.size() < 2) continue;
        PipeStage* write_stage = p.second.write_stage;
        // Wire up the sels and valid-signal combining ORs.
        IRStmt* last_sel = nullptr;
        IRStmt* last_or = nullptr;
        for (unsigned i = 0; i < p.second.stmts.size() - 1; i++) {
            unique_ptr<IRStmt> sel(new IRStmt());
            sel->valnum = program->GetValnum();
            sel->type = IRStmtExpr;
            sel->op = IRStmtOpSelect;
            sel->pipe = write_stage->pipe;
            sel->stage = write_stage;

            sel->args.push_back(CreateNonStagedLink(program,
                        p.second.stmts[i+1]->stage,
                        p.second.stmts[i+1]->valid_in, sel->stage));
            sel->arg_nums.push_back(sel->args.back()->valnum);

            sel->args.push_back(CreateNonStagedLink(program,
                        p.second.stmts[i+1]->stage,
                        p.second.stmts[i+1]->args[0], sel->stage));
            sel->arg_nums.push_back(sel->args.back()->valnum);
            sel->args.push_back(last_sel ? last_sel :
                    CreateNonStagedLink(program,
                        p.second.stmts[i]->stage,
                        p.second.stmts[i]->args[0], sel->stage));
            sel->arg_nums.push_back(sel->args.back()->valnum);
            sel->width = sel->args[1]->width;
            last_sel = sel.get();

            sel->pipe->stmts.push_back(sel.get());
            sel->stage->stmts.push_back(sel.get());
            sel->stage->owned_stmts.push_back(move(sel));

            unique_ptr<IRStmt> valid_or(new IRStmt());
            valid_or->valnum = program->GetValnum();
            valid_or->type = IRStmtExpr;
            valid_or->op = IRStmtOpOr;
            valid_or->pipe = write_stage->pipe;
            valid_or->stage = write_stage;
            valid_or->args.push_back(last_or ? last_or :
                    CreateNonStagedLink(program,
                        p.second.stmts[i]->stage,
                        p.second.stmts[i]->valid_in, valid_or->stage));
            valid_or->arg_nums.push_back(valid_or->args.back()->valnum);
            valid_or->args.push_back(
                    CreateNonStagedLink(program,
                        p.second.stmts[i+1]->stage,
                        p.second.stmts[i+1]->valid_in, valid_or->stage));
            valid_or->arg_nums.push_back(valid_or->args.back()->valnum);
            valid_or->width = 1;
            last_or = valid_or.get();

            valid_or->pipe->stmts.push_back(valid_or.get());
            valid_or->stage->stmts.push_back(valid_or.get());
            valid_or->stage->owned_stmts.push_back(move(valid_or));
        }

        // Rewrite the first write's arg, and replace its 'valid_in' with the
        // OR of all.
        IRStmt* one_write = p.second.one_write;
        one_write->valid_in = last_or;
        one_write->args[0] = last_sel;
        one_write->arg_nums[0] = last_sel->valnum;

        // Delete all of the other writes.
        for (auto* stmt : p.second.stmts) {
            if (stmt != one_write) {
                stmt->deleted = true;
            }
        }

        if (one_write->port) {
            one_write->port->defs.clear();
            one_write->port->defs.push_back(one_write);
        } else if (one_write->storage) {
            one_write->storage->writers.clear();
            one_write->storage->writers.push_back(one_write);
        }
    }

    return true;
}

// Assigns 'stall' signals to pipestages: each stage stalls if any later
// 'backedge' evaluates. We simply take the OR of all backedge predicates.
// Note that the predicates from later stage backedges must be marked such that
// they do *not* constrain stage scheduling, and do *not* get staged across
// pipestage boundaries: they are "cross-stage" signals.
bool AssignStalls(IRProgram* program,
                  PipeSys* sys,
                  Pipe* pipe,
                  ErrorCollector* coll) {

    // Stage 0 must be empty -- we use it to hold the stall logic for stage 1,
    // if present, but there is no stage in which stage 0's stall logic could
    // go.
    if (pipe->stages.size() > 0) {
        for (auto* stmt : pipe->stages[0]->stmts) {
            printf("%s\n", stmt->ToString().c_str());
        }
        assert(pipe->stages[0]->stmts.empty());
    }

    // For each stage, find all backedge ops in later stages, generate an OR
    // tree in the prior stage and set it as the stall input for the latches
    // prior to this stage.
    for (unsigned i = 1; i < pipe->stages.size(); i++) {
        auto* stage = pipe->stages[i].get();

        // Collect valids for all backedges with *targets* later than this
        // stage in *all* pipes.
        vector<IRStmt*> later_backedge_valids;
        for (auto& other_pipe : sys->pipes) {
            for (unsigned j = i + 1; j < other_pipe->stages.size(); j++) {
                auto* later_stage = other_pipe->stages[j].get();
                for (auto* stmt : later_stage->stmts) {
                    if (stmt->type == IRStmtBackedge &&
                        stmt->restart_target->restart_cond->stage->stage > i) {
                        assert(stmt->valid_in != nullptr);
                        later_backedge_valids.push_back(
                                CreateNonStagedLink(
                                    program, later_stage,
                                    stmt->valid_in, stage));
                    }
                }
            }
        }

        // If no backedges past this point, then no stall signal.
        if (later_backedge_valids.empty()) {
            // We're actually done completely, since later stages *also* will
            // have no downstream backedges creating stalls.
            break;
        }

        // Generate an OR-tree across all stall signals and use its stall
        // output.
        unique_ptr<IRBB> stallgen_bb(new IRBB());
        stallgen_bb->label = strprintf("__stallgen_stage_%d", i);
        IRBBBuilder builder(program, stallgen_bb.get());
        stage->stall = builder.BuildTree(IRStmtOpOr, later_backedge_valids);
        builder.PrependToBB();
        
        // Find the prior stage in which to insert the OR-tree, and put it
        // there.
        auto* prior_stage = pipe->stages[i-1].get();
        for (auto& stmt : stallgen_bb->stmts) {
            prior_stage->stmts.push_back(stmt.get());
            stmt->stage = prior_stage;
            stmt->stage->pipe->stmts.push_back(stmt.get());
        }
        pipe->bbs.push_back(stallgen_bb.get());
        program->bbs.push_back(move(stallgen_bb));
    }

    return true;
}

bool AssignKills(IRProgram* program,
                 PipeSys* sys,
                 Pipe* pipe,
                 ErrorCollector* coll) {
    // For each pipe stage:
    //   - Compute the kill for this pipe stage: the OR of (i) all later
    //     killyoungers, and (ii) all later backedges. Build the OR-tree for
    //     this.
    //   - Find all valids entering this pipestage, i.e. 'valid_in' signals on
    //     statements that come from statements in earlier stages, and any input
    //     on valid_spine ops (these are gates generated by predication that carry
    //     valid pulses down the pipeline).
    //   - For each such signal, insert an AND gate to qualify (gate) the
    //     incoming valid with its other input the inverse of the kill.
    
    // Start at stage 1; stage 0 is empty and has no kills.
    for (unsigned i = 1; i < pipe->stages.size(); i++) {
        auto* stage = pipe->stages[i].get();

        // Pick up any signals that kill this stage.
        vector<IRStmt*> kill_inputs;
        // Any later killyounger kills this stage.
        for (auto& other_pipe : sys->pipes) {
            for (unsigned j = i + 1; j < other_pipe->stages.size(); j++) {
                auto* later_stage = other_pipe->stages[j].get();
                for (auto* stmt : later_stage->stmts) {
                    if (stmt->type == IRStmtKillYounger) {
                        kill_inputs.push_back(CreateNonStagedLink(program,
                                    later_stage, stmt->valid_in, stage));
                    }
                }
            }
        }
        // Add additional kills (e.g. from kill_if).
        for (auto* kill : stage->kills) {
            kill_inputs.push_back(kill);
        }
        
        // Generate an OR-tree across all kill inputs if we had any.
        IRStmt* kill_signal = nullptr;
        if (kill_inputs.size() > 0) {
            unique_ptr<IRBB> killgen_bb(new IRBB());
            killgen_bb->label = strprintf("__killgen_stage_%d", i);
            IRBBBuilder builder(program, killgen_bb.get());
            kill_signal = builder.BuildTree(IRStmtOpOr, kill_inputs);
            
            // Find the prior stage in which to insert the OR-tree, and put it
            // there.
            auto* prior_stage = pipe->stages[i-1].get();
            for (auto& stmt : killgen_bb->stmts) {
                prior_stage->stmts.push_back(stmt.get());
                stmt->stage = prior_stage;
            }
            pipe->bbs.push_back(killgen_bb.get());
            program->bbs.push_back(move(killgen_bb));
        }

        // The kill signal for this stage is the OR of its killyounger-derived
        // kill (above), any downstream kill_if clones, and its stall signal.
        // The reason for the latter is that if the stage is stalled, its
        // internal logic must be prevented from invoking its side-effects; in
        // such a case, the stage's output latches will hold the prior output.
        if (stage->stall) {
            // insert an OR, if necessary.
            if (kill_signal != nullptr) {
                vector<IRStmt*> final_kill_inputs = { stage->stall, kill_signal };
                unique_ptr<IRBB> kill_or_bb(new IRBB());
                kill_or_bb->label = strprintf("__kill_or_stage_%d", i);
                IRBBBuilder builder(program, kill_or_bb.get());
                kill_signal = builder.BuildTree(IRStmtOpOr, final_kill_inputs);

                for (auto& stmt : kill_or_bb->stmts) {
                    stmt->stage = stage;
                    stage->stmts.push_back(stmt.get());
                    stage->pipe->stmts.push_back(stmt.get());
                }

                pipe->bbs.push_back(kill_or_bb.get());
                program->bbs.push_back(move(kill_or_bb));
            } else {
                kill_signal = stage->stall;
            }
        }

        if (kill_signal != nullptr) {
            // Now find the valid-cut across inputs to this stage: all valid_ins
            // that come from prior stages. We will insert ANDs to gate each of
            // these valids and a map of substitutions, then make a second pass to
            // substitute all uses of the 'valid' signals when they are either
            // valid_ins on any stmt, or when they are ordinary args on
            // valid_spine stmts.
            set<IRStmt*> valid_cut;
            for (auto* stmt : stage->stmts) {
                if (stmt->valid_in && stmt->valid_in->stage->stage < i) {
                    valid_cut.insert(stmt->valid_in);
                } else if (stmt->is_valid_start) {
                    valid_cut.insert(stmt);
                }
                if (stmt->valid_spine) {
                    for (auto* arg : stmt->args) {
                        if (arg->stage->stage < i) {
                            valid_cut.insert(arg);
                        }
                    }
                }
            }
            // TODO: abstract out this "create a new BB" pattern.
            map<IRStmt*, IRStmt*> valid_replacements;
            unique_ptr<IRBB> valid_cut_gating_bb(new IRBB());
            valid_cut_gating_bb->label = strprintf("__valid_cut_gating_stage_%d", i);
            IRBBBuilder builder(program, valid_cut_gating_bb.get());
            IRStmt* not_kill = builder.AddStmt(unique_ptr<IRStmt>(new IRStmt()));
            not_kill->type = IRStmtExpr;
            not_kill->op = IRStmtOpNot;
            not_kill->width = 1;
            not_kill->valnum = program->GetValnum();
            not_kill->bb = valid_cut_gating_bb.get();
            not_kill->args.push_back(kill_signal);
            not_kill->arg_nums.push_back(kill_signal->valnum);

            for (auto* stmt : valid_cut) {
                vector<IRStmt*> and_args = { not_kill, stmt };
                IRStmt* gated = builder.AddExpr(IRStmtOpAnd, and_args);
                valid_replacements[stmt] = gated;
            }

            for (auto* stmt : stage->stmts) {
                if (stmt->valid_in) {
                    auto it = valid_replacements.find(stmt->valid_in);
                    if (it != valid_replacements.end()) {
                        stmt->valid_in = it->second;
                    }
                }
                if (stmt->valid_spine) {
                    for (int i = 0; i < stmt->args.size(); i++) {
                        auto it = valid_replacements.find(stmt->args[i]);
                        if (it != valid_replacements.end()) {
                            stmt->args[i] = it->second;
                        }
                    }
                }
            }

            builder.PrependToBB();
            for (auto& stmt : valid_cut_gating_bb->stmts) {
                stmt->stage = stage;
                stage->stmts.push_back(stmt.get());
                stage->pipe->stmts.push_back(stmt.get());
            }
            program->bbs.push_back(move(valid_cut_gating_bb));
        }
    }

    return true;
}

}  // anonymous namespace

vector<unique_ptr<PipeSys>> IRProgram::Lower(ErrorCollector* coll) {

    // Extract pipelines from the spawn forest (set of spawn trees). Each BB is
    // extracted to at most one pipe, because each pipeline can only be spawned
    // from one point.
    vector<unique_ptr<PipeSys>> pipesystems;
    bool had_error = false;
    for (auto* entry : entries) {
        unique_ptr<PipeSys> sys(new PipeSys());
        sys->program = this;
        if (!FindPipes(this, sys.get(), entry, coll)) {
            had_error = true;
            break;
        }
        pipesystems.push_back(move(sys));
    }
    if (had_error) {
        pipesystems.clear();
        return pipesystems;
    }

    // For each PipeSys, perform pipe conversion and predication.

    unique_ptr<TimingModel> timing_model =
        TimingModel::New(this->timing_model);

    PipeTimer timer(timing_model.get());

    for (auto& sys : pipesystems) {
        // Check that chans are only used within their own extracted pipes. This is
        // really a typecheck-like pass, but cannot be run until the spawn-tree
        // pipe extraction runs.
        if (!CheckChanUses(this, sys.get(), coll)) goto err;

        for (auto& pipe : sys->pipes) {
            // Compute killyounger dominance over all points in the CFG. This is
            // used during backedge conversion to decide how to constrain stages.
            if (!ComputeKillyoungerDom(this, sys.get(), pipe.get(), coll)) goto err;
            // Break backedges into backedge BB / restart BB pairs with no CFG
            // linkage, so that the CFG becomes a DAG with restart points.
            if (!ConvertBackedges(this, sys.get(), pipe.get(), coll)) goto err;
            // Build a 'valid'-signal spine along each path in the CFG, and assign
            // valid predicates to all statements.
            if (!IfConvert(this, sys.get(), pipe.get(), coll)) goto err;
            // Build the dependence DAG according to side effects, used as a part
            // of the partial order that constrains pipe staging.
            if (!BuildPipeDAG(this, sys.get(), pipe.get(), coll)) goto err;
            // Flatten the BBs into a list of statements.
            if (!FlattenPipe(this, sys.get(), pipe.get(), coll)) goto err;
        }

        // Once all pipes have been flattened to lists of statements with
        // partial-order DAGs, we can segment statements into pipe stages according
        // to a model of node delays.
        if (!timer.TimePipe(sys.get(), coll)) goto err;

        // We clone 'kill_if' backward slices downstream to each stage.
        if (!InsertKillIfKills(this, sys.get(), coll)) goto err;

        // We check that ports, chans, and regs have all write-points in only
        // one stage. Then we convert the multiple writes to a single write
        // with a muxed input. (We check that valids do not overlap.)
        if (!ConvertSingleWrites(this, sys.get(), coll)) goto err;

        for (auto& pipe : sys->pipes) {

            // TODO: check here for 'can only be killed if killyounger' markers
            // and error out if so.

            // We assign stall signals *after* pipelining because the
            // signals depend on the pipestage assignments.
            if (!AssignStalls(this, sys.get(), pipe.get(), coll)) goto err;

            // Likewise, we assign kill signals *after* pipelining because their inputs
            // depend on the pipestage assignments. We shoehorn in ANDs on valids that
            // occur at the start of pipestages (i.e., valid_ins on stmts whose
            // producer statements occur in previous pipestages, or which are NULL).
            // TODO: implement this. Need to add RestartValueSrc / RestartValue pair
            // for each killyounger, and then ensure that codegen uses the result of
            // RestartValue *directly*, with no staging aside from the latch between
            // RestartValueSrc and RestartValue.
            if (!AssignKills(this, sys.get(), pipe.get(), coll)) goto err;
        }

        continue;
err:
        had_error = true;
        break;
    }

    if (had_error) {
        pipesystems.clear();
    }
    return pipesystems;
}
