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

#include "backend/compiler.h"
#include "backend/ir.h"
#include "backend/pipe.h"
#include "backend/gen-verilog.h"

#include <fstream>
#include <memory>

using namespace std;

namespace autopiper {

bool BackendCompiler::CompileFile(
        const Options& options,
        ErrorCollector* collector) {
    unique_ptr<IRProgram> parsed_prog;
    IRProgram* prog = nullptr;

    if (options.input_ir) {
        prog = options.input_ir;
    } else {
        ifstream in(options.filename);
        if (!in.good()) {
            Location loc;
            loc.filename = options.filename;
            loc.line = loc.column = 0;
            collector->ReportError(loc, ErrorCollector::ERROR,
                                   string("Could not open file '") +
                                   options.filename +
                                   string("'"));
            return false;
        }

        parsed_prog = IRProgram::Parse(options.filename, &in, collector);
        prog = parsed_prog.get();
        in.close();
        if (!prog) return false;
    }

    if (!prog->Crosslink(collector)) return false;
    if (!prog->Typecheck(collector)) return false;

    if (options.print_ir) {
        printf("IR:\n%s\n", prog->ToString().c_str());
    }

    vector<unique_ptr<PipeSys>> pipesystems = prog->Lower(collector);
    if (pipesystems.empty()) return false;
    vector<PipeSys*> systems;
    for (auto& sys : pipesystems) {
        systems.push_back(sys.get());
    }

    if (options.print_lowered) {
        printf("Lowered pipeline form:\n");
        for (auto& pipesys : pipesystems) {
            printf("%s\n", pipesys->ToString().c_str());
        }
    }

    ofstream out(options.output);
    if (!out.good()) {
        Location loc;
        loc.filename = options.output;
        loc.line = loc.column = 0;
        collector->ReportError(loc, ErrorCollector::ERROR,
                               string("Could not open file '") +
                               options.output +
                               string("'"));
    }
    Printer out_printer(&out);

    VerilogGenerator gen(&out_printer, systems, "main");
    gen.Generate();
    out.close();

    return true;
}

}
