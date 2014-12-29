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

#include "frontend/ast.h"
#include "common/util.h"

#include <iostream>
#include <string>

using namespace std;

namespace autopiper {
namespace frontend {

static string Indent(int indent) {
    string ret;
    for (int i = 0; i < indent; i++) {
        ret += "  ";
    }
    return ret;
}

#define AST_PRINTER(type)                                                      \
    template<>                                                                 \
    void PrintAST<type>(const type* node, ostream& out, int indent)

#define I(incr) Indent(indent + incr)
#define P(subnode, incr) PrintAST(subnode, out, indent + incr)

AST_PRINTER(AST) {
    out << I(0) << "(ast " << node << endl;
    out << I(1) << "(gencounter " << node->gencounter << ")" << endl;
    for (auto& type : node->types) {
        P(type.get(), 1);
    }
    for (auto& func : node->functions) {
        P(func.get(), 1);
    }
    out << I(0) << ")" << endl;
}

AST_PRINTER(ASTFunctionDef) {
    out << I(0) << "(func " << node << endl;

    out << I(1) << "(name ";
    P(node->name.get(), 2);
    out << ")" << endl;
    if (node->is_entry) {
        out << I(1) << "(entry)" << endl;
    }

    out << I(1) << "(return_type ";
    P(node->return_type.get(), 2);
    out << ")" << endl;

    out << I(1) << "(params" << endl;
    for (auto& param : node->params) {
        P(param.get(), 2);
    }
    out << I(1) << ")" << endl;

    P(node->block.get(), 1);
}

AST_PRINTER(ASTTypeDef) {
    out << I(0) << "(typedef " << node << endl;
    out << I(1) << "(ident ";
    P(node->ident.get(), 1);
    out << ")" << endl;
    for (auto& field : node->fields) {
        P(field.get(), 1);
    }
}

AST_PRINTER(ASTIdent) {
    out << "(ident \"" << node->name << "\" ";
    switch (node->type) {
        case ASTIdent::FUNC: out << "FUNC"; break;
        case ASTIdent::VAR: out << "VAR"; break;
        case ASTIdent::TYPE: out << "TYPE"; break;
        case ASTIdent::FIELD: out << "FIELD"; break;
        case ASTIdent::PORT: out << "PORT"; break;
    }
    out << ")";
}

AST_PRINTER(ASTType) {
    out << "(type ";
    P(node->ident.get(), 0);
    if (node->is_port) {
        out << " PORT";
    }
    out << ")";
}

AST_PRINTER(ASTParam) {
    out << I(0) << "(param" << endl;
    out << I(1) << "(ident ";
    P(node->ident.get(), 2);
    out << ")" << endl;
    out << I(1) << "(type ";
    P(node->type.get(), 2);
    out << ")" << endl;
    out << I(0) << ")" << endl;
}

AST_PRINTER(ASTStmt) {
    out << I(0) << "(stmt " << node << endl;
#define T(type)                 \
    if (node->type)             \
        P(node->type.get(), 1)
    T(block);
    T(let);
    T(assign);
    T(if_);
    T(while_);
    T(break_);
    T(continue_);
    T(write);
    T(spawn);
    T(return_);
    T(kill);
    T(killyounger);
    T(killif);
    T(timing);
    T(stage);
    T(expr);
#undef T
    out << I(0) << ")" << endl;
}

AST_PRINTER(ASTStmtBlock) {
    out << I(0) << "(stmt-block " << node << endl;
    for (auto& stmt : node->stmts) {
        P(stmt.get(), 1);
    }
    out << I(0) << ")" << endl;
}

AST_PRINTER(ASTStmtLet) {
    out << I(0) << "(stmt-let " << node << endl;
    out << I(1) << "(lhs ";
    P(node->lhs.get(), 2);
    out << ")" << endl;
    if (node->type) {
        out << I(1) << "(type ";
        P(node->type.get(), 2);
        out << ")" << endl;
    }
    out << I(1) << "(rhs" << endl;
    P(node->rhs.get(), 2);
    out << I(1) << ")" << endl;
    out << I(0) << ")" << endl;
}

AST_PRINTER(ASTStmtAssign) {
    out << I(0) << "(stmt-assign " << node << endl;
    out << I(1) << "(lhs " << endl;
    P(node->lhs.get(), 2);
    out << I(1) << ")" << endl;
    out << I(1) << "(rhs" << endl;
    P(node->rhs.get(), 2);
    out << I(1) << ")" << endl;
    out << I(0) << ")" << endl;
}

AST_PRINTER(ASTStmtIf) {
    out << I(0) << "(stmt-if " << node << endl;
    out << I(1) << "(condition" << endl;
    P(node->condition.get(), 2);
    out << I(1) << ")" << endl;
    out << I(1) << "(if-body" << endl;
    P(node->if_body.get(), 2);
    out << I(1) << ")" << endl;
    if (node->else_body) {
        out << I(1) << "(else-body" << endl;
        P(node->else_body.get(), 2);
        out << I(1) << ")" << endl;
    }
    out << I(0) << ")" << endl;
}

AST_PRINTER(ASTStmtWhile) {
    out << I(0) << "(stmt-while " << node << endl;
    if (node->label) {
        out << I(1) << "(label ";
        P(node->label.get(), 2);
        out << ")" << endl;
    }
    out << I(1) << "(condition" << endl;
    P(node->condition.get(), 2);
    out << I(1) << ")" << endl;
    out << I(1) << "(body" << endl;
    P(node->body.get(), 2);
    out << I(1) << ")" << endl;
    out << I(0) << ")" << endl;
}

AST_PRINTER(ASTStmtBreak) {
    out << I(0) << "(stmt-break " << node << " ";
    if (node->label) {
        out << " ";
        P(node->label.get(), 1);
    }
    out << ")" << endl;
}

AST_PRINTER(ASTStmtContinue) {
    out << I(0) << "(stmt-continue " << node << " ";
    if (node->label) {
        out << " ";
        P(node->label.get(), 1);
    }
    out << ")" << endl;
}

AST_PRINTER(ASTStmtWrite) {
    out << I(0) << "(stmt-write " << node << endl;
    out << I(1) << "(port" << endl;
    P(node->port.get(), 2);
    out << I(1) << ")" << endl;;
    out << I(1) << "(rhs" << endl;
    P(node->rhs.get(), 2);
    out << I(1) << ")" << endl;
    out << I(0) << ")" << endl;
}

AST_PRINTER(ASTStmtSpawn) {
    out << I(0) << "(stmt-spawn " << node << endl;
    P(node->body.get(), 1);
    out << I(0) << ")" << endl;
}

AST_PRINTER(ASTStmtReturn) {
    out << I(0) << "(stmt-return " << node << endl;
    P(node->value.get(), 1);
    out << I(0) << ")" << endl;
}

AST_PRINTER(ASTStmtKill) {
    out << I(0) << "(stmt-kill " << node << ")" << endl;
}

AST_PRINTER(ASTStmtKillYounger) {
    out << I(0) << "(stmt-killyounger " << node << ")" << endl;
}

AST_PRINTER(ASTStmtKillIf) {
    out << I(0) << "(stmt-kill-if " << node << endl;
    P(node->condition.get(), 1);
    out << I(0) << ")" << endl;
}

AST_PRINTER(ASTStmtTiming) {
    out << I(0) << "(stmt-timing " << node << endl;
    P(node->body.get(), 1);
    out << I(0) << ")" << endl;
}

AST_PRINTER(ASTStmtStage) {
    out << I(0) << "(stmt-stage " << node << " "
        << node->offset << ")" << endl;
}

AST_PRINTER(ASTStmtExpr) {
    out << I(0) << "(stmt-expr " << node << endl;
    P(node->expr.get(), 1);
    out << I(0) << ")" << endl;
}

AST_PRINTER(ASTExpr) {
    out << I(0) << "(expr " << node << " ";
    switch (node->op) {
#define T(type)               \
        case ASTExpr:: type : \
            out << #type;     \
            break

        T(ADD);
        T(SUB);
        T(MUL);
        T(DIV);
        T(REM);
        T(AND);
        T(OR);
        T(NOT);
        T(XOR);
        T(LSH);
        T(RSH);
        T(SEL);
        T(BITSLICE);
        T(CONCAT);

        T(EQ);
        T(NE);
        T(LE);
        T(LT);
        T(GE);
        T(GT);

        T(VAR);
        T(CONST);
        T(FIELD_REF);
        T(ARRAY_REF);
        T(ARG);
        T(AGGLITERAL);
        T(AGGLITERALFIELD);

        T(FUNCCALL);

        T(PORTREAD);
        T(PORTDEF);

        T(STMTBLOCK);

        T(NOP);
#undef T
    }

    out << endl;

    if (node->op == ASTExpr::CONST) {
        out << I(1) << "(const " << node->constant << ")" << endl;
    }
    if (node->ident) {
        out << I(1) << "(ident ";
        P(node->ident.get(), 2);
        out << ")" << endl;
    }
    for (auto& op : node->ops) {
        out << I(1) << "(op" << endl;
        P(op.get(), 2);
        out << I(1) << ")" << endl;
    }
    if (node->stmt) {
        out << I(1) << "(stmt" << endl;
        P(node->stmt.get(), 2);
        out << I(1) << ")" << endl;
    }
    if (node->inferred_type.type == InferredType::RESOLVED ||
        node->inferred_type.type == InferredType::EXPANDING_CONST) {
        out << I(1) << "(inferred_type " << node->inferred_type.ToString()
            << ")" << endl;
    }
    out << I(0) << ")" << endl;
}

AST_PRINTER(ASTTypeField) {
    out << I(0) << "(typefield" << endl;
    out << I(1) << "(ident ";
    P(node->ident.get(), 2);
    out << ")" << endl;
    out << I(1) << "(type ";
    P(node->type.get(), 2);
    out << ")" << endl;
    out << I(0) << ")" << endl;
}

#undef P
#undef I
#undef AST_PRINTER

template<typename T>
ASTVector<T> CloneVec(const ASTVector<T>& orig) {
    ASTVector<T> ret;
    for (const auto& node : orig) {
        ret.push_back(move(CloneAST<T>(node.get())));
    }
    return ret;
}

#define AST_CLONE(type) \
    template<> ASTRef<type> CloneAST<type>(const type* node)
#define SETUP(type)                                                           \
    ASTRef<type> ret(new type());                                             \
    ret->loc = node->loc
#define SUB(name) if (node->name) ret->name = CloneAST(node->name.get())
#define PRIM(name) ret->name = node->name
#define VEC(name) ret->name = CloneVec(node->name)

AST_CLONE(AST) {
    SETUP(AST);
    VEC(functions);
    VEC(types);
    PRIM(gencounter);
    return ret;
}

AST_CLONE(ASTFunctionDef) {
    SETUP(ASTFunctionDef);
    SUB(name);
    SUB(return_type);
    VEC(params);
    SUB(block);
    return ret;
}

AST_CLONE(ASTParam) {
    SETUP(ASTParam);
    SUB(ident);
    SUB(type);
    return ret;
}

AST_CLONE(ASTTypeDef) {
    SETUP(ASTTypeDef);
    SUB(ident);
    VEC(fields);
    return ret;
}

AST_CLONE(ASTIdent) {
    SETUP(ASTIdent);
    PRIM(name);
    PRIM(type);
    return ret;
}

AST_CLONE(ASTType) {
    SETUP(ASTType);
    SUB(ident);
    PRIM(is_port);
    return ret;
}

AST_CLONE(ASTStmt) {
    SETUP(ASTStmt);
    SUB(block);
    SUB(let);
    SUB(assign);
    SUB(if_);
    SUB(while_);
    SUB(break_);
    SUB(continue_);
    SUB(write);
    SUB(spawn);
    SUB(return_);
    SUB(kill);
    SUB(killyounger);
    SUB(killif);
    SUB(timing);
    SUB(stage);
    SUB(expr);
    return ret;
}

AST_CLONE(ASTStmtBlock) {
    SETUP(ASTStmtBlock);
    VEC(stmts);
    return ret;
}

AST_CLONE(ASTStmtLet) {
    SETUP(ASTStmtLet);
    SUB(lhs);
    SUB(type);
    SUB(rhs);
    return ret;
}

AST_CLONE(ASTStmtAssign) {
    SETUP(ASTStmtAssign);
    SUB(lhs);
    SUB(rhs);
    return ret;
}

AST_CLONE(ASTStmtIf) {
    SETUP(ASTStmtIf);
    SUB(condition);
    SUB(if_body);
    SUB(else_body);
    return ret;
}

AST_CLONE(ASTStmtWhile) {
    SETUP(ASTStmtWhile);
    SUB(condition);
    SUB(body);
    SUB(label);
    return ret;
}

AST_CLONE(ASTStmtBreak) {
    SETUP(ASTStmtBreak);
    SUB(label);
    return ret;
}

AST_CLONE(ASTStmtContinue) {
    SETUP(ASTStmtContinue);
    SUB(label);
    return ret;
}

AST_CLONE(ASTStmtWrite) {
    SETUP(ASTStmtWrite);
    SUB(port);
    SUB(rhs);
    return ret;
}

AST_CLONE(ASTStmtSpawn) {
    SETUP(ASTStmtSpawn);
    SUB(body);
    return ret;
}

AST_CLONE(ASTStmtReturn) {
    SETUP(ASTStmtReturn);
    SUB(value);
    return ret;
}

AST_CLONE(ASTStmtKill) {
    SETUP(ASTStmtKill);
    return ret;
}

AST_CLONE(ASTStmtKillYounger) {
    SETUP(ASTStmtKillYounger);
    return ret;
}

AST_CLONE(ASTStmtKillIf) {
    SETUP(ASTStmtKillIf);
    SUB(condition);
    return ret;
}

AST_CLONE(ASTStmtTiming) {
    SETUP(ASTStmtTiming);
    SUB(body);
    return ret;
}

AST_CLONE(ASTStmtStage) {
    SETUP(ASTStmtStage);
    PRIM(offset);
    return ret;
}

AST_CLONE(ASTStmtExpr) {
    SETUP(ASTStmtExpr);
    SUB(expr);
    return ret;
}

AST_CLONE(ASTExpr) {
    SETUP(ASTExpr);
    PRIM(op);
    VEC(ops);
    SUB(ident);
    PRIM(constant);
    return ret;
}

AST_CLONE(ASTTypeField) {
    SETUP(ASTTypeField);
    SUB(ident);
    SUB(type);
    return ret;
}

#undef VEC
#undef PRIM
#undef SUB
#undef AST_CLONE

ASTRef<ASTIdent> ASTGenSym(AST* ast, const char* prefix) {
    ASTRef<ASTIdent> ret(new ASTIdent());
    ret->name = strprintf("%s_%d", prefix, ast->gencounter++);
    return ret;
}

pair<const ASTIdent*, ASTRef<ASTExpr>> ASTDefineTemp(
    AST* ast,
    const char* prefix,
    ASTStmtBlock* parent,
    ASTRef<ASTExpr> initial_value,
    ASTRef<ASTType> type) {

    ASTRef<ASTStmtLet> let_stmt(new ASTStmtLet());
    let_stmt->lhs.reset(new ASTIdent());
    let_stmt->lhs = ASTGenSym(ast, prefix);
    let_stmt->rhs = move(initial_value);
    let_stmt->type = move(type);
    ASTRef<ASTStmt> stmt_box(new ASTStmt());
    const ASTIdent* temp_ident = let_stmt->lhs.get();
    stmt_box->let = move(let_stmt);

    ASTRef<ASTExpr> ref_expr(new ASTExpr());
    ref_expr->op = ASTExpr::VAR;
    ref_expr->ident = CloneAST(stmt_box->let->lhs.get());

    parent->stmts.push_back(move(stmt_box));

    return make_pair(temp_ident, move(ref_expr));
}

}  // namespace frontend
}  // namespace autopiper
