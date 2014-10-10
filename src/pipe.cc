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

#include "pipe.h"
#include "ir.h"

#include <sstream>
#include <iostream>

using namespace autopiper;
using namespace std;

namespace autopiper {

string Pipe::ToString() const {
    ostringstream os;

    os << "Pipe: entry at '" << entry->label << "'" << endl;
    os << "Spawned by: ";
    if (spawn) {
        os << " stmt %" << spawn->valnum << " in BB '" << spawn->bb->label << "'" << endl;
    } else {
        os << " (entry point)" << endl;
    }

    if (!stages.empty()) {
        for (auto& stage : stages) {
            os << "Pipestage " << stage->stage << endl;
            if (stage->stall) {
                os << "Stall = %" << stage->stall->valnum << endl;
            }
            os << "Kills = { ";
            for (auto* kill : stage->kills) {
                os << "%" << kill->valnum << ", ";
            }
            os << " }" << endl;
            for (auto* stmt : stage->stmts) {
                os << stmt->ToString() << endl;
            }
            os << endl;
        }
    } else {
        for (auto* stmt : stmts) {
            os << stmt->ToString() << endl;
        }
    }
    os << endl;

    return os.str();
}

string PipeSys::ToString() const {
    string s;
    for (auto& pipe : pipes) {
        s += pipe->ToString();
        s += "\n";
    }
    return s;
}

}  // namespace autopiper
