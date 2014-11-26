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

#ifndef _AUTOPIPER_COMMON_PARSE_ARGS_H_
#define _AUTOPIPER_COMMON_PARSE_ARGS_H_

#include <string>

namespace autopiper {

class CmdlineParser {

    public:
        CmdlineParser(int argc, const char* const* argv);
        ~CmdlineParser();

        void Parse();

    protected:
        enum FlagHandlerResult {
            FLAG_BAD,
            FLAG_CONSUMED_KEY,
            FLAG_CONSUMED_KEY_VALUE,
        };

        // Handle a flag: a "-x value" or "--long value" pair.
        virtual FlagHandlerResult HandleFlag(
                const std::string& flag,
                bool have_value,
                const std::string& value)
        { return FLAG_BAD; }

        enum ArgHandlerResult {
            ARG_BAD,
            ARG_OK,
        };

        // Handle an arg: a freestanding argument without a leading dash.
        virtual ArgHandlerResult HandleArg(const std::string& arg)
        { return ARG_BAD; }

    private:
        int argc_;
        const char* const* argv_;
};

}  // namespace autopiper

#endif // _AUTOPIPER_COMMON_PARSE_ARGS_H_
