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
    out << I(0) << "(ast" << endl;
    for (auto& type : node->types) {
        P(type.get(), 1);
    }
    for (auto& func : node->functions) {
        P(func.get(), 1);
    }
    out << I(0) << ")" << endl;
}

AST_PRINTER(ASTFunctionDef) {
    out << I(0) << "(func" << endl;

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
    out << I(0) << "(typedef" << endl;
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
    out << I(0) << "(stmt" << endl;
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
#undef T
}

AST_PRINTER(ASTStmtBlock) {
    out << I(0) << "(stmt-block" << endl;
    for (auto& stmt : node->stmts) {
        P(stmt.get(), 1);
    }
    out << I(0) << ")" << endl;
}

AST_PRINTER(ASTStmtLet) {
    out << I(0) << "(stmt-let" << endl;
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
    out << I(0) << "(stmt-assign" << endl;
    out << I(1) << "(lhs ";
    P(node->lhs.get(), 2);
    out << ")" << endl;
    out << I(1) << "(rhs" << endl;
    P(node->rhs.get(), 2);
    out << I(1) << ")" << endl;
    out << I(0) << ")" << endl;
}

AST_PRINTER(ASTStmtIf) {
    out << I(0) << "(stmt-if" << endl;
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
    out << I(0) << "(stmt-while" << endl;
    out << I(1) << "(condition" << endl;
    P(node->condition.get(), 2);
    out << I(1) << ")" << endl;
    out << I(1) << "(body" << endl;
    P(node->body.get(), 2);
    out << I(1) << ")" << endl;
    out << I(0) << ")" << endl;
}

AST_PRINTER(ASTStmtBreak) {
    out << I(0) << "(stmt-break)" << endl;
}

AST_PRINTER(ASTStmtContinue) {
    out << I(0) << "(stmt-continue)" << endl;
}

AST_PRINTER(ASTStmtWrite) {
    out << I(0) << "(stmt-write" << endl;
    out << I(1) << "(port ";
    P(node->port.get(), 2);
    out << ")" << endl;;
    out << I(1) << "(rhs" << endl;
    P(node->rhs.get(), 2);
    out << I(1) << ")" << endl;
    out << I(0) << ")" << endl;
}

AST_PRINTER(ASTStmtSpawn) {
    out << I(0) << "(spawn" << endl;
    P(node->body.get(), 1);
    out << I(0) << ")" << endl;
}

AST_PRINTER(ASTExpr) {
    out << I(0) << "(expr ";
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
        T(AGGLITERAL);
        T(AGGLITERALFIELD);

        T(FUNCCALL);

        T(PORTREAD);
        T(PORTDEF);
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

}  // namespace frontend
}  // namespace autopiper
