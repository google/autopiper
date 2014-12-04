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

#include <map>
#include <set>
#include <string>

namespace autopiper {
namespace frontend {

// Modify pass -- convert let x = ... f(arg) ... to:
// {  // new block
//   let $temp$ = 0;
//   func_loop: while (true) {
//     f's body: rewrite return to:
//       $temp$ = return_value; break func_loop;
//     ... break
//   }
// }
// rewrite use of f(x) with $temp$
class FuncInlinePass : public ASTVisitorContext {
    public:
        FuncInlinePass(autopiper::ErrorCollector* coll);

        static bool Transform(ASTRef<AST>& node,
                              autopiper::ErrorCollector* coll) {
            ASTVisitor visitor;
            FuncInlinePass pass(coll);
            if (!visitor.VisitAST(node.get(), &pass)) {
                return false;
            }
            if (!visitor.ModifyAST(node, &pass)) {
                return false;
            }
            return true;
        }

    protected:
        // Visit pass -- make note of all function defs.
        virtual bool VisitASTFunctionDefPre(
                const ASTFunctionDef* node);

        // To do the modification, we have a post-pass on ASTExpr
        // that replaces a function call expr with a statement-block expr whose
        // body is a modified version of the called function's body, using a
        // while-loop pattern with break statements and assignments to a
        // temporary return value variable to desugar 'return' statements.
        virtual bool ModifyASTExprPost(ASTRef<ASTExpr>& node);

        // Grab a pointer to the AST so we can gensym new temps.
        virtual bool ModifyASTPre(ASTRef<AST>& node) {
            ast_ = node.get();
            return true;
        }
    private:
        // map from function names to ASTFunctionDefs.
        std::map<const std::string, const ASTFunctionDef*> function_defs_;

        AST* ast_;
};

}  // namesapce frontend
}  // namespace autopiper

#endif  // _AUTOPIPER_FRONTEND_FUNC_INLINE_H_
