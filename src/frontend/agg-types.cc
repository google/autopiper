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

#include "frontend/agg-types.h"
#include "common/util.h"

using namespace std;

namespace autopiper {
namespace frontend {

// We resolve aggregate types (structs) so that each field has a bit offset and
// each type has a total width in bits. We use an iterative approach here to
// resolve types that include other types.

void AggTypeResolver::BuildMap() {
    for (auto& type : ast_->types) {
        map_.insert(make_pair(type->ident->name, type.get()));
    }
}

bool AggTypeResolver::Compute(ErrorCollector* coll) {
    map<ASTTypeDef*, InferredType> typedef_types;
    map<ASTTypeField*, InferredType> field_types;

    BuildMap();

    // Initially set all field types according to the type map.
    for (auto& type_pair : map_) {
        ASTTypeDef* type = type_pair.second;
        for (auto& field : type->fields) {
            InferredType inftype = ResolveType(field->type.get());
            // If this is an aggregate type, keep as 'UNKNOWN'
            if (inftype.agg) {
                inftype.type = InferredType::UNKNOWN;
            }
            field_types[field.get()] = inftype;
        }
    }

    // While at least one type resolves, continue iterating over all types and
    // resolving fields. A type is resolved when all its fields' types are
    // resolved.
    bool changed = true;
    int iters = 0;
    static const int kMaxIters = 100;
    while (changed && iters++ < kMaxIters) {
        changed = false;
        for (auto& type_pair : map_) {
            ASTTypeDef* type = type_pair.second;
            auto& type_inftype = typedef_types[type];
            // If this aggregate type is already completely resolved, then
            // we're done.
            if (type_inftype.type == InferredType::RESOLVED) {
                continue;
            }
            // Resolve all the field types that we can.
            bool unresolved = false;
            for (auto& field : type->fields) {
                auto& field_inftype = field_types[field.get()];
                if (field_inftype.type == InferredType::RESOLVED) {
                    continue;
                }
                if (field_inftype.agg) {
                    auto field_agg_inftype = typedef_types[field_inftype.agg];
                    if (field_agg_inftype.type == InferredType::RESOLVED) {
                        field_inftype.type = InferredType::RESOLVED;
                        field_inftype.width = field_agg_inftype.width;
                        field->width = field_inftype.width;
                        changed = true;
                    }
                }
                if (field_inftype.type != InferredType::RESOLVED) {
                    unresolved = true;
                }
            }
            // If all field types are now resolved, sum the width.
            if (!unresolved) {
                type_inftype.width = 0;
                for (auto& field : type->fields) {
                    field->offset = type_inftype.width;
                    type_inftype.width += field->width;
                }
                type_inftype.type = InferredType::RESOLVED;
                type->width = type_inftype.width;
                changed = true;
            }
        }
    }

    // If loop exited due to reaching the iter limit, then we have an error.
    if (changed) {
        Location loc;
        coll->ReportError(loc, ErrorCollector::ERROR, "Could not resolve types: "
                "cycle or too-deep recursion exists");
        return false;
    }
    // If any types are conflicted, report the errors.
    bool have_errors = false;
    for (auto& type_pair : map_) {
        auto* type = type_pair.second;
        if (typedef_types[type].type == InferredType::CONFLICT ||
            typedef_types[type].type == InferredType::UNKNOWN) {
            coll->ReportError(type->loc, ErrorCollector::ERROR,
                    strprintf("CONFLICT or UNKNOWN type: %s (message: %s)",
                        type->ident->name.c_str(),
                        typedef_types[type].conflict_msg.c_str()));
            have_errors = true;
        }
        for (auto& field : type->fields) {
            if (field_types[field.get()].type == InferredType::CONFLICT ||
                field_types[field.get()].type == InferredType::UNKNOWN) {
                coll->ReportError(field->loc, ErrorCollector::ERROR,
                        strprintf("CONFLICT or UNKNOWN type on field: %s.%s "
                                  "(message: %s)",
                                  type->ident->name.c_str(),
                                  field->ident->name.c_str(),
                                  field_types[field.get()].conflict_msg.c_str()));
                have_errors = true;
            }
        }
    }
    return !have_errors;
}

InferredType AggTypeResolver::ResolveType(const ASTType* type) {
    InferredType ret;
    if (type) {
        ret.is_array = type->is_array;
        ret.is_port = type->is_port;
        ret.is_chan = type->is_chan;
        ret.array_size = type->array_length;

#define PRIM(prim_name, width_bits)                                 \
        if (type->ident->name == # prim_name ) {                    \
            ret.type = InferredType::RESOLVED;                      \
            ret.agg = NULL;                                         \
            ret.width = width_bits;                                 \
        }

        PRIM(void, 0);
        PRIM(bool, 1);
        PRIM(int8, 8);
        PRIM(int16, 16);
        PRIM(int32, 32);
        PRIM(int64, 64);

#undef PRIM

        if (ret.type != InferredType::RESOLVED) {
            // Look for an aggregate type defined with this name.
            auto it = map_.find(type->ident->name);
            if (it != map_.end()) {
                ret.agg = it->second;
                ret.type = InferredType::RESOLVED;
                ret.width = ret.agg->width;
            } else {
                ret.type = InferredType::CONFLICT;
                ret.conflict_msg = strprintf(
                        "Type not found: '%s'", type->ident->name.c_str());
            }
        }
    }
    return ret;
}

}  // namesapce frontend
}  // namespace autopiper
