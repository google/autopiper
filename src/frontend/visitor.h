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
#include "common/parser-utils.h"  // ErrorCollector
#include "common/exception.h"

namespace autopiper {
namespace frontend {

class ASTVisitorContext;

class ASTVisitor {
    public:
        ASTVisitor() {}
        ~ASTVisitor() {}

        template<typename T, typename ...Args>
        static bool Transform(ASTRef<AST>& node,
                              autopiper::ErrorCollector* coll,
                              Args&&... pass_args) {
            ASTVisitor visitor;
            T pass(coll, pass_args...);
            if (T::NeedsVisit()) {
                if (!visitor.VisitAST(node.get(), &pass)) {
                    return false;
                }
            }
            return visitor.ModifyAST(node, &pass);
        }

#define METHODS(type)                                                          \
        bool Visit ## type(const type* ast, ASTVisitorContext* context) const; \
        bool Modify ## type(ASTRef<type>& node, ASTVisitorContext* context)    \
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
        METHODS(ASTStmtReturn)
        METHODS(ASTStmtExpr)
        METHODS(ASTExpr)
        METHODS(ASTTypeField)

#undef METHODS
};

class ASTVisitorContext {
    public:
        ASTVisitorContext() : coll_(nullptr) {}
        ASTVisitorContext(autopiper::ErrorCollector* coll) : coll_(coll) {}
        ~ASTVisitorContext() {}

    protected:
        friend class ASTVisitor;

        // Called by ASTVisitor::Transform. Override and return true to
        // indicate that the pass requires a Visit traversal over the AST
        // before the Modify traversal.
        static bool NeedsVisit() { return false; }

        ErrorCollector* Errors() { return coll_; }

        template<typename T> void Error(const T* node, const std::string& str) {
            if (Errors()) {
                Errors()->ReportError(node->loc,
                        autopiper::ErrorCollector::ERROR, str);
            }
        }

        // The visitor can override up to three methods per node type: a
        // Visit<type>() method, a Visit<type>Post() method, and a
        // Modify<type>() method . The methods are used as follows:
        //
        // For a traversal: for each node:
        //     - The Visit<type> method is called.
        //     - the traversal recurses down the subtree.
        //     - The Visit<type>Post method is called.
        //
        // If a Visit method returns false, traversal stops immediately.
        //
        // For a modify pass: for each node:
        //     - The ModifyPre() method is called.
        //     - the modification traversal recurses down the (new) subtree.
        //     - The ModifyPost() method is called.
        //
        // Each modification method takes a reference to a unique_ptr<T>. It is
        // allowed to modify the referred-to node in place or replace it. If it
        // replaces the node, it may steal the original node (e.g. to use as
        // part of a subtree of the new node) or simply allow the unique_ptr to
        // delete it. The method returns a bool; if it returns false, traversal
        // stops immediately.
#define METHODS(type)                                                          \
        virtual bool Visit ## type ## Pre(const type* node) { return true; }   \
        virtual bool Visit ## type ## Post(const type* node) { return true; }  \
        virtual bool Modify ## type ## Pre(ASTRef<type>& node) { return true; }\
        virtual bool Modify ## type ## Post(ASTRef<type>& node) { return true; }

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
        METHODS(ASTStmtReturn)
        METHODS(ASTStmtExpr)
        METHODS(ASTExpr)
        METHODS(ASTTypeField)

#undef METHODS

    private:
        ErrorCollector* coll_;
};

}  // namesapce frontend
}  // namespace autopiper

#endif  // _AUTOPIPER_FRONTEND_VISITOR_H_
