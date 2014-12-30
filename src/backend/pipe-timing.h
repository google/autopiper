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

#ifndef _AUTOPIPER_PIPE_TIMING_H_
#define _AUTOPIPER_PIPE_TIMING_H_

#include "backend/ir.h"
#include "backend/pipe.h"

#include <vector>
#include <memory>
#include <string>

namespace autopiper {

class TimingModel {
    public:
        virtual int Delay(const IRStmt* stmt) const = 0;
        virtual int DelayPerStage() const = 0;

        static std::unique_ptr<TimingModel> New(std::string name);
};

class StandardTimingModel : public TimingModel {
    public:
        StandardTimingModel() {}

        virtual int Delay(const IRStmt* stmt) const;
        virtual int DelayPerStage() const;

    private:
        static const int kGatesPerStage = 32;  // TODO: parameterize this (knobs on cmdline)
};

class NullTimingModel : public TimingModel {
    public:
        NullTimingModel() {}

        virtual int Delay(const IRStmt* stmt) const { return 0; }
        virtual int DelayPerStage() const { return 1; }
};

class PipeTimer {
    public:
        PipeTimer(TimingModel* model) : model_(model) {}
        bool TimePipe(PipeSys* sys, ErrorCollector* coll) const;

    private:
        TimingModel* model_;
};

}

#endif
