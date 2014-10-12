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

#include "gen-verilog.h"
#include "ir.h"
#include "pipe.h"
#include "util.h"

#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace autopiper;
using namespace std;

namespace {
string HexStrAllOnes(int width) {
    ostringstream os;
    if (width % 4 != 0) {
        os << dec << ((1 << (width % 4)) - 1);
    }
    for (int i = 0; i < width/4; i++) {
        os << "f";
    }
    return os.str();
}
}  // anonymous namespace

// Strategy:
//
// Generated code has a single module. The inputs and outputs are all ports
// (for now! -- marking ports as 'external' TBD).
//
// All ops generate combinational logic.
//
// Pipestage boundaries generate instances of a generic 'pipereg' module. We
// have helpers that carry signals from generation point to required pipestage
// and create piperegs along the way.
//
// By the time we reach this point, each pipestage should have 'stall' and
// 'kill' signals.

void VerilogGenerator::Generate() {
    PrinterScope global_scope(out_);
    out_->SetVar("module_name", name_);

    GenerateModuleStart();
    // Generate node implementations first. In the process, we learn which
    // signals need to be staged to which pipestages.
    for (auto* sys : systems_) {
        for (auto& pipe : sys->pipes) {
            for (auto& stmt : pipe->stmts) {
                GenerateNode(stmt);
            }
        }
    }
    // Generate flops between each pipestage for each signal.
    for (auto& p : signal_stages_) {
        const auto* signal = p.first;
        GenerateStaging(signal);
    }
    // Generate storage elements: registers and arrays.
    for (auto& s : program_->storage) {
        GenerateStorage(s.get());
    }
    GenerateModuleEnd();
}

void VerilogGenerator::GenerateModuleStart() {
    out_->Print("module $module_name$(\n");
    {
        PrinterIndent arg_indent(out_);
        out_->Print("input clock,\ninput reset");
        for (auto& port : program_->ports) {
            if (port->exported) {
                GenerateModulePortDef(port.get());
            }
        }
        out_->Print("\n");
    }
    out_->Print(");\n");
    out_->Indent();
}

void VerilogGenerator::GenerateModulePortDef(const IRPort* port) {
    assert(port->width > 0);
    PrinterScope scope(out_);
    out_->SetVars({
        { "name", port->name },
        { "msb", strprintf("%d", port->width - 1) },
        { "dir", port->def != nullptr ? "output" : "input" },
    });
    out_->Print(",\n$dir$ [$msb$:0] $name$");
}

void VerilogGenerator::GenerateModuleEnd() {
    out_->Outdent();
    out_->Print("endmodule\n");
}

void VerilogGenerator::GenerateNode(const IRStmt* stmt) {
    PrinterScope scope(out_);
    // Materialize all inputs.
    vector<string> arg_signals;
    for (auto* arg : stmt->args) {
        arg_signals.push_back(GetSignalInStage(arg, stmt->stage->stage));
    }
    // Materialize the valid signal, if any.
    string valid_signal;
    if (stmt->valid_in) {
        valid_signal = GetSignalInStage(stmt->valid_in, stmt->stage->stage);
    }
    out_->SetVar("predicate", valid_signal);
    // If this statement generates an output value, declare a wire for it.
    string signal_name;
    if (stmt->width > 0) {
        signal_name = SignalName(stmt, stmt->stage->stage);
        out_->SetVars({
            { "signal", signal_name },
            { "width", strprintf("%d", stmt->width) },
        });
        out_->Print("wire [$width$-1:0] $signal$;\n");
    }
    out_->SetVar("signal", signal_name);

    switch (stmt->type) {
        case IRStmtExpr:
            GenerateNodeExpr(stmt, arg_signals);
            break;
        case IRStmtPhi:
        case IRStmtIf:
        case IRStmtJmp:
            assert(false);  // removed during if-conversion
            break;

        case IRStmtChanRead:
            out_->SetVar("arg", GetSignalInStage(stmt->port->def->args[0],
                                                 stmt->stage->stage));
            out_->Print("assign $signal$ = $arg$;\n");
            break;

        case IRStmtChanWrite:
            // Nothing -- corresponding ChanRead directly takes the writer's
            // arg.
            break;

        case IRStmtPortRead:
            out_->SetVar("portname", stmt->port->name);
            out_->Print("assign $signal$ = $portname$;\n");
            break;

        case IRStmtPortWrite:
            out_->SetVar("portname", stmt->port->name);
            out_->SetVar("arg", arg_signals[0]);
            if (!stmt->port->exported) {
                out_->SetVar("width", strprintf("%d", stmt->args[0]->width));
                out_->Print("wire [$width$-1:0] $portname$;\n");
            }
            out_->Print("assign $portname$ = $arg$;\n");
            break;

        case IRStmtPortExport:
            // Nothing.
            break;

        case IRStmtRegRead:
            out_->SetVar("regname", stmt->storage->name);
            out_->Print("assign $signal$ = reg_$regname$;\n");
            break;

        case IRStmtRegWrite:
            out_->SetVar("regname", stmt->storage->name);
            out_->SetVar("arg", arg_signals[0]);
            out_->Print(
                "always @(negedge clock) begin\n"
                "    if (reset)\n"
                "        reg_$regname$ <= 0;\n"
                "    else if ($predicate$)\n"
                "        reg_$regname$ <= $arg$;\n"
                "end\n");
            break;

        case IRStmtArrayRead:
            out_->SetVar("arrayname", stmt->storage->name);
            out_->SetVar("index", arg_signals[0]);
            out_->Print(
                "assign $signal$ = array_$arrayname$[$index$];\n");
            break;

        case IRStmtArrayWrite:
            out_->SetVar("arrayname", stmt->storage->name);
            out_->SetVar("index", arg_signals[0]);
            out_->SetVar("data", arg_signals[1]);
            out_->Print(
                "always @(negedge clock)\n"
                "    if ($predicate$)\n"
                "        array_$arrayname$[$index$] <= $data$;\n");
            break;

        case IRStmtRestartValue:
            // Get the RestartValueSrc buffered by one latch stage. This
            // aligns with our stage on the restart.
            if (stmt->restart_arg) {
                out_->SetVar("arg",
                        GetSignalInStage(
                            stmt->restart_arg,
                            stmt->restart_arg->stage->stage + 1));
                out_->Print("assign $signal$ = $arg$;\n");
            } else {
                // No arg -- set to all ones.
                out_->SetVar("allones", HexStrAllOnes(stmt->width));
                out_->Print("assign $signal$ = $width$'h$allones$;\n");
            }
            break;

        case IRStmtRestartValueSrc:
            // This doesn't generate anything -- the BackedgeValue produces the
            // latch.
            out_->SetVar("arg", GetSignalInStage(stmt->args[0], stmt->stage->stage));
            out_->Print("assign $signal$ = $arg$;\n");
            break;

        case IRStmtSpawn:
        case IRStmtKill:
        case IRStmtKillIf:
            // Nothing required -- valid-signal linkage takes care of these
            // automatically.

        case IRStmtKillYounger:
            // Nothing required -- recognized in AssignKills().
            break;

        case IRStmtTimingBarrier:
            // Nothing
            break;

        case IRStmtBackedge:
            // Nothing
            break;

        case IRStmtDone:
            // Nothing
            break;

        default:
            printf("Unknown statement in Verilog generator: %s\n", stmt->ToString().c_str());
            assert(false);
            break;
    }
}

namespace {
string GetExprOp(IRStmtOp op) {
    switch (op) {
        case IRStmtOpAdd: return "+";
        case IRStmtOpSub: return "-";
        case IRStmtOpMul: return "*";
        case IRStmtOpDiv: return "/";
        case IRStmtOpRem: return "%";
        case IRStmtOpAnd: return "&";
        case IRStmtOpOr:  return "|";
        case IRStmtOpXor: return "^";
        case IRStmtOpNot: return "~";
        case IRStmtOpLsh: return "<<";
        case IRStmtOpRsh: return ">>";
        case IRStmtOpCmpLT: return "<";
        case IRStmtOpCmpLE: return "<=";
        case IRStmtOpCmpEQ: return "==";
        case IRStmtOpCmpNE: return "!=";
        case IRStmtOpCmpGT: return "<";
        case IRStmtOpCmpGE: return "<=";
        default: assert(false); return "";
    }
}
}  // anonymous namespace

void VerilogGenerator::GenerateNodeExpr(const IRStmt* stmt,
                                        const vector<string>& args) {
    // Have vars: $signal$, $width$
    switch (stmt->op) {
        case IRStmtOpConst:
            out_->SetVar("const", string(stmt->constant));
            out_->Print("assign $signal$ = $width$'d$const$;\n");
            break;
        case IRStmtOpAdd:
        case IRStmtOpSub:
        case IRStmtOpMul:
        case IRStmtOpDiv:
        case IRStmtOpRem:
        case IRStmtOpAnd:
        case IRStmtOpOr:
        case IRStmtOpXor:
        case IRStmtOpLsh:
        case IRStmtOpRsh:
        case IRStmtOpCmpLT:
        case IRStmtOpCmpLE:
        case IRStmtOpCmpEQ:
        case IRStmtOpCmpNE:
        case IRStmtOpCmpGT:
        case IRStmtOpCmpGE:
            out_->SetVars({
                { "arg1", args[0] },
                { "arg2", args[1] },
                { "op", GetExprOp(stmt->op) },
            });
            out_->Print("assign $signal$ = $arg1$ $op$ $arg2$;\n");
            break;

        case IRStmtOpNot:
            out_->SetVars({
                { "arg1", args[0] },
            });
            out_->Print("assign $signal$ = ~ $arg1$;\n");
            break;

        case IRStmtOpBitslice:
            out_->SetVars({
                { "arg", args[0] },
                { "top", string(stmt->args[1]->constant) },
                { "bot", string(stmt->args[2]->constant) },
            });
            out_->Print("assign $signal$ = $arg$[$top$:$bot$];\n");
            break;

        case IRStmtOpConcat: {
            out_->Print("assign $signal$ = { ");
            bool first = true;
            for (const auto& arg : args) {
                PrinterScope subscope(out_);
                out_->SetVar("arg", arg);
                if (!first) out_->Print(", ");
                first = false;
                out_->Print("$arg$");
            }
            out_->Print("};\n");
            break;
        }

        case IRStmtOpSelect:
            out_->SetVars({
                { "sel", args[0] },
                { "arg1", args[1] },
                { "arg2", args[2] },
            });
            out_->Print("assign $signal$ = $sel$ ? $arg1$ : $arg2$;\n");
            break;

        default:
            assert(false);
    }
}

std::string VerilogGenerator::GetSignalInStage(const IRStmt* stmt, int stage) {
    auto it = signal_stages_.find(stmt);
    if (it == signal_stages_.end()) {
        it = signal_stages_.insert(
            make_pair(stmt,
                      make_pair(stmt->stage->stage,
                                stmt->stage->stage))).first;
    }
    auto& min_max = it->second;
    if (stage < min_max.first) min_max.first = stage;
    if (stage > min_max.second) min_max.second = stage;
    return SignalName(stmt, stage);
}

std::string VerilogGenerator::SignalName(const IRStmt* stmt, int stage) const {
    return strprintf("val%d_%d", stmt->valnum, stage);
}

void VerilogGenerator::GenerateStaging(const IRStmt* stmt) {
    auto it = signal_stages_.find(stmt);
    if (it == signal_stages_.end()) return;
    auto min_max = it->second;
    for (int i = min_max.first; i < min_max.second; i++) {
        // We need to stage the value from pipestage i to pipestage i+1.
        PrinterScope scope(out_);
        out_->SetVars({
            { "src", SignalName(stmt, i) },
            { "dst", SignalName(stmt, i+1) },
            // Valid signal is staged because it logically travels with the txn.
            { "valid", stmt->valid_in ? SignalName(stmt->valid_in, i) : "1'b1" },
            // Stall signal is not staged -- comes directly from combinational
            // logic that generates it.
            { "hold",  stmt->pipe->stages[i]->stall ?
                       SignalName(stmt->pipe->stages[i]->stall,
                                  stmt->pipe->stages[i]->stall->stage->stage) : "1'b0" },
            { "width", strprintf("%d", stmt->width) },
            { "instance_name", SignalName(stmt, i+1) + "_pipereg" },
        });

        out_->Print("pipereg #($width$) $instance_name$(\n"
                    "  .src($src$),\n"
                    "  .dst($dst$),\n"
                    "  .valid($valid$),\n"
                    "  .hold($hold$),\n"
                    "  .clock(clock),\n"
                    "  .reset(reset));\n");
    }
}

void VerilogGenerator::GenerateStorage(const IRStorage* storage) {
    PrinterScope scope(out_);
    out_->SetVar("name", storage->name);
    out_->SetVar("width", strprintf("%d", storage->data_width));
    assert(storage->index_width < 64);  // checked during typecheck
    out_->SetVar("entries", strprintf("%uld", (1UL << storage->index_width)));

    if (storage->index_width == 0) {  // individual register
        out_->Print("reg [$width$-1:0] reg_$name$;\n");
    } else {  // array
        out_->Print("reg [$width$-1:0] array_$name$[$entries$-1:0];\n");
    }
}
