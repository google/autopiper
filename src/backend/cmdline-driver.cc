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

#include "cmdline-driver.h"
#include "util.h"
#include "build-config.h"

#include <string>
#include <iostream>

using namespace std;

namespace autopiper {

namespace {

class CmdlineErrorCollector : public ErrorCollector {
    public:
        CmdlineErrorCollector(std::ostream* output)
            : output_(output), has_errors_(false) {}

        virtual void ReportError(Location loc,
                                 Level level,
                                 const std::string& message) {
            switch (level) {
                case ERROR:
                    (*output_) << "Error: ";
                    has_errors_ = true;
                    break;
                case WARNING:
                    (*output_) << "Warning: ";
                    break;
                case INFO:
                    (*output_) << "Info: ";
                    break;
            }
            (*output_) << loc.filename << ":" << loc.line << ":" << loc.column << ": ";
            (*output_) << message << endl;
        }

        virtual bool HasErrors() const { return has_errors_; }
    private:
        std::ostream* output_;
        bool has_errors_;
};

}

CmdlineDriver::CmdlineDriver() {
}

CmdlineDriver::~CmdlineDriver() {
}

static const char* kUsage =
    "Usage: autopiper-backend [flags] <input>\n"
    "    Flags:\n"
    "        -o <filename>:   specify the Verilog output filename (<input>.v by default).\n"
    "        --print-ir:      print IR as parsed, before transforms or lowering.\n"
    "        --print-lowered: print program as lowered to pipeline form,\n"
    "                         before code generation occurs.\n"
    "        -h, --help:      print this help message.\n"
    "        -v, --version:   print version and license information.\n";

static const char* kVersion =
    "autopiper-backend version " CONFIG_VERSION ".\n"
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

void CmdlineDriver::InterpretFlag(
        const std::string& flag,
        const std::string& value,
        bool* consumed_value) {
    if (flag == "--print-ir") {
        options_.print_ir = true;
    } else if (flag == "--print-lowered") {
        options_.print_lowered = true;
    } else if (flag == "-o") {
        output_ = value;
        *consumed_value = true;
    } else if (flag == "-h" || flag == "--help") {
        cerr << kUsage;
        throw autopiper::Exception("No compilation performed.");
    } else if (flag == "-v" || flag == "--version") {
        cerr << kVersion;
        throw autopiper::Exception("No compilation performed.");
    } else {
        throw autopiper::Exception(
                strprintf("Invalid flag: %s. Use -h for help.", flag.c_str()));
    }
}

void CmdlineDriver::ParseArgs(int argc, const char* const* argv) {
    // Look for flags.
    while (argc >= 1) {
        string flag = argv[0];
        if (flag.empty() || flag[0] != '-') break;
        argv++;
        argc--;
        string value = (argc > 0) ? argv[0] : "";  // don't consume it yet
        bool consumed_value = false;
        InterpretFlag(flag, value, &consumed_value);
        if (consumed_value) {
            if (argc == 0) {
                throw autopiper::Exception(
                        strprintf("Flag '%s' requires a value.", flag.c_str()));
            }
            argv++;
            argc--;
        }
    }

    if (argc != 1) {
        cerr << "No input filename specified." << endl
             << "Specify -h for usage help." << endl;
        throw autopiper::Exception("No compilation performed.");
    }

    file_ = argv[0];
    if (output_.empty()) {
        output_ = file_ + ".v";
    }
}

void CmdlineDriver::Execute() {
    CmdlineErrorCollector collector(&std::cerr);
    if (!compiler_.CompileFile(file_, output_, options_, &collector)) {
        throw autopiper::Exception("Compilation failed.");
    }
}

}  // namespace autopiper
