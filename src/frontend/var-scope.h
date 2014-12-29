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

#ifndef _AUTOPIPER_FRONTEND_VAR_SCOPE_H_
#define _AUTOPIPER_FRONTEND_VAR_SCOPE_H_

#include "frontend/ast.h"
#include "frontend/visitor.h"

#include <map>
#include <set>
#include <string>

namespace autopiper {
namespace frontend {

// This pass makes one sweep over the AST, adding lets for each param in each
// function def. Note that we are doing this after the function-inlining pass,
// so these lets won't interfere with the lets inserted during inlining.
class ArgLetPass : public ASTVisitorContext {
    public:
        ArgLetPass(ErrorCollector* coll)
            : ASTVisitorContext(coll) {}

    protected:
        virtual Result ModifyASTFunctionDefPre(ASTRef<ASTFunctionDef>& node);
};

// This pass makes one sweep over the AST, linking all var uses (in ASTExprs
// and ASTStmtAssign left-hand sides) to their definitions (let stmts).
class VarScopePass : public ASTVisitorContext {
    public:
        VarScopePass(ErrorCollector* coll)
            : ASTVisitorContext(coll) {}

    protected:

        // Pre-hook on ASTStmtBlock: open a scope.
        virtual Result ModifyASTStmtBlockPre(ASTRef<ASTStmtBlock>& node);
        // Post-hook on ASTStmtBlock: close a scope.
        virtual Result ModifyASTStmtBlockPost(ASTRef<ASTStmtBlock>& node);
        // Post-hook on ASTStmtLet: create a new binding in the scope. Note
        // that this is a *post* handler because the let's initial_value
        // expression may refer to a previous binding; the new binding does not
        // enter the scope until after the let is evaluated.
        virtual Result ModifyASTStmtLetPost(ASTRef<ASTStmtLet>& node);

        // Modifier on ASTExpr: resolve all VARs.
        virtual Result ModifyASTExprPre(ASTRef<ASTExpr>& expr);


    private:
        struct Scope {
            std::map<std::string, ASTStmtLet*> defs;
        };

        std::vector<Scope> scopes_;

        void OpenScope() {
            scopes_.push_back(Scope());
        }
        void CloseScope() {
            assert(!scopes_.empty());
            scopes_.pop_back();
        }

        // Returns null if not found.
        ASTStmtLet* GetDef(const std::string& name);
        // Returns true for success or false for failure (already defined).
        bool AddDef(const std::string& name, ASTStmtLet* def);
};

}  // namesapce frontend
}  // namespace autopiper

#endif  // _AUTOPIPER_FRONTEND_VAR_SCOPE_H_
