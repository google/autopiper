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

#include "frontend/cmdline-driver.h"
#include "frontend/compiler.h"
#include "backend/compiler.h"
#include "common/parse-args.h"
#include "common/exception.h"
#include "build-config.h"

using namespace std;

namespace autopiper {
namespace frontend {

static const char* kUsage =
    "Usage: autopiper [flags] <input>\n"
    "    Flags:\n"
    "        -o <file>:          specify the Verilog output filename (<input>.v by default).\n"
    "        --print-ast:        print the AST after parsing.\n"
    "        --print-ir:         print the IR after frontend codegen,\n"
    "                            before backend transforms.\n"
    "        --print-lowered:    print the lowered pipeline form before backend codegen.\n"
    "        --ir-output <file>: print the IR to the given file (and continue to backend).\n"
    "        -h, --help:         print this help message.\n"
    "        -v, --version:      print version and license information.\n";

static const char* kVersion =
    "autopiper version " CONFIG_VERSION ".\n"
    "\n"
    "autopiper is authored by Chris Fallin <cfallin@c1f.net>.\n"
    "\n"
    "autopiper is Copyright (c) 2014 Google Inc. All rights reserved.\n"
    "\n"
    "Licensed under the Apache License, Version 2.0 (the \"License\");\n"
    "you may not use this file except in compliance with the License.\n"
    "You may obtain a copy of the License at\n"
    "\n"
    "    http://www.apache.org/licenses/LICENSE-2.0\n"
    "\n"
    "Unless required by applicable law or agreed to in writing, software\n"
    "distributed under the License is distributed on an \"AS IS\" BASIS,\n"
    "WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.\n"
    "See the License for the specific language governing permissions and\n"
    "limitations under the License.\n"
    "\n"
    "autopiper is not an official Google product; it is a side project\n"
    "developed independently by the author.\n"
    "\n";

class FrontendFlags : public CmdlineParser {
    public:
        FrontendFlags(FrontendCmdlineDriver* driver,
                int argc, const char* const* argv)
            : CmdlineParser(argc, argv), driver_(driver)
        { }

    protected:
        virtual FlagHandlerResult HandleFlag(
                const std::string& flag,
                bool have_value,
                const std::string& value) {
            if (flag == "--print-ast") {
                driver_->options_.print_ast = true;
                return FLAG_CONSUMED_KEY;
            } else if (flag == "--print-ir") {
                driver_->options_.print_ir = true;
                return FLAG_CONSUMED_KEY;
            } else if (flag == "--print-lowered") {
                driver_->options_.print_lowered = true;
                return FLAG_CONSUMED_KEY;
            } else if (flag == "--ir-output") {
                driver_->options_.ir_output = value;
                return FLAG_CONSUMED_KEY_VALUE;
            } else if (flag == "-o") {
                driver_->options_.output = value;
                return FLAG_CONSUMED_KEY_VALUE;
            } else if (flag == "-h" || flag == "--help") {
                cerr << kUsage;
                throw autopiper::Exception("No compilation performed.");
            } else if (flag == "-v" || flag == "--version") {
                cerr << kVersion;
                throw autopiper::Exception("No compilation performed.");
            }
            return FLAG_BAD;
        }

        virtual ArgHandlerResult HandleArg(const string& arg) {
            if (driver_->options_.filename.empty()) {
                driver_->options_.filename = arg;
                if (driver_->options_.output.empty()) {
                    driver_->options_.output = arg + ".v";
                }
                return ARG_OK;
            }
            return ARG_BAD;
        }

    private:
        FrontendCmdlineDriver* driver_;

};

void FrontendCmdlineDriver::ParseArgs(int argc, const char* const* argv) {
    FrontendFlags parser(this, argc, argv);
    parser.Parse();
}

void FrontendCmdlineDriver::Execute() {
    frontend::Compiler compiler;
    CmdlineErrorCollector collector(&std::cerr);
    if (!compiler.CompileFile(options_, &collector)) {
        throw autopiper::Exception("Compilation failed.");
    }
}

}  // namespace frontend
}  // namespace autopiper
