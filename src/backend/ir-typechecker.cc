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

#include "ir.h"
#include "compiler.h"
#include "util.h"
#include "domtree.h"

#include <map>
#include <vector>
#include <string>
#include <set>

using namespace std;
using namespace autopiper;

namespace {

bool DerivePortWidth(IRPort* port, ErrorCollector* collector) {
    int width = -1;  // -1: not yet determined

    // Pass 1: derive width.
    // Look for a write first -- if present, this defines the width.
    if (port->def) {
        width = port->def->args[0]->width;
    }
    // If no writes, pick a width from the first read (they must all have the
    // same width).
    if (width == -1 && port->uses.size() > 0) {
        width = port->uses[0]->width;
    }

    port->width = width;

    // Pass 2: check derived width against reads.
    for (auto* use : port->uses) {
        if (use->width != width) {
            collector->ReportError(use->location, ErrorCollector::ERROR,
                    strprintf("Port read width of %d does not match derived "
                              "width of %d on port '%s'",
                              use->width, width, port->name.c_str()));
            return false;
        }
    }

    // Check that the port def and uses are either consistently chan or port
    // ops. (If there is a def, it will have already set the type
    // appropriately.)
    for (auto* use : port->uses) {
        IRStmtType expected_read_type = IRStmtChanRead;
        switch (port->type) {
            case IRPort::CHAN: expected_read_type = IRStmtChanRead; break;
            case IRPort::PORT: expected_read_type = IRStmtPortRead; break;
            default: assert(false); break;
        }
        if (use->type != expected_read_type) {
            collector->ReportError(use->location, ErrorCollector::ERROR,
                    strprintf("Use %%%d on port '%s' is of wrong type (port/chan)",
                              use->valnum, port->name.c_str(),
                              use->type, expected_read_type));
            return false;
        }
    }

    // Check that the port is exported only if a port, not a chan.
    if (port->exported && port->type != IRPort::PORT) {
        Location empty_loc;
        collector->ReportError(port->def ? port->def->location :
                               port->uses.size() > 0 ? port->uses[0]->location :
                               empty_loc,
                               ErrorCollector::ERROR,
                               strprintf("Chan '%s' cannot be exported; only ports can be exported",
                                         port->name.c_str()));
    }
    
    return true;
}

bool DeriveStorageSize(IRStorage* storage, ErrorCollector* collector) {
    // There must be at least one writer.
    if (storage->writers.empty()) {
        Location loc;
        if (storage->readers.size() > 0) {
            loc = storage->readers[0]->location;
        }
        collector->ReportError(loc, ErrorCollector::ERROR,
                strprintf("Storage '%s' has no writers. At least "
                          "one writer required.",
                          storage->name.c_str()));
        return false;
    }

    // Determine width and elems by surveying writers.
    int index_width = -1, data_width = -1;
    for (auto* writer : storage->writers) {
        int stmt_index_width = (writer->type == IRStmtRegWrite) ?
            0 :                     // single reg: always 1 elem (2^0 == 1)
            writer->args[0]->width; // array: first arg is index
        int stmt_data_width = (writer->type == IRStmtRegWrite) ?
            writer->args[0]->width : // single reg: first arg is data
            writer->args[1]->width;  // array: second arg is data

        if (writer->type == IRStmtArrayWrite) {
            if (stmt_index_width <= 0) {
            collector->ReportError(writer->location, ErrorCollector::ERROR,
                    "Index width on array write must be greater than zero.");
            return false;
            }
            if (stmt_index_width >= 64) {
                collector->ReportError(writer->location, ErrorCollector::ERROR,
                        strprintf("Writer index-width %d for storage '%s' is "
                            "too large: maximum is 64 bits (2^64 entries).",
                            stmt_index_width, storage->name.c_str()));
                return false;
            }
        }

        if (index_width == -1) {
            index_width = stmt_index_width;
        } else if (index_width != stmt_index_width) {
            collector->ReportError(writer->location, ErrorCollector::ERROR,
                    strprintf("Writer index-width %d does not match previous %d",
                        stmt_index_width, index_width));
            return false;
        }

        if (data_width == -1) {
            if (stmt_data_width <= 0) {
                collector->ReportError(writer->location, ErrorCollector::ERROR,
                        strprintf("Writer data-width for storage '%s' "
                                  "must be greater than zero.", storage->name.c_str()));
                return false;
            }
            data_width = stmt_data_width;
        } else if (data_width != stmt_data_width) {
            collector->ReportError(writer->location, ErrorCollector::ERROR,
                    strprintf("Writer data-width %d does not match previous %d",
                        stmt_data_width, data_width));
        }
    }

    assert(index_width >= 0);
    assert(data_width >= 0);
    storage->index_width = index_width;
    storage->data_width = data_width;

    // If single reg, we cannot allow more than one writer.
    if (index_width == 0 && storage->writers.size() > 1) {
        collector->ReportError(storage->writers[1]->location, ErrorCollector::ERROR,
                strprintf("More than one writer for single register storage '%s'. "
                          "Only one writer allowed.", storage->name.c_str()));
        return false;
    }

    // Check that all readers match index and data width.
    for (auto* reader : storage->readers) {
        int stmt_index_width = (reader->type == IRStmtRegRead) ?
            0 :                     // single reg: always 1 elem (2^0 == 1)
            reader->args[0]->width; // array: first arg is index

        if (reader->type == IRStmtArrayRead && stmt_index_width <= 0) {
            collector->ReportError(reader->location, ErrorCollector::ERROR,
                    "Index width on array read must be greater than zero.");
            return false;
        }

        if (stmt_index_width != storage->index_width) {
            collector->ReportError(reader->location, ErrorCollector::ERROR,
                    "Index width does not match on storage reader.");
            return false;
        }
        if (reader->width != storage->data_width) {
            collector->ReportError(reader->location, ErrorCollector::ERROR,
                    "Data width does not match on storage reader.");
            return false;
        }
    }
    
    return true;
}

bool CheckStmtWidthMatchesArgs(IRStmt* stmt, ErrorCollector* collector) {
    for (auto* arg : stmt->args) {
        if (arg->width != stmt->width) {
            collector->ReportError(stmt->location, ErrorCollector::ERROR,
                    strprintf("Width of %d on argument %%%d does not match "
                              "width %d of this statement",
                              arg->width, arg->valnum, stmt->width));
            return false;
        }
    }
    return true;
}

bool CheckExactWidth(IRStmt* stmt, int width, ErrorCollector* collector) {
    if (stmt->width != width) {
        collector->ReportError(stmt->location, ErrorCollector::ERROR,
                strprintf("Statement must have width of %d",
                    width));
        return false;
    }
    return true;
}

bool CheckMinWidth(IRStmt* stmt, int minwidth, ErrorCollector* collector) {
    if (stmt->width < minwidth) {
        collector->ReportError(stmt->location, ErrorCollector::ERROR,
                strprintf("Statement must have width of at least %d",
                    minwidth));
        return false;
    }
    return true;
}

bool CheckStmtWidth(IRStmt* stmt, ErrorCollector* collector) {
    switch (stmt->type) {
        case IRStmtExpr:
            // txnIDs can't be carried through expressions.
            if (stmt->width == kIRStmtWidthTxnID) {
                collector->ReportError(stmt->location, ErrorCollector::ERROR,
                        "Cannot carry transaction ID through expression");
                return false;
            }
            switch (stmt->op) {
                case IRStmtOpNone:
                case IRStmtOpConst:
                case IRStmtOpAdd:
                case IRStmtOpSub:
                case IRStmtOpAnd:
                case IRStmtOpOr:
                case IRStmtOpXor:
                case IRStmtOpNot:
                    if (!CheckMinWidth(stmt, 1, collector)) return false;
                    if (!CheckStmtWidthMatchesArgs(stmt, collector)) return false;
                    break;
                case IRStmtOpLsh:
                case IRStmtOpRsh:
                    if (!CheckExactWidth(stmt,
                                stmt->args[0]->width,
                                collector)) return false;
                case IRStmtOpMul:
                    if (!CheckExactWidth(stmt,
                                stmt->args[0]->width +
                                stmt->args[1]->width,
                                collector)) return false;
                    break;
                case IRStmtOpDiv:
                    if (!CheckExactWidth(stmt,
                                stmt->args[0]->width -
                                stmt->args[1]->width,
                                collector)) return false;
                    break;
                case IRStmtOpRem:
                    if (!CheckExactWidth(stmt,
                                stmt->args[0]->width -
                                stmt->args[1]->width,
                                collector)) return false;
                    break;
                case IRStmtOpBitslice:
                    {
                        if (stmt->args[1]->op != IRStmtOpConst ||
                            stmt->args[2]->op != IRStmtOpConst) {
                            collector->ReportError(stmt->location, ErrorCollector::ERROR,
                                    "Second and third args must be constants");
                            return false;
                        }
                        bignum diff = stmt->args[1]->constant - stmt->args[2]->constant;
                        int width = static_cast<int>(diff);
                        if (width < 0) width = -width;
                        if (!CheckExactWidth(stmt, width, collector)) return false;
                    }
                    break;
                case IRStmtOpConcat:
                    {
                        int width = 0;
                        for (auto* arg : stmt->args) {
                            width += arg->width;
                        }
                        if (!CheckExactWidth(stmt, width, collector)) return false;
                    }
                    break;
                case IRStmtOpSelect:
                    {
                        if (stmt->args[1]->width != stmt->args[2]->width ||
                            stmt->args[1]->width != stmt->width) {
                            collector->ReportError(stmt->location, ErrorCollector::ERROR,
                                    "Select statement second and third arg widths must match statement width");
                            return false;
                        }
                        if (stmt->args[0]->width != 1) {
                            collector->ReportError(stmt->location, ErrorCollector::ERROR,
                                    "Select statement first arg width must be 1 (boolean)");
                            return false;
                        }
                    }
                    break;
                case IRStmtOpCmpLT:
                case IRStmtOpCmpLE:
                case IRStmtOpCmpEQ:
                case IRStmtOpCmpNE:
                case IRStmtOpCmpGT:
                case IRStmtOpCmpGE:
                    {
                        if (stmt->args[0]->width != stmt->args[1]->width) {
                            collector->ReportError(stmt->location, ErrorCollector::ERROR,
                                    "Compare statement first and second args must have same width");
                            return false;
                        }
                        if (!CheckExactWidth(stmt, 1, collector)) return false;
                    }
                    break;
            }
            break;
        case IRStmtPhi:
            if (!CheckStmtWidthMatchesArgs(stmt, collector)) return false;
            break;
        case IRStmtIf:
            if (stmt->args[0]->width != 1) {
                collector->ReportError(stmt->location, ErrorCollector::ERROR,
                        "If statement first arg width must be 1 (boolean)");
                return false;
            }
            break;
        case IRStmtJmp:
            // No value args, only BB target.
            break;

        case IRStmtPortRead:
        case IRStmtPortWrite:
        case IRStmtChanRead:
        case IRStmtChanWrite:
        case IRStmtPortExport:
        case IRStmtRegRead:
        case IRStmtRegWrite:
        case IRStmtArrayRead:
        case IRStmtArrayWrite:
            // Already checked.
            break;

        case IRStmtProvide:
        case IRStmtUnprovide:
        case IRStmtAsk:
            // TODO
            assert(false);
            break;

        case IRStmtSpawn:
            if (!CheckExactWidth(stmt, kIRStmtWidthTxnID, collector)) return false;
            break;
        case IRStmtKill:
        case IRStmtKillYounger:
            // No args.
            break;
        case IRStmtKillIf:
            // Must have one arg with width 1 (boolean). Further restrictions
            // on backward slice (only port reads and expression ops) are
            // checked much later, after pipelining, when the backward slice is
            // actually cloned into each pipestage.
            if (stmt->args.size() != 1 || stmt->args[0]->width != 1) {
                collector->ReportError(stmt->location, ErrorCollector::ERROR,
                        "killif statement must have one boolean argument.");
                return false;
            }
        default:
            break;
    }
    return true;
}

bool CheckStmt(IRStmt* stmt, ErrorCollector* collector) {
    // All statements must have width matches. The precise width match
    // requirements depend on the operation.
    if (!CheckStmtWidth(stmt, collector)) return false;
    return true;
}

bool CheckBB(IRBB* bb, ErrorCollector* collector) {
    // Must end with a valid terminator: if, jmp, or kill.
    if (bb->stmts.size() == 0) {
        collector->ReportError(bb->location, ErrorCollector::ERROR,
                "BB has no statements");
        return false;
    }
    IRStmt* last = bb->stmts.back().get();
    if (last->type != IRStmtIf && last->type != IRStmtJmp &&
        last->type != IRStmtKill && last->type != IRStmtDone) {
        collector->ReportError(bb->location, ErrorCollector::ERROR,
                "BB does not end with a valid terminator: if, jmp, kill, or done");
        return false;
    }
    if (last->type == IRStmtIf) {
        if (last->targets[0] == last->targets[1]) {
            collector->ReportError(last->location, ErrorCollector::ERROR,
                    "Conditional if-terminator has two identical targets");
            return false;
        }
    }
    return true;
}

// Validates that phi-node sources are from immediately preceding BBs, and
// that each phi node has an in-edge from every BB that precedes it.
// Also validates that every value use is dominated by its def.
bool CheckPhis(IRProgram* program, ErrorCollector* collector) {
    // Find all predecessors for each BB.
    map<IRBB*, set<IRBB*>> preds;
    for (auto& bb : program->bbs) {
        for (auto* succ : bb->Succs()) {
            preds[succ].insert(bb.get());
        }
    }
    // Compute the domtree.
    BBDomTree domtree;
    domtree.Compute(program->Roots());

    // Check that each phi-node has the required inputs.
    for (auto& bb : program->bbs) {
        for (auto& stmt : bb->stmts) {
            if (stmt->type != IRStmtPhi) continue;
            assert(stmt->targets.size() == stmt->args.size());

            set<IRBB*> expected_inputs(preds[bb.get()].begin(),
                                       preds[bb.get()].end());
            if (expected_inputs.size() != stmt->args.size()) {
                collector->ReportError(
                    stmt->location,
                    ErrorCollector::ERROR,
                    "Phi-node has wrong number of arguments: must have "
                    "one bb, %value pair for every predecessor BB.");
                return false;
            }

            for (unsigned i = 0; i < stmt->args.size(); i++) {
                IRStmt* input_value = stmt->args[i];
                IRBB* input_block = stmt->targets[i];
                // Input must be seen exactly once.
                if (expected_inputs.find(input_block) ==
                    expected_inputs.end()) {
                    collector->ReportError(
                        stmt->location,
                        ErrorCollector::ERROR,
                        "Phi-node input either duplicated or specified "
                        "to non-predecessor BB");
                    return false;
                }
                // Input's defining BB must dominate in-edge predecessor
                // BB (either trivially, i.e. same block, or further up the
                // domtree).
                if (!domtree.Dom(input_value->bb, input_block)) {
                    collector->ReportError(
                        stmt->location,
                        ErrorCollector::ERROR,
                        strprintf("Defining block '%s' of phi-node input "
                                  "%%%d does not dominate in-edge predecessor '%s'",
                                  input_value->bb->label.c_str(),
                                  input_value->valnum,
                                  input_block->label.c_str()));
                    return false;
                }
            }
        }
    }

    // Check each use to ensure it's dominated by its def (except in a phi node).
    for (auto& bb : program->bbs) {
        for (auto& stmt : bb->stmts) {
            if (stmt->type == IRStmtPhi) continue;
            for (auto* arg : stmt->args) {
                if (!domtree.Dom(arg->bb, bb.get())) {
                    collector->ReportError(
                        stmt->location,
                        ErrorCollector::ERROR,
                        strprintf("Arg '%%%d' is defined in BB '%s' but "
                                  "this BB does not dominate current BB '%s'. "
                                  "Invalid SSA form.",
                                  arg->valnum, arg->bb->label.c_str(),
                                  bb->label.c_str()));
                    return false;
                }
            }
        }
    }

    return true;
}

// Validates that each IRStmt arg comes from a dominating BB.

}  // anonymous namespace

bool IRProgram::Typecheck(ErrorCollector* collector) {
    if (entries.size() == 0) {
        Location empty_loc;
        collector->ReportError(empty_loc, ErrorCollector::ERROR,
                "No entry points defined. Use 'entry' token on BB labels "
                "to define one or more entry points.");
        return false;
    }
    for (auto& port : ports) {
        if (!DerivePortWidth(port.get(), collector)) return false;
    }
    for (auto& s : storage) {
        if (!DeriveStorageSize(s.get(), collector)) return false;
    }
    for (auto& bb : bbs) {
        if (!CheckBB(bb.get(), collector)) return false;
        for (auto& stmt : bb->stmts) {
            if (!CheckStmt(stmt.get(), collector)) return false;
        }
    }
    if (!CheckPhis(this, collector)) return false;
    return true;
}
