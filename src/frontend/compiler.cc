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
#include "frontend/func-inline.h"
#include "frontend/var-scope.h"
#include "frontend/type-infer.h"
#include "frontend/type-lower.h"
#include "frontend/codegen.h"
#include "backend/compiler.h"

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
        return false;
    }

    Lexer lexer(&in);
    Parser parser(options.filename, &lexer, collector);
    unique_ptr<AST> ast(new AST());
    if (!parser.Parse(ast.get())) {
        return false;
    }

    if (options.print_ast_orig) {
        PrintAST(ast.get(), cout);
    }

    // AST desugaring/transforms.
#define TRANSFORM(tform)                                                       \
    if (!ASTVisitor::Transform< tform >(ast, collector)) {                     \
        throw autopiper::Exception(                                            \
                "Compilation failed in pass '" #tform "'.");                   \
    }                                                                          \

    TRANSFORM(FuncInlinePass);
    TRANSFORM(ArgLetPass);
    TRANSFORM(VarScopePass);
    TRANSFORM(TypeInferPass);
    TRANSFORM(TypeLowerPass);

#undef TRANSFORM

    if (options.print_ast) {
        PrintAST(ast.get(), cout);
    }

    CodeGenContext codegen_ctx(ast.get());
    CodeGenPass codegen_pass(collector, &codegen_ctx);
    ASTVisitor codegen_visitor;
    if (!codegen_visitor.ModifyAST(ast, &codegen_pass)) {
        throw autopiper::Exception(
                "Compilation failed in IR code generation.");
    }
    codegen_pass.RemoveUnreachableBBsAndPhis();

    unique_ptr<IRProgram> ir = codegen_ctx.Release();

    if (options.print_ir) {
        printf("IR:\n%s\n", ir->ToString().c_str());
    }

    // TODO: print IR to IR output file if requested (ensure IR printer
    // produces parsable output).

    BackendCompiler backend_;
    BackendCompiler::Options backend_options_;
    backend_options_.input_ir = ir.get();
    backend_options_.filename = "(ir)";
    backend_options_.output = options.output;
    backend_options_.print_lowered = options.print_lowered;
    if (!backend_.CompileFile(backend_options_, collector)) {
        throw autopiper::Exception(
                "Compilation failed in backend.");
    }

    return true;
}
