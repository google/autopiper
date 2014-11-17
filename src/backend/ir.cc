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

#include "backend/ir.h"

#include <set>
#include <sstream>

using namespace autopiper;
using namespace std;

const IRStmt* IRBB::SuccStmt() const {
    for (auto& stmt : stmts) {
        if (stmt->type == IRStmtJmp || stmt->type == IRStmtIf)
            return stmt.get();
    }
    return NULL;
}

vector<IRBB*> IRBB::Succs() const {
    const IRStmt* succ_stmt = SuccStmt();
    if (succ_stmt == NULL) {
        return std::vector<IRBB*>();
    } else {
        return succ_stmt->targets;
    }
}

int IRBB::WhichSucc(const IRBB* succ) const {
    vector<IRBB*> succs = Succs();
    for (unsigned i = 0; i < succs.size(); i++) {
        if (succs[i] == succ) return i;
    }
    return -1;
}

// Requires a crosslinked IRProgram.
vector<const IRBB*> IRProgram::Roots() const {
    vector<const IRBB*> ret;
    // Include all top-level entry points.
    for (auto* entry : entries) {
        ret.push_back(entry);
    }
    // Find spawn-points.
    set<const IRBB*> already_found;
    for (auto& bb : bbs) {
        for (auto& stmt : bb->stmts) {
            if (stmt->type != IRStmtSpawn) continue;
            for (auto* target : stmt->targets) {
                if (already_found.find(target) != already_found.end())
                    continue;
                already_found.insert(target);
                ret.push_back(target);
            }
        }
    }
    return ret;
}

IRTimeVar* IRProgram::GetTimeVar() {
    std::unique_ptr<IRTimeVar> new_var(new IRTimeVar());
    IRTimeVar* ret = new_var.get();
    ostringstream os;
    os << "__timevar__" << next_anon_timevar++;
    ret->name = os.str();
    timevar_map.insert(std::make_pair(ret->name, ret));
    timevars.push_back(std::move(new_var));
    return ret;
}

string IRProgram::ToString() const {
    string s;
    bool first = true;
    for (auto& bb : bbs) {
        if (!first) s += "\n";
        first = false;
        s += bb->ToString();
    }
    return s;
}

string IRBB::ToString() const {
    string s;
    s += label + ":\n";
    for (auto& stmt : stmts) {
        s += stmt->ToString() + "\n";
    }
    return s;
}

string IRStmt::ToString() const {
    ostringstream os;

    os << '%' << valnum;
    if (width > 0) {
        os << '[' << width << ']';
    } else if (width == kIRStmtWidthTxnID) {
        os << "[txn]";
    }
    os << " = ";
    switch (type) {
        case IRStmtExpr:
            switch (op) {
                case IRStmtOpNone:
                    os << "none"; break;
                case IRStmtOpConst:
                    os << "const"; break;
                case IRStmtOpAdd:
                    os << "add"; break;
                case IRStmtOpSub:
                    os << "sub"; break;
                case IRStmtOpMul:
                    os << "mul"; break;
                case IRStmtOpDiv:
                    os << "div"; break;
                case IRStmtOpRem:
                    os << "rem"; break;
                case IRStmtOpAnd:
                    os << "and"; break;
                case IRStmtOpOr:
                    os << "or"; break;
                case IRStmtOpXor:
                    os << "xor"; break;
                case IRStmtOpNot:
                    os << "not"; break;
                case IRStmtOpLsh:
                    os << "lsh"; break;
                case IRStmtOpRsh:
                    os << "rsh"; break;
                case IRStmtOpBitslice:
                    os << "bsl"; break;
                case IRStmtOpConcat:
                    os << "cat"; break;
                case IRStmtOpSelect:
                    os << "sel"; break;
                case IRStmtOpCmpLT:
                    os << "cmplt"; break;
                case IRStmtOpCmpLE:
                    os << "cmple"; break;
                case IRStmtOpCmpEQ:
                    os << "cmpeq"; break;
                case IRStmtOpCmpNE:
                    os << "cmpne"; break;
                case IRStmtOpCmpGT:
                    os << "cmpgt"; break;
                case IRStmtOpCmpGE:
                    os << "cmpge"; break;
            }
            break;
        case IRStmtPhi:
            os << "phi"; break;
        case IRStmtIf:
            os << "if"; break;
        case IRStmtJmp:
            os << "jmp"; break;
        case IRStmtPortRead:
            os << "portread"; break;
        case IRStmtPortWrite:
            os << "portwrite"; break;
        case IRStmtChanRead:
            os << "chanread"; break;
        case IRStmtChanWrite:
            os << "chanwrite"; break;
        case IRStmtPortExport:
            os << "portexport"; break;
        case IRStmtRegRead:
            os << "regread"; break;
        case IRStmtRegWrite:
            os << "regwrite"; break;
        case IRStmtArrayRead:
            os << "arrayread"; break;
        case IRStmtArrayWrite:
            os << "arraywrite"; break;
        case IRStmtProvide:
            os << "provide"; break;
        case IRStmtUnprovide:
            os << "unprovide"; break;
        case IRStmtAsk:
            os << "ask"; break;
        case IRStmtSpawn:
            os << "spawn"; break;
        case IRStmtKill:
            os << "kill"; break;
        case IRStmtKillYounger:
            os << "killyounger"; break;
        case IRStmtDone:
            os << "done"; break;
        case IRStmtKillIf:
            os << "killif"; break;
        case IRStmtTimingBarrier:
            os << "timing_barrier"; break;
        case IRStmtBackedge:
            os << "backedge"; break;
        case IRStmtRestartValue:
            os << "restart_value"; break;
        case IRStmtRestartValueSrc:
            os << "restart_value_src"; break;
        case IRStmtNone:
            break;
    }
    os << ' ';

    bool first = true;

    // Special-case 'phi' to get interleaved BBname/valnum list.
    if (type == IRStmtPhi) {
        assert(arg_nums.size() == target_names.size());
        for (unsigned i = 0; i < arg_nums.size(); i++) {
            if (!first) os << ", ";
            first = false;
            os << target_names[i] << ", " << arg_nums[i];
        }
        return os.str();
    }

    if (port_name != "") {
        first = false;
        os << '"' << port_name << '"';
    }

    for (auto arg : arg_nums) {
        if (!first) os << ", ";
        first = false;
        os << '%' << arg;
    }

    for (auto t : target_names) {
        if (!first) os << ", ";
        first = false;
        os << t;
    }

    if (has_constant) {
        if (!first) os << ", ";
        first = false;
        os << constant;
    }

    if (valid_in) {
        os << " [valid_in = %" << valid_in->valnum << "]";
    }
    if (valid_out) {
        os << " [valid_out = %" << valid_out->valnum << "]";
    }

    if (restart_arg) {
        os << " [restart = %" << restart_arg->valnum << "]";
    }

    if (restart_target) {
        os << " [restart_target = " << restart_target->label << "]";
    }

    if (timevar) {
        os << " @[" << timevar->name << " + " << time_offset << "]";
    }

    if (pipedag_deps.size() > 0) {
        os << " pipedag:[";
        bool first = true;
        for (auto* dep : pipedag_deps) {
            if (!first) os << ",";
            first = false;
            os << "%" << dep->valnum;
        }
        os << "]";
    }

    return os.str();
}
