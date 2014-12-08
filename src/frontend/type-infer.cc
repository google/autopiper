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

#include "frontend/type-infer.h"

#include <vector>

using namespace std;

namespace autopiper {
namespace frontend {

bool TypeInferPass::HandleBinOpCommonWidth(ASTExpr* node) {
    // TODO
    return true;
}

bool TypeInferPass::CheckArgsArePrimitive(ASTExpr* node) {
    // TODO
    return true;
}

bool TypeInferPass::ModifyASTExprPost(ASTRef<ASTExpr>& node) {
    // Depending on the particular expr op, we may be able to infer either
    // types either upward or downward.
    switch (node->op) {
        case ASTExpr::ADD:
        case ASTExpr::SUB:
        case ASTExpr::AND:
        case ASTExpr::OR:
        case ASTExpr::NOT:
        case ASTExpr::XOR:
            if (!CheckArgsArePrimitive(node.get())) return false;
            if (!HandleBinOpCommonWidth(node.get())) return false;

        case ASTExpr::LE:
        case ASTExpr::LT:
        case ASTExpr::GE:
        case ASTExpr::GT:
            if (!CheckArgsArePrimitive(node.get())) return false;
            if (!HandleBinOpCommonWidth(node.get())) return false;

        case ASTExpr::EQ:
        case ASTExpr::NE:
            if (!HandleBinOpCommonWidth(node.get())) return false;

        case ASTExpr::MUL:
        case ASTExpr::DIV:
        case ASTExpr::REM:

        case ASTExpr::LSH:
        case ASTExpr::RSH:

        case ASTExpr::BITSLICE:
        case ASTExpr::CONCAT:

        case ASTExpr::VAR:

        case ASTExpr::CONST:

        case ASTExpr::FIELD_REF:
        case ASTExpr::ARRAY_REF:

        default:
            break;
    }

    return true;
}

bool TypeInferPass::ModifyASTStmtLetPost(ASTRef<ASTStmtLet>& node) {
    return true;
}

}  // namesapce frontend
}  // namespace autopiper
