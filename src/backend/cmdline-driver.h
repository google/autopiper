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

#ifndef _AUTOPIPER_CMDLINE_DRIVER_H_
#define _AUTOPIPER_CMDLINE_DRIVER_H_

#include "backend/exception.h"
#include "backend/compiler.h"

#include <string>
#include <boost/noncopyable.hpp>

namespace autopiper {

class BackendCmdlineDriver : public boost::noncopyable {
    public:
        BackendCmdlineDriver();
        ~BackendCmdlineDriver();

        void ParseArgs(int argc, const char* const* argv);
        void Execute();

    private:
        Compiler compiler_;
        std::string file_, output_;
        CompilerOptions options_;

        void InterpretFlag(
                const std::string& flag,
                const std::string& value,
                bool* consumed_value);
};

}  // namespace autopiper

#endif
