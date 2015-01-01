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

#ifndef _AUTOPIPER_GEN_VERILOG_H_
#define _AUTOPIPER_GEN_VERILOG_H_

#include <vector>
#include <map>
#include <set>
#include <memory>
#include <string>
#include <iostream>

#include "backend/ir.h"
#include "backend/pipe.h"
#include "backend/gen-printer.h"

namespace autopiper {

class VerilogGenerator {
 public:
  VerilogGenerator(Printer* out,
                   const std::vector<PipeSys*>& systems,
                   const std::string& name)
      : out_(out), program_(nullptr), systems_(systems), name_(name) {
      if (systems.size() > 0) {
          program_ = systems[0]->program;
      }
  }

  void Generate();

 private:
  Printer* out_;
  IRProgram* program_;
  std::vector<PipeSys*> systems_;
  std::string name_;

  // Map from generating node to (min_stage, max_stage) pairs
  std::map<const IRStmt*, std::pair<int, int>> signal_stages_;

  // Returns a signal name for an IRStmt's value in a given stage. Creates
  // entries in the staged-values map but does not emit the pipereg instances.
  std::string GetSignalInStage(const IRStmt* stmt, int stage);

  // Generate initial node computation for a given node.
  void GenerateNode(const IRStmt* stmt);

  // Generate pipereg instances for a signal.
  void GenerateStaging(const IRStmt* stmt);

  // Helpers: Generate()
  void GenerateModuleStart();
  void GenerateModuleEnd();
  void GenerateModulePortDef(const IRPort* port);

  // Generate a storage element.
  void GenerateStorage(const IRStorage* storage);

  // Helper: GenerateNode()
  void GenerateNodeExpr(const IRStmt* stmt,
                        const std::vector<std::string>& args);

  // Helper: deterministic name given a stmt and a stage
  std::string SignalName(const IRStmt* stmt, int stage) const;

  // Generate the pipereg module.
  void GeneratePipeRegModule();
};

}  // namespace autopiper

#endif
