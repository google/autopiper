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

bool ASTVisitor::Visit(const AST* ast, ASTVisitorContext* context) const {
    return VisitAST(ast, context);
}

ASTRef<AST> ASTVisitor::Modify(ASTRef<AST> ast, ASTVisitorContext* context)
    const {
    return ModifyAST(move(ast), context);
}

// ---------------------- visitor methods --------------------

#define VISIT(type, visit_code)                                     \
    bool ASTVisitor::Visit ## type(const type* node,                \
            ASTVisitorContext* context) const {                     \
        if (!context->Visit ## type ## Pre(node)) return false;     \
        visit_code                                                  \
        if (!context->Visit ## type ## Post(node)) return false;    \
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
})

VISIT(ASTFunctionDef, {
    CHECK(VisitASTIdent(node->name.get(), context));
    CHECK(VisitASTType(node->return_type.get(), context));
    for (auto& param : node->params) {
        CHECK(VisitASTParam(param.get(), context));
    }
    CHECK(VisitASTStmtBlock(node->block.get(), context));
})

VISIT(ASTTypeDef, {
    CHECK(VisitASTIdent(node->ident.get(), context));
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
    CHECK(VisitASTIdent(node->lhs.get(), context));
    CHECK(VisitASTExpr(node->rhs.get(), context));
})

VISIT(ASTStmtIf, {
    CHECK(VisitASTExpr(node->condition.get(), context));
    CHECK(VisitASTStmt(node->if_body.get(), context));
    if (node->else_body) {
        CHECK(VisitASTStmt(node->else_body.get(), context));
    }
})

VISIT(ASTStmtWhile, {
    CHECK(VisitASTExpr(node->condition.get(), context));
    CHECK(VisitASTStmt(node->body.get(), context));
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
    CHECK(VisitASTIdent(node->port.get(), context));
    CHECK(VisitASTExpr(node->rhs.get(), context));
})

VISIT(ASTStmtSpawn, {
    CHECK(VisitASTStmt(node->body.get(), context));
})

VISIT(ASTStmtReturn, {
    CHECK(VisitASTExpr(node->value.get(), context));
})

VISIT(ASTExpr, {
    for (auto& op : node->ops) {
        CHECK(VisitASTExpr(op.get(), context));
    }
    if (node->ident) {
        CHECK(VisitASTIdent(node->ident.get(), context));
    }
    if (node->type) {
        CHECK(VisitASTType(node->type.get(), context));
    }
})

VISIT(ASTTypeField, {
    CHECK(VisitASTIdent(node->ident.get(), context));
    CHECK(VisitASTType(node->type.get(), context));
})

#undef CHECK
#undef VISIT

// ------------------- modify methods ------------------------

#define MODIFY(type, code_block)                                     \
    ASTRef<type> ASTVisitor::Modify ## type(ASTRef<type> node,       \
        ASTVisitorContext* context) const {                          \
        node = context->Modify ## type ## Pre (move(node));          \
        if (!node) return node;                                      \
        code_block                                                   \
        node = context->Modify ## type ## Post(move(node));          \
        return node;                                                 \
    }

#define CHECK(dest, value) do {                                      \
    dest = value;                                                    \
    if (!dest) return nullptr;                                       \
} while (0)

MODIFY(AST, {
    for (int i = 0; i < node->types.size(); i++) {
        CHECK(node->types[i],
                ModifyASTTypeDef(move(node->types[i]), context));
    }
    for (int i = 0; i < node->functions.size(); i++) {
        CHECK(node->functions[i],
                ModifyASTFunctionDef(move(node->functions[i]), context));
    }
})

MODIFY(ASTFunctionDef, {
    CHECK(node->name, ModifyASTIdent(move(node->name), context));
    CHECK(node->return_type,
            ModifyASTType(move(node->return_type), context));
    for (int i = 0; i < node->params.size(); i++) {
        CHECK(node->params[i],
                ModifyASTParam(move(node->params[i]), context));
    }
    CHECK(node->block,
            ModifyASTStmtBlock(move(node->block), context));
})

MODIFY(ASTTypeDef, {
    CHECK(node->ident,
            ModifyASTIdent(move(node->ident), context));
    for (int i = 0; i < node->fields.size(); i++) {
        CHECK(node->fields[i],
                ModifyASTTypeField(move(node->fields[i]), context));
    }
})

MODIFY(ASTIdent, {})  // no subnodes

MODIFY(ASTType, {
    CHECK(node->ident,
            ModifyASTIdent(move(node->ident), context));
})

MODIFY(ASTParam, {
    CHECK(node->ident,
            ModifyASTIdent(move(node->ident), context));
    CHECK(node->type,
            ModifyASTType(move(node->type), context));
})

#define T(field, type)                                                  \
        if (node-> field ) {                                            \
            CHECK(node-> field,                                         \
                    ModifyASTStmt ## type(move(node->field), context)); \
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
})

#undef T

MODIFY(ASTStmtBlock, {
    for (int i = 0; i < node->stmts.size(); i++) {
        CHECK(node->stmts[i],
                ModifyASTStmt(move(node->stmts[i]), context));
    }
})

MODIFY(ASTStmtLet, {
    CHECK(node->lhs,
            ModifyASTIdent(move(node->lhs), context));
    if (node->type) {
        CHECK(node->type,
                ModifyASTType(move(node->type), context));
    }
    CHECK(node->rhs,
            ModifyASTExpr(move(node->rhs), context));
})

MODIFY(ASTStmtAssign, {
    CHECK(node->lhs,
            ModifyASTIdent(move(node->lhs), context));
    CHECK(node->rhs,
            ModifyASTExpr(move(node->rhs), context));
})

MODIFY(ASTStmtIf, {
    CHECK(node->condition,
            ModifyASTExpr(move(node->condition), context));
    CHECK(node->if_body,
            ModifyASTStmt(move(node->if_body), context));
    if (node->else_body) {
        CHECK(node->else_body,
                ModifyASTStmt(move(node->else_body), context));
    }
})

MODIFY(ASTStmtWhile, {
    CHECK(node->condition,
            ModifyASTExpr(move(node->condition), context));
    CHECK(node->body,
            ModifyASTStmt(move(node->body), context));
    if (node->label) {
        CHECK(node->label,
                ModifyASTIdent(move(node->label), context));
    }
})

MODIFY(ASTStmtBreak, {
    if (node->label) {
        CHECK(node->label,
                ModifyASTIdent(move(node->label), context));
    }
})

MODIFY(ASTStmtContinue, {
    if (node->label) {
        CHECK(node->label,
                ModifyASTIdent(move(node->label), context));
    }
})

MODIFY(ASTStmtWrite, {
    CHECK(node->port,
            ModifyASTIdent(move(node->port), context));
    CHECK(node->rhs,
            ModifyASTExpr(move(node->rhs), context));
})

MODIFY(ASTStmtSpawn, {
    CHECK(node->body,
            ModifyASTStmt(move(node->body), context));
})

MODIFY(ASTStmtReturn, {
    CHECK(node->value,
            ModifyASTExpr(move(node->value), context));
})

MODIFY(ASTExpr, {
    for (int i = 0; i < node->ops.size(); i++) {
        CHECK(node->ops[i],
                ModifyASTExpr(move(node->ops[i]), context));
    }
    if (node->ident) {
        CHECK(node->ident,
                ModifyASTIdent(move(node->ident), context));
    }
    if (node->type) {
        CHECK(node->type,
                ModifyASTType(move(node->type), context));
    }
})

MODIFY(ASTTypeField, {
    CHECK(node->ident,
            ModifyASTIdent(move(node->ident), context));
    CHECK(node->type,
            ModifyASTType(move(node->type), context));
})

}  // namespace frontend
}  // namespace autopiper
