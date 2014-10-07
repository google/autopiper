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

#include "gen-printer.h"

#include <assert.h>

using namespace autopiper;
using namespace std;

Printer::Printer(ostream* out)
    : out_(out), tab_width_(4), indent_(0) {
    // create global contex
    Context global_ctx;
    vars_.push_back(global_ctx);
}

Printer::~Printer() {
    out_->flush();
}

void Printer::PushContext() {
    Context new_ctx;
    vars_.push_back(new_ctx);
}

void Printer::PopContext() {
    assert(vars_.size() > 1);  // cannot pop global context
    vars_.pop_back();
}

void Printer::SetVar(const string& name,
                     const string& value) {
    assert(vars_.size() > 1);
    vars_.back().vars[name] = value;
}

void Printer::SetVars(const vector<PrintArg>& args) {
    for (const auto& arg : args) {
        SetVar(arg.name, arg.value);
    }
}

string Printer::Lookup(const string& name) const {
    for (auto i = vars_.rbegin(), e = vars_.rend(); i != e; ++i) {
        auto it = i->vars.find(name);
        if (it != i->vars.end()) {
            return it->second;
        }
    }
    return string();
}

string Printer::Format(const string& fmt) const {
    string out;
    bool in_varname = false;
    bool start_line = true;
    string varname;
    for (auto c : fmt) {
        if (start_line) {
            for (int i = 0; i < tab_width_ * indent_; i++) {
                out += ' ';
            }
            start_line = false;
        }
        if (in_varname) {
            if (c == '$') {
                if (varname.empty()) {
                    out += '$';
                } else {
                    string value = Lookup(varname);
                    varname.clear();
                    out += value;
                    if (!value.empty() && value[value.size() - 1] == '\n') {
                        start_line = true;
                    }
                }
                in_varname = false;
            } else { varname += c;
                if (c == '\n') {
                    start_line = true;
                }
            }
        } else {
            if (c == '$') {
                in_varname = true;
            } else {
                out += c;
            }
        }
    }
    return out;
}

void Printer::Print(const string& fmt) {
    Output(Format(fmt));
}

void Printer::Print(const string& fmt,
                    const vector<PrintArg>& args) {
    PrinterScope(this);
    SetVars(args);
    Print(fmt);
}

void Printer::Output(const string& output) {
    (*out_) << output;
}
