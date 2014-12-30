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

#ifndef _AUTOPIPER_COMPILER_H_
#define _AUTOPIPER_COMPILER_H_

#include "backend/ir.h"
#include "common/parser-utils.h"

#include <boost/noncopyable.hpp>
#include <string>
#include <memory>

namespace autopiper {

class BackendCompiler : public boost::noncopyable {
    public:
        BackendCompiler() { }
        ~BackendCompiler() { }

        struct Options {
            // Specify exactly one of input_ir or filename (a text IR file).
            IRProgram* input_ir;
            std::string filename;

            // Verilog output.
            std::string output;

            // Print IR before transforming in the backend.
            bool print_ir;

            // Print lowered pipeline form before generating Verilog.
            bool print_lowered;

            Options()
                : input_ir(nullptr)
                , print_ir(false)
                , print_lowered(false)
            {}
        };

        bool CompileFile(const Options& options,
                         ErrorCollector* collector);
};

}  // namespace autopiper

#endif
