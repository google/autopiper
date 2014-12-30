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

#ifndef _AUTOPIPER_FRONTEND_COMPILER_H_
#define _AUTOPIPER_FRONTEND_COMPILER_H_

#include "common/error-collector.h"

#include <string>
#include <boost/noncopyable.hpp>

namespace autopiper {
namespace frontend {

class Compiler : public boost::noncopyable {
    public:
        Compiler() { }
        ~Compiler() { }

        struct Options {
            // Print AST after parsing.
            bool print_ast_orig;

            // Print AST before doing codegen.
            bool print_ast;

            // Print IR after frontend codegen.
            bool print_ir;

            // Print IR after backend transforms but before
            // lowering/pipelining.
            bool print_backend_ir;

            // Print lowered form after lowering/pipelining.
            bool print_lowered;

            // Autopiper input.
            std::string filename;

            // IR output.
            std::string ir_output;

            // Verilog output.
            std::string output;

            Options()
                : print_ast_orig(false)
                , print_ast(false)
                , print_ir(false)
                , print_backend_ir(false)
                , print_lowered(false)
            { }
        };

        bool CompileFile(const Options& options,
                         ErrorCollector* collector);
};

}  // namespace frontend
}  // namespace autopiper

#endif  // _AUTOPIPER_FRONTEND_COMPILER_H_
