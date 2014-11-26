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

#ifndef _AUTOPIPER_COMMON_ERROR_COLLECTOR_H_
#define _AUTOPIPER_COMMON_ERROR_COLLECTOR_H_

#include "common/parser-utils.h"

#include <iostream>

namespace autopiper {

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
            (*output_) << message << std::endl;
        }

        virtual bool HasErrors() const { return has_errors_; }
    private:
        std::ostream* output_;
        bool has_errors_;
};

}  // namespace autopiper

#endif // _AUTOPIPER_COMMON_ERROR_COLLECTOR_H_
