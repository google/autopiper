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

#ifndef _AUTOPIPER_FRONTEND_FUNC_INLINE_H_
#define _AUTOPIPER_FRONTEND_FUNC_INLINE_H_

#include "frontend/ast.h"
#include "frontend/visitor.h"

namespace autopiper {
namespace frontend {

// Modify pass -- convert let x = ... f(arg) ... to:
// {  // new block
//   let $temp$ = 0;
//   while (true) {
//     f's body: rewrite return to:
//       $temp$ = return value; break;
//     ... break
//   }
// }
// TODO: need labeled breaks for this to work.
// rewrite use of f(x) with $temp$
class FuncInlinePass : public ASTVisitorContext {
    public:
        FuncInlinePass();
    protected:
        // Visit pass -- make note of all function defs.
        virtual void VisitASTFunctionDefPre(
                const ASTFunctionDef* node);
        // Visit pass -- make note of all stmts that contain function calls.
        virtual void VisitASTStmt(const ASTStmt* node);
        virtual void VisitASTExpr(const ASTExpr* node);


        // To do the modification, we have (i) a pre-pass on ASTStmt that wraps
        // any stmt containing a function call in a block, recording the
        // statement->parent block mapping, and (ii) a post-pass on ASTExpr
        // that prepends the function body for the called function, if any,
        // within the enclosing stmt's block, rewriting the expr to a use of
        // the return-value variable.
        virtual ASTRef<ASTStmt> ModifyASTStmt(ASTRef<ASTStmt> node);
        virtual ASTRef<ASTExpr> ModifyASTExpr(ASTRef<ASTExpr> node);
    private:
        // Set of all ASTStmts that contain exprs with function calls.
        std::set<const ASTStmt*> stmts_containing_func_calls_;
        // map from callsite (ASTExpr) to function def.
        std::map<const ASTExpr*, const ASTFunctionDef*> function_defs_;
        // map from statements in stmts_containing_func_calls_ set to their
        // inserted surrounding blocks.
        std::map<const ASTStmt*, ASTStmtBlock*> parent_block_map_;

        // Visit pass: are we inside a stmt?
        const ASTStmt* visit_in_stmt_;
};

}  // namesapce frontend
}  // namespace autopiper

#endif  // _AUTOPIPER_FRONTEND_FUNC_INLINE_H_
