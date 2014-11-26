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

#ifndef _AUTOPIPER_FRONTEND_CMDLINE_DRIVER_H_
#define _AUTOPIPER_FRONTEND_CMDLINE_DRIVER_H_

#include "frontend/compiler.h"
#include "backend/compiler.h"

#include <string>
#include <boost/noncopyable.hpp>

namespace autopiper {
namespace frontend {

class FrontendFlags;

class FrontendCmdlineDriver : public boost::noncopyable {
    public:
        FrontendCmdlineDriver() { }
        ~FrontendCmdlineDriver() { }

        void ParseArgs(int argc, const char* const* argv);
        void Execute();

    private:
        friend class FrontendFlags;
        autopiper::frontend::Compiler::Options options_;
};

}  // namespace frontend
}  // namespace autopiper

#endif  // _AUTOPIPER_FRONTEND_CMDLINE_DRIVER_H_
