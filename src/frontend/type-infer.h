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

#ifndef _AUTOPIPER_FRONTEND_TYPE_INFER_H_
#define _AUTOPIPER_FRONTEND_TYPE_INFER_H_

#include "frontend/ast.h"
#include "frontend/visitor.h"

#include <map>
#include <set>
#include <string>

namespace autopiper {
namespace frontend {

// This pass makes one sweep over the AST, attempting to derive each expr's and
// let-stmt's width from its args, or derive its args' widths from itself (both
// directions are needed to reach convergence in many cases). The invocation
// driver function will continue to invoke the pass until a fix-point is
// reached, at which point either all types or known or they can't be.
//
// This pass runs after function inlining (so does not need to deal with
// func-arg type propagation) but before type lowering (so needs to worry about
// aggregate types and type fields).
class TypeInferPass : public ASTVisitorContext {
    public:
        TypeInferPass(ErrorCollector* coll)
            : ASTVisitorContext(coll) {}

    protected:
        // We hook expr nodes to propagate types 'up' (from args to result) and
        // 'down' (from result to args) as appropriate.
        virtual bool ModifyASTExprPost(ASTRef<ASTExpr>& node);
        // We hook lets to connect the explicit definition type (if given), the
        // var's type, and the initial_value expr's type.
        virtual bool ModifyASTStmtLetPost(ASTRef<ASTStmtLet>& node);

    private:
        bool HandleBinOpCommonWidth(ASTExpr* node);
        bool CheckArgsArePrimitive(ASTExpr* node);

        // ------ InferredType operations ------

        // Create a new InferredType from an ASTType and an AST typedef map.
        typedef std::map<std::string, const ASTTypeDef*> TypeDefMap;
        static InferredType New(const ASTType* type, const TypeDefMap& types);
        // Create a map of typedefs by name for 'New' above given an AST.
        static TypeDefMap GetTypeMap(const AST* ast);

        // Join two types. Resolves to a concrete type if either input type is
        // concrete or if both are, and are the same, or "top" if neither is known,
        // or "bottom" if both are known and are different.
        InferredType Meet(const InferredType& other) const;
};

}  // namesapce frontend
}  // namespace autopiper

#endif  // _AUTOPIPER_FRONTEND_TYPE_INFER_H_
