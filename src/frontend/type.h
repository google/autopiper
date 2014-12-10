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

#ifndef _AUTOPIPER_FRONTEND_TYPE_H_
#define _AUTOPIPER_FRONTEND_TYPE_H_

#include <string>

namespace autopiper {
namespace frontend {

struct ASTTypeDef;  // ast.h -- not included here (it includes this file).

// General inferred-type value type.
//
// Type inference works by:
// - assigning an 'inferred type' value to every expression node and let-stmt
//   (variable definition), initially equal to UNKNOWN (the "top" value in a
//   semilattice).
// - repeatedly evaluating transfer functions at each node. Depending on the
//   node type (let-def or an expr node with some op type) this transfer
//   function may use 'meet' to join types that should be equal (e.g., the
//   inputs and output to an ADD node) or do other things.
//
// When the transfer function evaluation reaches a fix-point, the inferred
// types remain on the nodes, and type-lowering will later use these inferred
// types.
struct InferredType {
    enum Type {
        UNKNOWN,  // not known yet ("top" in the semilattice)
        // known, with agg (null or not), is_port, is_array determining type
        RESOLVED,
        CONFLICT, // conflicted ("bottom" in the semilattice)
    } type;

    ASTTypeDef* agg;  // for aggregate only
    int width;        // for primitive and aggregate
    bool is_port;
    bool is_array;

    // Error message if conflicted.
    std::string conflict_msg;

    InferredType()
        : type(UNKNOWN), agg(nullptr), width(-1),
          is_port(false), is_array(false) {}
};

}  // namespace frontend
}  // namespace autopiper

#endif  // _AUTOPIPER_FRONTEND_TYPE_H_
