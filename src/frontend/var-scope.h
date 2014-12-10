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
        virtual bool ModifyASTFunctionDefPre(ASTRef<ASTFunctionDef>& node);
};

// This pass makes one sweep over the AST, linking all var uses (in ASTExprs
// and ASTStmtAssign left-hand sides) to their definitions (let stmts).
class VarScopePass : public ASTVisitorContext {
    public:
        VarScopePass(ErrorCollector* coll)
            : ASTVisitorContext(coll) {}

    protected:

        // Pre-hook on ASTStmtBlock: open a scope.
        virtual bool ModifyASTStmtBlockPre(ASTRef<ASTStmtBlock>& node);
        // Post-hook on ASTStmtBlock: close a scope.
        virtual bool ModifyASTStmtBlockPost(ASTRef<ASTStmtBlock>& node);
        // Post-hook on ASTStmtLet: create a new binding in the scope. Note
        // that this is a *post* handler because the let's initial_value
        // expression may refer to a previous binding; the new binding does not
        // enter the scope until after the let is evaluated.
        virtual bool ModifyASTStmtLetPost(ASTRef<ASTStmtLet>& node);

        // Modifier on ASTExpr: resolve all VARs.
        virtual bool ModifyASTExprPre(ASTRef<ASTExpr>& expr);

    private:
        struct Scope {
            // lexical scope root; do not go further up. This is our hacky way
            // of implementing lexical scope when inlining functions. The real
            // solution is to do name resolution prior to inlining. However,
            // then our inliner needs to do a bit more work to link up args and
            // the return value and to patch up the def-pointers on clone.
            // Because we don't have nested or first-class functions, but only
            // top-level functions with lexically nested scopes within them,
            // this should be sufficient.
            bool scope_root;
            std::map<std::string, ASTStmtLet*> defs;

            Scope() : scope_root(false) {}
            explicit Scope(bool scope_root_) : scope_root(scope_root_) {}
        };

        std::vector<Scope> scopes_;

        void OpenScope(bool is_root) {
            scopes_.push_back(Scope(is_root));
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