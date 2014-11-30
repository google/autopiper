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

#ifndef _AUTOPIPER_FRONTEND_AST_LINK_H_
#define _AUTOPIPER_FRONTEND_AST_LINK_H_

#include "frontend/ast.h"
#include "frontend/visitor.h"

namespace autopiper {
namespace frontend {

// Links all var uses to the defining lets.
class VarLinkPass : public ASTVisitorContext {
    public:
        VarLinkPass();
    protected:
        // Block pre/post hooks: start and end lexical scopes.
        virtual void VisitASTStmtBlockPre(
                const ASTStmtBlock* node);
        virtual void VisitASTStmtBlockPost(
                const ASTStmtBlock* node);

        // Let hook: add another definition to the current scope.
        virtual void VisitASTStmtLet(
                const ASTStmtLet* node);
        virtual void VisitASTExpr(
                const ASTStmtExpr* node);

        // Rewrite portion: rewrite all var references to point to definition
        // used.
        virtual ASTRef<ASTExpr> ModifyASTExprPre(ASTRef<ASTExpr> node);
};

// Links all ASTTypes to the defining ASTTypeDefs.
class TypeLinkPass : public ASTVisitorContext {
};

}  // namesapce frontend
}  // namespace autopiper

#endif  // _AUTOPIPER_FRONTEND_AST_LINK_H_
