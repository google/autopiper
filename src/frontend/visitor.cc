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

void ASTVisitor::Visit(const AST* ast, ASTVisitorContext* context) const {
    VisitAST(ast, context);
}

// ---------------------- visitor methods --------------------

#define VISIT(type, visit_code)                                     \
    void ASTVisitor::Visit ## type(const type* node,                \
            ASTVisitorContext* context) const {                     \
        context->Visit ## type ## Pre(node);                        \
        visit_code                                                  \
        context->Visit ## type ## Post(node);                       \
    }

VISIT(AST, {
    for (auto& type : node->types) {
        VisitASTTypeDef(type.get(), context);
    }
    for (auto& func : node->functions) {
        VisitASTFunctionDef(func.get(), context);
    }
})

VISIT(ASTFunctionDef, {
    VisitASTIdent(node->name.get(), context);
    VisitASTType(node->return_type.get(), context);
    for (auto& param : node->params) {
        VisitASTParam(param.get(), context);
    }
    VisitASTStmtBlock(node->block.get(), context);
})

VISIT(ASTTypeDef, {
    VisitASTIdent(node->ident.get(), context);
    for (auto& field : node->fields) {
        VisitASTTypeField(field.get(), context);
    }
})

VISIT(ASTIdent, {})  // no subnodes

VISIT(ASTType, {
    VisitASTIdent(node->ident.get(), context);
})

VISIT(ASTParam, {
    VisitASTIdent(node->ident.get(), context);
    VisitASTType(node->type.get(), context);
})

#define T(field, type)                                             \
        if (node-> field ) {                                       \
            VisitASTStmt ## type(node-> field .get(), context);    \
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
})

#undef T

VISIT(ASTStmtBlock, {
    for (auto& stmt : node->stmts) {
        VisitASTStmt(stmt.get(), context);
    }
})

VISIT(ASTStmtLet, {
    VisitASTIdent(node->lhs.get(), context);
    if (node->type) {
        VisitASTType(node->type.get(), context);
    }
    VisitASTExpr(node->rhs.get(), context);
})

VISIT(ASTStmtAssign, {
    VisitASTIdent(node->lhs.get(), context);
    VisitASTExpr(node->rhs.get(), context);
})

VISIT(ASTStmtIf, {
    VisitASTExpr(node->condition.get(), context);
    VisitASTStmt(node->if_body.get(), context);
    if (node->else_body) {
        VisitASTStmt(node->else_body.get(), context);
    }
})

VISIT(ASTStmtWhile, {
    VisitASTExpr(node->condition.get(), context);
    VisitASTStmt(node->body.get(), context);
})

VISIT(ASTStmtBreak, {})

VISIT(ASTStmtContinue, {})

VISIT(ASTStmtWrite, {
    VisitASTIdent(node->port.get(), context);
    VisitASTExpr(node->rhs.get(), context);
})

VISIT(ASTStmtSpawn, {
    VisitASTStmt(node->body.get(), context);
})

VISIT(ASTExpr, {
    for (auto& op : node->ops) {
        VisitASTExpr(op.get(), context);
    }
    if (node->ident) {
        VisitASTIdent(node->ident.get(), context);
    }
    if (node->type) {
        VisitASTType(node->type.get(), context);
    }
})

VISIT(ASTTypeField, {
    VisitASTIdent(node->ident.get(), context);
    VisitASTType(node->type.get(), context);
})

#undef VISIT

// ------------------- modify methods ------------------------

#define MODIFY(type, code_block)                                     \
    ASTRef<type> ASTVisitor::Modify ## type(ASTRef<type> node,       \
        ASTVisitorContext* context) const {                          \
        node = context->Modify ## type ## Pre (move(node));          \
        code_block                                                   \
        node = context->Modify ## type ## Post(move(node));          \
        return node;                                                 \
    }

MODIFY(AST, {
    for (int i = 0; i < node->types.size(); i++) {
        node->types[i] =
            ModifyASTTypeDef(move(node->types[i]), context);
    }
    for (int i = 0; i < node->functions.size(); i++) {
        node->functions[i] =
            ModifyASTFunctionDef(move(node->functions[i]), context);
    }
})

MODIFY(ASTFunctionDef, {
    node->name = ModifyASTIdent(move(node->name), context);
    node->return_type =
        ModifyASTType(move(node->return_type), context);
    for (int i = 0; i < node->params.size(); i++) {
        node->params[i] =
            ModifyASTParam(move(node->params[i]), context);
    }
    node->block =
        ModifyASTStmtBlock(move(node->block), context);
})

MODIFY(ASTTypeDef, {
    node->ident =
        ModifyASTIdent(move(node->ident), context);
    for (int i = 0; i < node->fields.size(); i++) {
        node->fields[i] =
            ModifyASTTypeField(move(node->fields[i]), context);
    }
})

MODIFY(ASTIdent, {})  // no subnodes

MODIFY(ASTType, {
    node->ident =
        ModifyASTIdent(move(node->ident), context);
})

MODIFY(ASTParam, {
    node->ident =
        ModifyASTIdent(move(node->ident), context);
    node->type =
        ModifyASTType(move(node->type), context);
})

#define T(field, type)                                             \
        if (node-> field ) {                                       \
            node-> field =                                         \
                ModifyASTStmt ## type(move(node->field), context); \
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
})

#undef T

MODIFY(ASTStmtBlock, {
    for (int i = 0; i < node->stmts.size(); i++) {
        node->stmts[i] =
            ModifyASTStmt(move(node->stmts[i]), context);
    }
})

MODIFY(ASTStmtLet, {
    node->lhs =
        ModifyASTIdent(move(node->lhs), context);
    if (node->type) {
        node->type =
            ModifyASTType(move(node->type), context);
    }
    node->rhs =
        ModifyASTExpr(move(node->rhs), context);
})

MODIFY(ASTStmtAssign, {
    node->lhs =
        ModifyASTIdent(move(node->lhs), context);
    node->rhs =
        ModifyASTExpr(move(node->rhs), context);
})

MODIFY(ASTStmtIf, {
    node->condition =
        ModifyASTExpr(move(node->condition), context);
    node->if_body =
        ModifyASTStmt(move(node->if_body), context);
    if (node->else_body) {
        node->else_body =
            ModifyASTStmt(move(node->else_body), context);
    }
})

MODIFY(ASTStmtWhile, {
    node->condition =
        ModifyASTExpr(move(node->condition), context);
    node->body =
        ModifyASTStmt(move(node->body), context);
})

MODIFY(ASTStmtBreak, {})

MODIFY(ASTStmtContinue, {})

MODIFY(ASTStmtWrite, {
    node->port =
        ModifyASTIdent(move(node->port), context);
    node->rhs =
        ModifyASTExpr(move(node->rhs), context);
})

MODIFY(ASTStmtSpawn, {
    node->body =
        ModifyASTStmt(move(node->body), context);
})

MODIFY(ASTExpr, {
    for (int i = 0; i < node->ops.size(); i++) {
        node->ops[i] =
            ModifyASTExpr(move(node->ops[i]), context);
    }
    if (node->ident) {
        node->ident =
            ModifyASTIdent(move(node->ident), context);
    }
    if (node->type) {
        node->type =
            ModifyASTType(move(node->type), context);
    }
})

MODIFY(ASTTypeField, {
    node->ident =
        ModifyASTIdent(move(node->ident), context);
    node->type =
        ModifyASTType(move(node->type), context);
})

}  // namespace frontend
}  // namespace autopiper
