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

#ifndef _AUTOPIPER_FRONTEND_VISITOR_H_
#define _AUTOPIPER_FRONTEND_VISITOR_H_

#include "frontend/ast.h"

namespace autopiper {
namespace frontend {

class ASTVisitorContext;

class ASTVisitor {
    public:
        ASTVisitor() {}
        ~ASTVisitor() {}

        void Visit(const AST* ast, ASTVisitorContext* context) const;
        ASTRef<AST> Modify(ASTRef<AST> ast, ASTVisitorContext* context) const;

    private:
#define METHODS(type)                                                          \
        void Visit ## type(const type* ast, ASTVisitorContext* context) const; \
        ASTRef<AST> Modify ## type(ASTRef<AST> ast, ASTVisitorContext* contex) \
            const;

        METHODS(AST)
        METHODS(ASTFunctionDef)
        METHODS(ASTTypeDef)
        METHODS(ASTIdent)
        METHODS(ASTType)
        METHODS(ASTParam)
        METHODS(ASTStmt)
        METHODS(ASTStmtBlock)
        METHODS(ASTStmtLet)
        METHODS(ASTStmtAssign)
        METHODS(ASTStmtIf)
        METHODS(ASTStmtWhile)
        METHODS(ASTStmtBreak)
        METHODS(ASTStmtContinue)
        METHODS(ASTStmtWrite)
        METHODS(ASTStmtSpawn)
        METHODS(ASTExpr)
        METHODS(ASTTypeField)

#undef METHODS
};

class ASTVisitorContext {
    public:
        ASTVisitorContext() {}
        ~ASTVisitorContext() {}

    protected:
        friend class ASTVisitor;

        // The visitor can override up to three methods per node type: a
        // Visit<type>() method, a Visit<type>Post() method, and a
        // Modify<type>() method . The methods are used as follows:
        //
        // For a traversal: for each node:
        //     - The Visit<type> method is called.
        //     - the traversal recurses down the subtree.
        //     - The Visit<type>Post method is called.
        //
        // For a modify pass: for each node:
        //     - The ModifyPre() method is called.
        //     - the modification traversal recurses down the (new) subtree.
        //     - The ModifyPost() method is called.
        //
        // The modification protocol is: each modification method returns the
        // new node that should replace the existing one, or the same node if
        // no modification is to be performed.
#define METHODS(type)                                                          \
        virtual void  Visit ## type ## Pre(const type* node) {}                \
        virtual void  Visit ## type ## Post(const type* node) {}               \
        virtual ASTRef<type> Modify ## type ## Pre(ASTRef<type> node)          \
            { return node; }                                                   \
        virtual ASTRef<type> Modify ## type ## Post(ASTRef<type> node)         \
            { return node; }

        METHODS(AST)
        METHODS(ASTFunctionDef)
        METHODS(ASTTypeDef)
        METHODS(ASTIdent)
        METHODS(ASTType)
        METHODS(ASTParam)
        METHODS(ASTStmt)
        METHODS(ASTStmtBlock)
        METHODS(ASTStmtLet)
        METHODS(ASTStmtAssign)
        METHODS(ASTStmtIf)
        METHODS(ASTStmtWhile)
        METHODS(ASTStmtBreak)
        METHODS(ASTStmtContinue)
        METHODS(ASTStmtWrite)
        METHODS(ASTStmtSpawn)
        METHODS(ASTExpr)
        METHODS(ASTTypeField)

#undef METHODS
};

}  // namesapce frontend
}  // namespace autopiper

#endif  // _AUTOPIPER_FRONTEND_VISITOR_H_
