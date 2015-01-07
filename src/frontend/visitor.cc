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

#include "frontend/visitor.h"

using namespace std;

namespace autopiper {
namespace frontend {

// ---------------------- visitor methods --------------------

#define VISIT(type, visit_code)                                     \
    bool ASTVisitor::Visit ## type(const type* node,                \
            ASTVisitorContext* context) const {                     \
        auto res = context->Visit ## type ## Pre(node);             \
        if (res == ASTVisitorContext::VISIT_END) return false;      \
        if (res == ASTVisitorContext::VISIT_CONTINUE) {             \
            visit_code                                              \
        }                                                           \
        res = context->Visit ## type ## Post(node);                 \
        if (res == ASTVisitorContext::VISIT_END) return false;      \
        return true;                                                \
    }

#define CHECK(stmt) if (!stmt) { return false; }

VISIT(AST, {
    for (auto& type : node->types) {
        CHECK(VisitASTTypeDef(type.get(), context));
    }
    for (auto& func : node->functions) {
        CHECK(VisitASTFunctionDef(func.get(), context));
    }
    for (auto& pragma : node->pragmas) {
        CHECK(VisitASTPragma(pragma.get(), context));
    }
})

VISIT(ASTFunctionDef, {
    CHECK(VisitASTIdent(node->name.get(), context));
    CHECK(VisitASTType(node->return_type.get(), context));
    for (auto& param : node->params) {
        CHECK(VisitASTParam(param.get(), context));
    }
    if (node->block) {
        CHECK(VisitASTStmtBlock(node->block.get(), context));
    }
})

VISIT(ASTTypeDef, {
    CHECK(VisitASTIdent(node->ident.get(), context));
    if (node->alias) {
        CHECK(VisitASTType(node->alias.get(), context));
    }
    for (auto& field : node->fields) {
        CHECK(VisitASTTypeField(field.get(), context));
    }
})

VISIT(ASTIdent, {})  // no subnodes

VISIT(ASTType, {
    CHECK(VisitASTIdent(node->ident.get(), context));
})

VISIT(ASTParam, {
    CHECK(VisitASTIdent(node->ident.get(), context));
    CHECK(VisitASTType(node->type.get(), context));
})

#define T(field, type)                                                 \
        if (node-> field ) {                                           \
            CHECK(VisitASTStmt ## type(node-> field .get(), context)); \
        }

VISIT(ASTStmt, {
    T(block, Block)
    T(let, Let)
    T(assign, Assign)
    T(if_, If)
    T(while_, While)
    T(break_, Break)
    T(continue_, Continue)
    T(write, Write)
    T(spawn, Spawn)
    T(return_, Return)
    T(kill, Kill)
    T(killyounger, KillYounger)
    T(killif, KillIf)
    T(timing, Timing)
    T(stage, Stage)
    T(expr, Expr)
    T(nested, NestedFunc)
    T(onkillyounger, OnKillYounger)
})

#undef T

VISIT(ASTStmtBlock, {
    for (auto& stmt : node->stmts) {
        CHECK(VisitASTStmt(stmt.get(), context));
    }
})

VISIT(ASTStmtLet, {
    CHECK(VisitASTIdent(node->lhs.get(), context));
    if (node->type) {
        CHECK(VisitASTType(node->type.get(), context));
    }
    CHECK(VisitASTExpr(node->rhs.get(), context));
})

VISIT(ASTStmtAssign, {
    CHECK(VisitASTExpr(node->lhs.get(), context));
    CHECK(VisitASTExpr(node->rhs.get(), context));
})

VISIT(ASTStmtIf, {
    if (node->condition) {
        CHECK(VisitASTExpr(node->condition.get(), context));
    }
    if (node->if_body) {
        CHECK(VisitASTStmt(node->if_body.get(), context));
    }
    if (node->else_body) {
        CHECK(VisitASTStmt(node->else_body.get(), context));
    }
})

VISIT(ASTStmtWhile, {
    if (node->condition) {
        CHECK(VisitASTExpr(node->condition.get(), context));
    }
    if (node->body) {
        CHECK(VisitASTStmt(node->body.get(), context));
    }
    if (node->label) {
        CHECK(VisitASTIdent(node->label.get(), context));
    }
})

VISIT(ASTStmtBreak, {
    if (node->label) {
        CHECK(VisitASTIdent(node->label.get(), context));
    }
})

VISIT(ASTStmtContinue, {
    if (node->label) {
        CHECK(VisitASTIdent(node->label.get(), context));
    }
})

VISIT(ASTStmtWrite, {
    CHECK(VisitASTExpr(node->port.get(), context));
    CHECK(VisitASTExpr(node->rhs.get(), context));
})

VISIT(ASTStmtSpawn, {
    if (node->body) {
        CHECK(VisitASTStmt(node->body.get(), context));
    }
})

VISIT(ASTStmtReturn, {
    CHECK(VisitASTExpr(node->value.get(), context));
})

VISIT(ASTStmtKill, {})

VISIT(ASTStmtKillYounger, {})

VISIT(ASTStmtKillIf, {
    if (node->condition) {
        CHECK(VisitASTExpr(node->condition.get(), context));
    }
})

VISIT(ASTStmtTiming, {
    if (node->body) {
        CHECK(VisitASTStmt(node->body.get(), context));
    }
})

VISIT(ASTStmtStage, {})

VISIT(ASTStmtExpr, {
    CHECK(VisitASTExpr(node->expr.get(), context));
})

VISIT(ASTStmtNestedFunc, {
    CHECK(VisitASTStmtBlock(node->body.get(), context));
})

VISIT(ASTStmtOnKillYounger, {
    CHECK(VisitASTStmtBlock(node->body.get(), context));
})

VISIT(ASTExpr, {
    for (auto& op : node->ops) {
        CHECK(VisitASTExpr(op.get(), context));
    }
    if (node->ident) {
        CHECK(VisitASTIdent(node->ident.get(), context));
    }
    if (node->stmt) {
        CHECK(VisitASTStmtBlock(node->stmt.get(), context));
    }
})

VISIT(ASTTypeField, {
    CHECK(VisitASTIdent(node->ident.get(), context));
    CHECK(VisitASTType(node->type.get(), context));
})

VISIT(ASTPragma, {})

#undef CHECK
#undef VISIT

// ------------------- modify methods ------------------------

#define MODIFY(type, code_block)                                           \
    bool ASTVisitor::Modify ## type(ASTRef<type>& node,                    \
        ASTVisitorContext* context) const {                                \
        auto res = context->Modify ## type ## Pre (node);                  \
        if (res == ASTVisitorContext::VISIT_END) {                         \
            return false;                                                  \
        }                                                                  \
        if (res == ASTVisitorContext::VISIT_CONTINUE) {                    \
            code_block                                                     \
        }                                                                  \
        res = context->Modify ## type ## Post(node);                       \
        if (res == ASTVisitorContext::VISIT_END) {                         \
            return false;                                                  \
        }                                                                  \
        return true;                                                       \
    }

#define FIELD(dest, type) do {                                       \
    if (!Modify ## type(dest, context)) {                            \
        return false;                                                \
    }                                                                \
} while (0)

MODIFY(AST, {
    for (int i = 0; i < node->types.size(); i++) {
        FIELD(node->types[i], ASTTypeDef);
    }
    for (int i = 0; i < node->functions.size(); i++) {
        FIELD(node->functions[i], ASTFunctionDef);
    }
    for (int i = 0; i < node->pragmas.size(); i++) {
        FIELD(node->pragmas[i], ASTPragma);
    }
})

MODIFY(ASTFunctionDef, {
    FIELD(node->name, ASTIdent);
    FIELD(node->return_type, ASTType);
    for (int i = 0; i < node->params.size(); i++) {
        FIELD(node->params[i], ASTParam);
    }
    if (node->block) {
        FIELD(node->block, ASTStmtBlock);
    }
})

MODIFY(ASTTypeDef, {
    FIELD(node->ident, ASTIdent);
    if (node->alias) {
        FIELD(node->alias, ASTType);
    }
    for (int i = 0; i < node->fields.size(); i++) {
        FIELD(node->fields[i], ASTTypeField);
    }
})

MODIFY(ASTIdent, {})  // no subnodes

MODIFY(ASTType, {
    FIELD(node->ident, ASTIdent);
})

MODIFY(ASTParam, {
    FIELD(node->ident, ASTIdent);
    FIELD(node->type, ASTType);
})

#define T(field, type)                                                  \
        if (node->field) {                                              \
            FIELD(node->field, ASTStmt ## type);                        \
        }

MODIFY(ASTStmt, {
    T(block, Block)
    T(let, Let)
    T(assign, Assign)
    T(if_, If)
    T(while_, While)
    T(break_, Break)
    T(continue_, Continue)
    T(write, Write)
    T(spawn, Spawn)
    T(return_, Return)
    T(kill, Kill)
    T(killyounger, KillYounger)
    T(killif, KillIf)
    T(timing, Timing)
    T(stage, Stage)
    T(expr, Expr)
    T(nested, NestedFunc)
    T(onkillyounger, OnKillYounger)
})

#undef T

MODIFY(ASTStmtBlock, {
    for (int i = 0; i < node->stmts.size(); i++) {
        FIELD(node->stmts[i], ASTStmt);
    }
})

MODIFY(ASTStmtLet, {
    FIELD(node->lhs, ASTIdent);
    if (node->type) {
        FIELD(node->type, ASTType);
    }
    FIELD(node->rhs, ASTExpr);
})

MODIFY(ASTStmtAssign, {
    FIELD(node->lhs, ASTExpr);
    FIELD(node->rhs, ASTExpr);
})

MODIFY(ASTStmtIf, {
    if (node->condition) {
        FIELD(node->condition, ASTExpr);
    }
    if (node->if_body) {
        FIELD(node->if_body, ASTStmt);
    }
    if (node->else_body) {
        FIELD(node->else_body, ASTStmt);
    }
})

MODIFY(ASTStmtWhile, {
    if (node->condition) {
        FIELD(node->condition, ASTExpr);
    }
    if (node->body) {
        FIELD(node->body, ASTStmt);
    }
    if (node->label) {
        FIELD(node->label, ASTIdent);
    }
})

MODIFY(ASTStmtBreak, {
    if (node->label) {
        FIELD(node->label, ASTIdent);
    }
})

MODIFY(ASTStmtContinue, {
    if (node->label) {
        FIELD(node->label, ASTIdent);
    }
})

MODIFY(ASTStmtWrite, {
    FIELD(node->port, ASTExpr);
    FIELD(node->rhs, ASTExpr);
})

MODIFY(ASTStmtSpawn, {
    if (node->body) {
        FIELD(node->body, ASTStmt);
    }
})

MODIFY(ASTStmtReturn, {
    FIELD(node->value, ASTExpr);
})

MODIFY(ASTStmtKill, {})

MODIFY(ASTStmtKillYounger, {})

MODIFY(ASTStmtKillIf, {
    FIELD(node->condition, ASTExpr);
})

MODIFY(ASTStmtTiming, {
    if (node->body) {
        FIELD(node->body, ASTStmt);
    }
})

MODIFY(ASTStmtStage, {})

MODIFY(ASTStmtExpr, {
    FIELD(node->expr, ASTExpr);
})

MODIFY(ASTStmtNestedFunc, {
    FIELD(node->body, ASTStmtBlock);
})

MODIFY(ASTStmtOnKillYounger, {
    FIELD(node->body, ASTStmtBlock);
})

MODIFY(ASTExpr, {
    for (unsigned i = 0; i < node->ops.size(); i++) {
        FIELD(node->ops[i], ASTExpr);
    }
    if (node->ident) {
        FIELD(node->ident, ASTIdent);
    }
    if (node->stmt) {
        FIELD(node->stmt, ASTStmtBlock);
    }
})

MODIFY(ASTTypeField, {
    FIELD(node->ident, ASTIdent);
    FIELD(node->type, ASTType);
})

MODIFY(ASTPragma, {})

}  // namespace frontend
}  // namespace autopiper
