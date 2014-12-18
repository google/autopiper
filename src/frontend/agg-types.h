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

#ifndef _AUTOPIPER_FRONTEND_AGG_TYPES_H_
#define _AUTOPIPER_FRONTEND_AGG_TYPES_H_

#include "frontend/ast.h"

#include <map>
#include <set>
#include <string>

namespace autopiper {
namespace frontend {

// This resolves aggregate types down to their widths and the bit-offsets of
// each contained field. It is not actually a visit/modify pass; it iterates
// over typedefs manually (it does not need to see the main body of the
// program).
class AggTypeResolver {
    public:
        AggTypeResolver(AST* ast) : ast_(ast) {}
        ~AggTypeResolver() {}

        // Resolve all types' widths. Returns |true| if successful.
        bool Compute(ErrorCollector* coll);

        // Resolve a type to an inferred type value (width / agg type).
        InferredType ResolveType(const ASTType* type);

    private:
        AST* ast_;

        typedef std::map<std::string, ASTTypeDef*> TypeDefMap;

        TypeDefMap map_;

        // Create a map of typedefs by name given an AST.
        void BuildMap();
};

}  // namesapce frontend
}  // namespace autopiper

#endif  // _AUTOPIPER_FRONTEND_AGG_TYPES_H_
