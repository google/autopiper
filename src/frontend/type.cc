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

#include "frontend/type.h"
#include "frontend/ast.h"
#include "common/util.h"

#include <string>
#include <sstream>

using namespace std;

namespace autopiper {
namespace frontend {

InferredType InferredType::Meet(const InferredType& other) const {
    if (this->type == InferredType::UNKNOWN) {
        return other;
    } else if (other.type == InferredType::UNKNOWN) {
        return *this;
    } else if (this->type == InferredType::CONFLICT ||
               other.type == InferredType::CONFLICT) {
        InferredType ret;
        ret.type = InferredType::CONFLICT;
        ret.conflict_msg = this->conflict_msg + "; " + other.conflict_msg;
        return ret;
    } else if (this->type == InferredType::EXPANDING_CONST &&
               other.type == InferredType::EXPANDING_CONST) {
        InferredType ret = *this;
        ret.width = other.width > this->width ? other.width : this->width;
        return ret;
    } else if (this->type == InferredType::EXPANDING_CONST ||
               other.type == InferredType::EXPANDING_CONST) {
        InferredType const_type;
        InferredType resolved_type;
        if (this->type == InferredType::EXPANDING_CONST) {
            const_type = *this;
            resolved_type = other;
        } else {
            const_type = other;
            resolved_type = *this;
        }
        // We meet to RESOLVED unless we're too wide.
        if (resolved_type.width < const_type.width) {
            InferredType conflict;
            conflict.type = InferredType::CONFLICT;
            conflict.conflict_msg = strprintf(
                    "Type conflict: resolved type of width %d met a "
                    "constant with minimum width %d",
                    resolved_type.width, const_type.width);
            return conflict;
        }
        return resolved_type;
    } else {
        // both in RESOLVED state
        if (this->agg == other.agg && this->width == other.width &&
            this->is_port == other.is_port && this->is_array == other.is_array) {
            return *this;
        } else {
            InferredType conflict;
            conflict.type = InferredType::CONFLICT;
            conflict.conflict_msg = strprintf(
                    "Type conflict: aggregate type '%s' vs. '%s', width "
                    "%d vs. %d, port %d vs. %d, array %d vs. %d",
                    this->agg ? this->agg->ident->name.c_str() : "",
                    other.agg ? other.agg->ident->name.c_str() : "",
                    this->width, other.width, this->is_port, other.is_port,
                    this->is_array, other.is_array);
            return conflict;
        }
    }
}

bool InferredType::operator==(const InferredType& other) const {
    return type == other.type &&
           agg == other.agg &&
           width == other.width &&
           is_port == other.is_port &&
           is_array == other.is_array &&
           conflict_msg == other.conflict_msg;
}

string InferredType::ToString() const {
    ostringstream os;
    switch (type) {
        case UNKNOWN: os << "UNKNOWN("; break;
        case RESOLVED: os << "RESOLVED("; break;
        case EXPANDING_CONST: os << "EXPANDING_CONST("; break;
        case CONFLICT: os << "CONFLICT("; break;
    }
    if (agg) {
        os << "AggType=" << agg->ident->name << ",";
    }
    os << "Width=" << width;
    if (is_port) {
        os << ",Port";
    }
    if (is_array) {
        os << ",Array";
    }
    if (type == CONFLICT) {
        os << ",Conflict=" << conflict_msg;
    }
    os << ")";
    return os.str();
}

}  // namespace frontend
}  // namesapce autopiper
