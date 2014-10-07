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

#ifndef _AUTOPIPER_IR_BUILD_H_
#define _AUTOPIPER_IR_BUILD_H_

#include "ir.h"

namespace autopiper {

class IRBBBuilder {
 public:
  IRBBBuilder(IRProgram* prog, IRBB* bb) : prog_(prog), bb_(bb) {}

  IRStmt* Add(IRStmt* new_stmt) {
      std::unique_ptr<IRStmt> p(new_stmt);
      stmts_.push_back(std::move(p));
      return new_stmt;
  }

  IRStmt* AddExpr(IRStmtOp op, std::vector<IRStmt*> args) {
      std::unique_ptr<IRStmt> new_stmt(new IRStmt());
      new_stmt->valnum = prog_->GetValnum();
      new_stmt->bb = bb_;
      new_stmt->type = IRStmtExpr;
      new_stmt->op = op;
      for (auto* arg : args) {
          new_stmt->arg_nums.push_back(arg->valnum);
          new_stmt->args.push_back(arg);
          new_stmt->width = arg->width;
      }
      IRStmt* ret = new_stmt.get();
      stmts_.push_back(std::move(new_stmt));
      return ret;
  }

  IRStmt* AddStmt(std::unique_ptr<IRStmt> stmt) {
      IRStmt* ret = stmt.get();
      stmts_.push_back(move(stmt));
      return ret;
  }

  // Requires |op| to be associative, i.e. (((v1 op v2) op v3) op v4) can be
  // rewritten (into a tree) as ((v1 op v2) op (v3 op v4)).
  IRStmt* BuildTree(IRStmtOp op, std::vector<IRStmt*> args) {
      // Base cases.
      if (args.size() == 0) return nullptr;
      if (args.size() == 1) return args[0];
      // Otherwise, build one layer.
      std::vector<IRStmt*> layer;
      for (unsigned i = 0; i < args.size(); i += 2) {
          if (i == args.size() - 1) {
              layer.push_back(args[i]);
          } else {
              layer.push_back(AddExpr(op, { args[i], args[i+1] }));
          }
      }
      return BuildTree(op, layer);
  }

  // Prepend accumulated ops to the BB's stmt list. User should batch additions
  // and do this only once, since it needs to copy the stmt list.
  void PrependToBB(std::function<bool(IRStmt*)> remove_pred = DefaultPred) {
      for (auto& s : bb_->stmts) {
          if (!remove_pred(s.get()))
              stmts_.push_back(std::move(s));
      }
      std::swap(bb_->stmts, stmts_);
      stmts_.clear();
  }

  void ReplaceBB() {
      std::swap(bb_->stmts, stmts_);
      stmts_.clear();
  }

 private:
  IRProgram* prog_;
  IRBB* bb_;
  // statements to append at flush.
  std::vector<std::unique_ptr<IRStmt>> stmts_;

  static bool DefaultPred(IRStmt*) { return true; }
};

}  // namespace autopiper

#endif
