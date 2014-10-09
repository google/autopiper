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

#ifndef _AUTOPIPER_IR_H_
#define _AUTOPIPER_IR_H_

#include <vector>
#include <map>
#include <memory>
#include <sstream>
#include <boost/multiprecision/gmp.hpp>

#include "predicate.h"

namespace autopiper {

class ErrorCollector;  // compiler.h

typedef boost::multiprecision::mpz_int bignum;

struct IRProgram;
struct IRBB;
struct IRStmt;
struct IRPort;
struct IRTimeVar;
struct PipeSys;
struct Pipe;
struct PipeStage;

struct Location {
    std::string filename;
    int line, column;

    Location() {
        filename = "(none)";
        line = 0;
        column = 0;
    }

    std::string ToString() const {
        std::ostringstream os;
        os << filename << ":" << line << ":" << column;
        return os.str();
    }
};

struct IRProgram {
    IRProgram() {
        next_valnum = 1;
        next_anon_timevar = 1;
    }

    std::vector<std::unique_ptr<IRBB>> bbs;
    std::vector<std::unique_ptr<IRPort>> ports;
    std::vector<std::unique_ptr<IRTimeVar>> timevars;
    std::map<std::string, IRTimeVar*> timevar_map;

    std::vector<IRBB*> entries;  // top-level entry points

    int next_valnum;
    int next_anon_timevar;

    static std::unique_ptr<IRProgram> Parse(const std::string& filename,
                                            std::istream* in,
                                            ErrorCollector* collector);

    bool Crosslink(ErrorCollector* collector);
    bool Typecheck(ErrorCollector* collector);
    std::vector<std::unique_ptr<PipeSys>> Lower(ErrorCollector* collector);

    std::vector<const IRBB*> Roots() const;  // top-level entry and any spawn points

    int GetValnum() {
        return next_valnum++;
    }

    IRTimeVar* GetTimeVar();

    std::string ToString() const;
};

struct IRBB {
    IRBB() {
        pipe = NULL;
        is_entry = false;
        is_restart = false;
        in_valid = NULL;
        restart_pred_src = NULL;
    }

    std::string label;
    std::vector<std::unique_ptr<IRStmt>> stmts;

    bool is_entry;  // top-level entry point
    Pipe* pipe;  // filled in during lowering

    Location location;

    std::vector<IRBB*> Succs() const;
    const IRStmt* SuccStmt() const;
    int WhichSucc(const IRBB* succ) const;
    std::vector<IRBB*> succs;  // cached
    std::vector<bool> backedge;  // per succs entry: is it a backedge?
    Predicate<IRStmt*> in_pred;
    IRStmt* in_valid;
    std::vector<Predicate<IRStmt*>> out_preds;
    std::vector<IRStmt*> out_valids;

    // is this a restart header (creating during backedge processing)?
    bool is_restart;
    // pointer to restart 'valid' value in restart header.
    IRStmt* restart_cond;

    // pointer to restart value src for 'valid' value in backedge BB.
    IRStmt* restart_pred_src;

    std::string ToString() const;
};

enum IRStmtType {
    // "normal" operations: computations and block-terminating jumps
    IRStmtExpr,
    IRStmtPhi,
    IRStmtIf,
    IRStmtJmp,

    // port reads and writes (ports interconnect between different txn
    // spawn-trees and/or to the outside world)
    IRStmtPortRead,
    IRStmtPortWrite,

    // channel reads and writes (channels interconnect within the same txn
    // spawn-tree, and connect only between the same instance of a txn)
    IRStmtChanRead,
    IRStmtChanWrite,

    // Export a port as a top-level IO (this op is a semantic no-op in the
    // program flow and can be placed anywhere, but is seen during codegen;
    // idiomatic form is to place it at the place where the channel is
    // read or written).
    IRStmtPortExport,

    // state element reads and writes
    IRStmtRegRead,
    IRStmtRegWrite,
    IRStmtArrayRead,
    IRStmtArrayWrite,

    // forwarding: provide and ask
    IRStmtProvide,
    IRStmtUnprovide,
    IRStmtAsk,

    // transaction-related: spawn (returning ID), kill own txn, kill younger
    // txns in this pipe.
    IRStmtSpawn,
    IRStmtKill,  // kill self (does not necessarily kill younger, but pinpoint kills allowed only if no txn state)
    IRStmtKillYounger,  // kill younger, not self
    IRStmtDone,  // txn completes

    IRStmtKillIf,  // kill self if a condition (first arg) is met at any downstream point.

    // Constrain timing.
    IRStmtTimingBarrier,

    // Represent a backedge to a target. Cannot be parsed in; generated in
    // lowering. First arg is used to represent target.
    IRStmtBackedge,
    // Represent a backedge-induced restart value. Cannot be parsed in;
    // generated in lowering. Predicate is set appropriately for the backedge
    // pred. restart_arg points to the corresponding RestartValueSrc stmt (or
    // predicate, if this is a valid-signal start).
    IRStmtRestartValue,
    // Represent a backedge restart value source.
    IRStmtRestartValueSrc,

    IRStmtNone,
};

enum IRStmtOp {
    IRStmtOpNone,
    IRStmtOpConst,
    IRStmtOpAdd,
    IRStmtOpSub,
    IRStmtOpMul,
    IRStmtOpDiv,
    IRStmtOpRem,
    IRStmtOpAnd,
    IRStmtOpOr,
    IRStmtOpXor,
    IRStmtOpNot,
    IRStmtOpLsh,
    IRStmtOpRsh,
    IRStmtOpBitslice,
    IRStmtOpConcat,
    IRStmtOpSelect,
    IRStmtOpCmpLT,  // unsigned! (this and below inequalities)
    IRStmtOpCmpLE,
    IRStmtOpCmpEQ,
    IRStmtOpCmpNE,
    IRStmtOpCmpGT,
    IRStmtOpCmpGE,
};

static const int kIRStmtWidthTxnID = -2;

struct IRStmt {
    IRStmt() {
        valnum = -1;
        type = IRStmtNone;
        bb = NULL;
        port = NULL;
        dom_killyounger = NULL;
        timevar = NULL;
        restart_arg = NULL;
        restart_target = NULL;
        time_offset = 0;
        width = 0;
        has_constant = false;
        is_valid_start = false;
        valid_in = NULL;
        valid_out = NULL;
        valid_spine = false;
    }
    
    int valnum;
    IRStmtType type;
    IRStmtOp op;
    IRBB* bb;
    std::vector<IRStmt*> args;
    bignum constant;
    bool has_constant;
    std::vector<IRBB*> targets;
    IRPort* port;
    IRStmt* dom_killyounger;  // dominated by a killyounger?
    IRTimeVar* timevar;
    IRStmt* restart_arg;  // backward arg to IRStmtRestartValueSrc on an IRStmtRestartValue op.
    IRBB* restart_target;  // backward target to restart header on an IRStmtBackedge op.
    int time_offset;
    int width;

    // used only prior to crosslinking:
    std::vector<int> arg_nums;
    std::vector<std::string> target_names;
    std::string port_name;

    // Filled in during lowering/timing:
    Pipe* pipe;
    bool is_valid_start;
    Predicate<IRStmt*> valid_in_pred;
    Predicate<IRStmt*> valid_out_pred;
    IRStmt* valid_in;
    IRStmt* valid_out;
    // the result of this stmt is a 'valid' signal. Note that this *cannot* be used
    // for non-valid-signal-related logic, as valids are added after user logic is
    // parsed in; in other words, the valid_spine bit is sticky and propagates to
    // all users.
    bool valid_spine;
    std::vector<IRStmt*> pipedag_deps; // DAG of side-effecting ops
    PipeStage* stage;  // stage into which this op is placed

    // Auxiliary info
    std::string ToString() const;
    Location location;
};

// IRPort represents either a port or a chan.
struct IRPort {
    enum Type {
        PORT,
        CHAN,
    };

    IRPort() {
        width = 0;
        def = nullptr;
        type = PORT;
        exported = false;
    }

    std::string name;
    int width;
    Type type;
    bool exported;

    IRStmt* def;
    std::vector<IRStmt*> uses;
};

struct IRTimeVar {
    IRTimeVar() {
        basis = 0;
    }

    std::string name;
    int basis;  // relative to global time (stage)
    std::vector<IRStmt*> uses;
};

// Helpers
inline bool IRWritesPort(IRStmtType type) {
    return type == IRStmtPortWrite ||
           type == IRStmtChanWrite;
}
inline bool IRReadsPort(IRStmtType type) {
    return type == IRStmtPortRead ||
           type == IRStmtChanRead;
}

inline bool IRHasSideEffects(IRStmtType type) {
    switch (type) {
        case IRStmtExpr:
        case IRStmtPhi:
            return false;
        default:
            return true;
    }
}

// Used by both BBReversePostorder and BBDomTree
struct BBSuccFunc {
    std::vector<const IRBB*> operator()(const IRBB* bb) {
        auto s = bb->Succs();
        return std::vector<const IRBB*>(s.begin(), s.end());
    }
};

}  // namespace autopiper

#endif
