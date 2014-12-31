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

#include "frontend/type-lower.h"
#include "common/util.h"

#include <map>

using namespace std;

namespace autopiper {
namespace frontend {

struct FieldLValue {
    ASTExpr* parent_aggregate;
    int offset;

    FieldLValue()
        : parent_aggregate(nullptr), offset(0) {}
    FieldLValue(ASTExpr* p, int offset_)
        : parent_aggregate(p), offset(offset_) {}
};

static bool ComputeLValue(ErrorCollector* coll,
        FieldLValue* ret, ASTExpr* expr, int off = 0) {
    if (expr->op != ASTExpr::FIELD_REF) {
        *ret = FieldLValue(expr, off);
        return true;
    } else {
        // find the field in the type corresponding to the provided identifier.
        if (!expr->ops[0]->inferred_type.agg) {
            coll->ReportError(expr->loc, ErrorCollector::ERROR,
                    "Field ref on non-aggregate type");
            return false;
        }

        ASTTypeDef* t = expr->ops[0]->inferred_type.agg;
        ASTTypeField* field = nullptr;
        for (auto& f : t->fields) {
            if (f->ident->name == expr->ident->name) {
                field = f.get();
                break;
            }
        }

        if (!field) {
            coll->ReportError(expr->loc, ErrorCollector::ERROR,
                    strprintf("Unknown aggregate field: %s",
                        expr->ident->name.c_str()));
            return false;
        }

        // Recurse with the additional bit-offset.
        return ComputeLValue(coll, ret, expr->ops[0].get(),
                off + field->offset);
    }
}

// This desugars an assignment with an LHS that contains a FIELD_REF (or a
// chain of FIELD_REFs), possibly embedded in an ARRAY_REF, into a write of the
// full-width value, constructed as a stmtblock expression that (i) reads the
// entire value, and (ii) uses bitslice and concatenation operators to build
// the new value.
TypeLowerPass::Result
TypeLowerPass::ModifyASTStmtAssignPre(ASTRef<ASTStmtAssign>& node) {
    if (node->lhs->op == ASTExpr::FIELD_REF) {
        // Follow the chain of FIELD_REFs, finding the final offset and width
        // corresponding to the lvalue to which we're assigning and the
        // underlying aggregate expr.
        FieldLValue lvalue;
        if (!ComputeLValue(Errors(), &lvalue, node->lhs->ops[0].get())) {
            return VISIT_END;
        }

        // Check that the aggregate value containing the referenced field is
        // either a bare var reference or an array reference, but nothing else.
        if (lvalue.parent_aggregate->op != ASTExpr::VAR &&
            (lvalue.parent_aggregate->op != ASTExpr::ARRAY_REF ||
             lvalue.parent_aggregate->ops[0]->op != ASTExpr::VAR)) {
            Error(node.get(),
                    "Field assignment must refer to aggregate value either "
                    "in a plain variable or in an array stored in a plain "
                    "variable; deeper nesting is not supported.");
            return VISIT_END;
        }

        // Rewrite the RHS as a STMTBLOCK expression, with a block as follows:
        // {
        //     let temp : agg_type = <clone of LHS's parent aggregate value>;
        //     /* return value = */ { temp[a:b], <original RHS>, temp[c:0] };
        // }
        //
        // Where `a', `b' and `c' are bit-offsets corresponding to the
        // aggregate type's width and the position of the bitfield being
        // replaced.
        ASTRef<ASTExpr> rhs(new ASTExpr());
        rhs->op = ASTExpr::STMTBLOCK;
        rhs->stmt.reset(new ASTStmtBlock());

        unique_ptr<ASTStmt> let_stmt(new ASTStmt());
        let_stmt->let.reset(new ASTStmtLet());
        let_stmt->let->lhs = ASTGenSym(ast_, "field_rmw_temp");
        let_stmt->let->inferred_type = lvalue.parent_aggregate->inferred_type;
        let_stmt->let->rhs = CloneAST(lvalue.parent_aggregate);

        unique_ptr<ASTStmt> expr_stmt(new ASTStmt());
        expr_stmt->expr.reset(new ASTStmtExpr());
        expr_stmt->expr->expr.reset(new ASTExpr());
        expr_stmt->expr->expr->op = ASTExpr::CONCAT;
        expr_stmt->expr->expr->inferred_type =
            lvalue.parent_aggregate->inferred_type;

        int a = lvalue.parent_aggregate->inferred_type.width - 1;
        int b = lvalue.offset + node->lhs->inferred_type.width;
        int c = lvalue.offset - 1;

        if (a >= b) {
            unique_ptr<ASTExpr> upper_bits(new ASTExpr());
            upper_bits->op = ASTExpr::BITSLICE;
            upper_bits->ops.emplace_back(new ASTExpr());
            upper_bits->ops.emplace_back(new ASTExpr());
            upper_bits->ops.emplace_back(new ASTExpr());
            upper_bits->ops[0]->op = ASTExpr::VAR;
            upper_bits->ops[0]->ident = CloneAST(let_stmt->let->lhs.get());
            upper_bits->ops[0]->def = let_stmt->let.get();
            upper_bits->ops[1]->op = ASTExpr::CONST;
            upper_bits->ops[1]->inferred_type = InferredType(32);
            upper_bits->ops[1]->constant = a;
            upper_bits->ops[2]->op = ASTExpr::CONST;
            upper_bits->ops[2]->inferred_type = InferredType(32);
            upper_bits->ops[2]->constant = b;
            upper_bits->inferred_type = InferredType(a - b + 1);
            expr_stmt->expr->expr->ops.push_back(move(upper_bits));
        }

        expr_stmt->expr->expr->ops.push_back(move(node->rhs));

        if (c >= 0) {
            unique_ptr<ASTExpr> lower_bits(new ASTExpr());
            lower_bits->op = ASTExpr::BITSLICE;
            lower_bits->ops.emplace_back(new ASTExpr());
            lower_bits->ops.emplace_back(new ASTExpr());
            lower_bits->ops.emplace_back(new ASTExpr());
            lower_bits->ops[0]->op = ASTExpr::VAR;
            lower_bits->ops[0]->ident = CloneAST(let_stmt->let->lhs.get());
            lower_bits->ops[0]->def = let_stmt->let.get();
            lower_bits->ops[1]->op = ASTExpr::CONST;
            lower_bits->ops[1]->inferred_type = InferredType(32);
            lower_bits->ops[1]->constant = c;
            lower_bits->ops[2]->op = ASTExpr::CONST;
            lower_bits->ops[2]->inferred_type = InferredType(32);
            lower_bits->ops[2]->constant = 0;
            lower_bits->inferred_type = InferredType(c + 1);
            expr_stmt->expr->expr->ops.push_back(move(lower_bits));
        }

        rhs->stmt->stmts.push_back(move(let_stmt));
        rhs->stmt->stmts.push_back(move(expr_stmt));

        node->lhs = CloneAST(lvalue.parent_aggregate);
        node->rhs = move(rhs);
    }

    return VISIT_CONTINUE;
}

TypeLowerPass::Result
TypeLowerPass::ModifyASTExprPost(ASTRef<ASTExpr>& node) {
    if (node->op == ASTExpr::FIELD_REF) {
        // Generate a bitslice operation to extract the requested field.
        FieldLValue lvalue;
        if (!ComputeLValue(Errors(), &lvalue, node.get())) {
            return VISIT_END;
        }

        unique_ptr<ASTExpr> bitslice(new ASTExpr());
        bitslice->op = ASTExpr::BITSLICE;
        bitslice->ops.push_back(CloneAST(lvalue.parent_aggregate));
        bitslice->ops.emplace_back(new ASTExpr());
        bitslice->ops.emplace_back(new ASTExpr());
        bitslice->ops[1]->op = ASTExpr::CONST;
        bitslice->ops[1]->inferred_type = InferredType(32);
        bitslice->ops[1]->constant = lvalue.offset +
            node->inferred_type.width - 1;
        bitslice->ops[2]->op = ASTExpr::CONST;
        bitslice->ops[2]->inferred_type = InferredType(32);
        bitslice->ops[2]->constant = lvalue.offset;
        bitslice->inferred_type = node->inferred_type;

        node = move(bitslice);
    } else if (node->op == ASTExpr::AGGLITERAL) {
        // collect a map of offsets -> field values.
        map<int, unique_ptr<ASTExpr>> field_vals;
        ASTTypeDef* t = node->inferred_type.agg;
        for (auto& field : node->ops) {
            assert(field->op == ASTExpr::AGGLITERALFIELD);
            ASTTypeField* typefield = nullptr;
            for (auto& f : t->fields) {
                if (f->ident->name == field->ident->name) {
                    typefield = f.get();
                    break;
                }
            }
            if (!typefield) {
                Error(node.get(),
                        strprintf("Unknown aggregate field: %s",
                            field->ident->name.c_str()));
                return VISIT_END;
            }

            field_vals[typefield->offset] = move(field->ops[0]);
        }

        // iterate highest->lowest, building a concat operator and filling in
        // gaps with constant zeroes.
        ASTRef<ASTExpr> concat(new ASTExpr());
        concat->op = ASTExpr::CONCAT;

        int last_highest_offset = node->inferred_type.width;
        for (auto ri = field_vals.rbegin(), re = field_vals.rend();
             ri != re; ++ri) {
            int off = ri->first;
            unique_ptr<ASTExpr> value = move(ri->second);

            if (off + value->inferred_type.width < last_highest_offset) {
                // Create a zero.
                ASTRef<ASTExpr> zero(new ASTExpr());
                zero->op = ASTExpr::CONST;
                zero->constant = 0;
                zero->inferred_type =
                    InferredType(
                            last_highest_offset -
                            (off + value->inferred_type.width));
                concat->ops.push_back(move(zero));
            }

            last_highest_offset = off;
            concat->ops.push_back(move(value));
        }

        if (last_highest_offset > 0) {
            // Fill in lowest bits with final zero.
            ASTRef<ASTExpr> zero(new ASTExpr());
            zero->op = ASTExpr::CONST;
            zero->constant = 0;
            zero->inferred_type = InferredType(last_highest_offset);
            concat->ops.push_back(move(zero));
        }

        concat->inferred_type = node->inferred_type;

        node = move(concat);
    }

    return VISIT_CONTINUE;
}

}  // namespace frontend
}  // namespace autopiper
