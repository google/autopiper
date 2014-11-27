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

#include "common/parse-args.h"
#include "common/exception.h"
#include "common/util.h"

#include <string>
#include <iostream>

using namespace autopiper;
using namespace std;

CmdlineParser::CmdlineParser(int argc, const char* const* argv)
    : argc_(argc), argv_(argv)
{ }

CmdlineParser::~CmdlineParser()
{ }

void CmdlineParser::Parse() {
    while (argc_ >= 1) {
        string flag = argv_[0];
        argv_++;
        argc_--;
        bool have_value = argc_ > 0;
        string value = have_value ? argv_[0] : "";
        if (!flag.empty() && flag[0] == '-') {
            switch (HandleFlag(flag, have_value, value)) {
                case FLAG_BAD:
                    throw autopiper::Exception(
                            strprintf("Invalid flag: %s. Use -h for help.",
                                flag.c_str()));
                case FLAG_CONSUMED_KEY:
                    continue;
                case FLAG_CONSUMED_KEY_VALUE:
                    if (!have_value) {
                        throw autopiper::Exception(
                                strprintf("Flag %s expects an argument.",
                                    flag.c_str()));
                    }
                    argc_--;
                    argv_++;
                    continue;
            }
        } else {
            switch (HandleArg(flag)) {
                case ARG_BAD:
                    throw autopiper::Exception(
                            strprintf("Bad argument: %s.", flag.c_str()));
                case ARG_OK:
                    continue;
            }
        }
    }
}
