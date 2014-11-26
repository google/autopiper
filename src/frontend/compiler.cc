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

#include "frontend/compiler.h"
#include "frontend/parser.h"

#include <fstream>
#include <memory>

using namespace autopiper::frontend;
using namespace autopiper;
using namespace std;

bool Compiler::CompileFile(const Options& options, ErrorCollector* collector) {
    // Parse input.
    ifstream in(options.filename);
    if (!in.good()) {
        Location loc;
        loc.filename = options.filename;
        loc.line = loc.column = 0;
        collector->ReportError(loc, ErrorCollector::ERROR,
                               string("Could not open file '") +
                               options.filename +
                               string("'"));
    }

    Lexer lexer(&in);
    Parser parser(options.filename, &lexer, collector);
    unique_ptr<AST> ast(new AST());
    if (!parser.Parse(ast.get())) {
        return false;
    }

    // TODO: crosslinking
    // TODO: typechecking and width inferencing
    // TODO: AST-visitor codegen pass
    // TODO: print/write out IR if requested; invoke backend

    return true;
}
