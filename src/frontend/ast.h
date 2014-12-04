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

#ifndef _AUTOPIPER_FRONTEND_AST_H_
#define _AUTOPIPER_FRONTEND_AST_H_

#include <vector>
#include <map>
#include <memory>
#include <string>

#include "common/parser-utils.h"

namespace autopiper {
namespace frontend {

// TODO: the whole AST structure should be representable as a tree of
// protobufs. This gives us dup and pretty-print for free. Visiting and
// rewriting still requires some machinery but we could probably write some
// reflection-based magic for this (including e.g. well-known field names for
// source location and other metadata of each node).

template<typename T>
using ASTVector = std::vector<std::unique_ptr<T>>;

template<typename T>
using ASTRef = std::unique_ptr<T>;

template<typename T>
ASTRef<T> astnull() { return std::unique_ptr<T>(nullptr); }

typedef Token::bignum ASTBignum;

struct ASTBase {
    autopiper::Location loc;

    ASTBase() { loc.line = 0; loc.column = 0; loc.filename = "(internal)"; }
};

struct AST;
struct ASTFunctionDef;
struct ASTTypeDef;

struct ASTIdent;
struct ASTType;
struct ASTParam;

struct ASTStmt;
struct ASTStmtBlock;
struct ASTStmtLet;
struct ASTStmtAssign;
struct ASTStmtIf;
struct ASTStmtWhile;
struct ASTStmtBreak;
struct ASTStmtContinue;
struct ASTStmtWrite;
struct ASTStmtSpawn;
struct ASTStmtReturn;
struct ASTStmtExpr;

struct ASTExpr;

struct ASTTypeField;

struct AST : public ASTBase {
    int gencounter;
    ASTVector<ASTFunctionDef> functions;
    ASTVector<ASTTypeDef> types;

    AST() : gencounter(0) {}
};

struct ASTFunctionDef : public ASTBase {
    ASTRef<ASTIdent> name;
    ASTRef<ASTType> return_type;
    ASTVector<ASTParam> params;
    ASTRef<ASTStmtBlock> block;
    bool is_entry;

    ASTFunctionDef() : is_entry(false)  {}
};

struct ASTParam : public ASTBase {
    ASTRef<ASTIdent> ident;
    ASTRef<ASTType> type;
};

struct ASTTypeDef : public ASTBase {
    ASTRef<ASTIdent> ident;
    ASTVector<ASTTypeField> fields;
};

struct ASTTypeField : public ASTBase {
    ASTRef<ASTIdent> ident;
    ASTRef<ASTType> type;
};

struct ASTIdent : public ASTBase {
    std::string name;

    enum Type {
        FUNC,
        VAR,
        TYPE,
        FIELD,
        PORT,
    };
    Type type;
};

struct ASTType : public ASTBase {
    ASTRef<ASTIdent> ident;
    bool is_port;

    ASTTypeDef* def;

    ASTType() : is_port(false), def(nullptr)  {}
};

struct ASTStmt : public ASTBase {
    ASTRef<ASTStmtBlock> block;
    ASTRef<ASTStmtLet> let;
    ASTRef<ASTStmtAssign> assign;
    ASTRef<ASTStmtIf> if_;
    ASTRef<ASTStmtWhile> while_;
    ASTRef<ASTStmtBreak> break_;
    ASTRef<ASTStmtContinue> continue_;
    ASTRef<ASTStmtWrite> write;
    ASTRef<ASTStmtSpawn> spawn;
    ASTRef<ASTStmtReturn> return_;
    ASTRef<ASTStmtExpr> expr;
};

struct ASTStmtExpr : public ASTBase {
    ASTRef<ASTExpr> expr;
};

struct ASTStmtBlock : public ASTBase {
    ASTVector<ASTStmt> stmts;
};

struct ASTStmtLet : public ASTBase {
    ASTRef<ASTIdent> lhs;
    ASTRef<ASTType> type;
    ASTRef<ASTExpr> rhs;
};

struct ASTStmtAssign : public ASTBase {
    // TODO: support array and type-field refs on LHS.
    ASTRef<ASTIdent> lhs;
    ASTRef<ASTExpr> rhs;
};

struct ASTStmtIf : public ASTBase {
    ASTRef<ASTExpr> condition;
    ASTRef<ASTStmt> if_body;
    ASTRef<ASTStmt> else_body;
};

struct ASTStmtWhile : public ASTBase {
    ASTRef<ASTExpr> condition;
    ASTRef<ASTStmt> body;
    ASTRef<ASTIdent> label;  // optional; can't be specified by user.
};

struct ASTStmtBreak : public ASTBase {
    ASTStmtWhile* enclosing_while;  // filled in later, after parse

    ASTStmtBreak() : enclosing_while(nullptr)  {}

    ASTRef<ASTIdent> label;  // optional; can't be specified by user.
};

struct ASTStmtContinue : public ASTBase {
    ASTStmtWhile* enclosing_while;  // filled in later, after parse

    ASTStmtContinue() : enclosing_while(nullptr)  {}

    ASTRef<ASTIdent> label;  // optional; can't be specified by user.
};

struct ASTStmtWrite : public ASTBase {
    ASTRef<ASTIdent> port;
    ASTRef<ASTExpr> rhs;
};

struct ASTStmtSpawn : public ASTBase {
    ASTRef<ASTStmt> body;
};

struct ASTStmtReturn : public ASTBase {
    ASTRef<ASTExpr> value;
};

struct ASTExpr : public ASTBase {
    enum Op {
        ADD,
        SUB,
        MUL,
        DIV,
        REM,
        AND,
        OR,
        NOT,
        XOR,
        LSH,
        RSH,
        SEL,
        BITSLICE,
        CONCAT,

        EQ,
        NE,
        LE,
        LT,
        GE,
        GT,

        VAR,
        CONST,
        FIELD_REF,
        ARRAY_REF,

        AGGLITERAL,
        AGGLITERALFIELD,

        FUNCCALL,

        PORTREAD,
        PORTDEF,

        STMTBLOCK,  // must end in an ASTStmtExpr
    };

    Op op;

    ASTVector<ASTExpr> ops;
    ASTRef<ASTIdent> ident;
    ASTBignum constant;

    ASTRef<ASTType> type;  // inferred; not from parser

    ASTRef<ASTStmtBlock> stmt;

    ASTExpr() : op(CONST)  {}
    ASTExpr(ASTBignum constant_)
        : op(CONST), constant(constant_)
    {}
};

template<typename T>
void PrintAST(const T* node, std::ostream& out, int indent = 0);
template<typename T>
ASTRef<T> CloneAST(const T* node);

#define AST_METHODS(type)                                                     \
    template<>                                                                \
    void PrintAST<type>(const type* node, std::ostream& out, int indent);     \
    template<>                                                                \
    ASTRef<type> CloneAST<type>(const type* node)                             \

AST_METHODS(AST);
AST_METHODS(ASTFunctionDef);
AST_METHODS(ASTTypeDef);
AST_METHODS(ASTIdent);
AST_METHODS(ASTType);
AST_METHODS(ASTParam);
AST_METHODS(ASTStmt);
AST_METHODS(ASTStmtBlock);
AST_METHODS(ASTStmtLet);
AST_METHODS(ASTStmtAssign);
AST_METHODS(ASTStmtIf);
AST_METHODS(ASTStmtWhile);
AST_METHODS(ASTStmtBreak);
AST_METHODS(ASTStmtContinue);
AST_METHODS(ASTStmtWrite);
AST_METHODS(ASTStmtSpawn);
AST_METHODS(ASTStmtReturn);
AST_METHODS(ASTStmtExpr);
AST_METHODS(ASTExpr);
AST_METHODS(ASTTypeField);

#undef AST_METHODS

// ----------------- Utility functions. ----------------------

// Generate a new ASTIdent with a unique name (based on gencounter).
ASTRef<ASTIdent> ASTGenSym(AST* ast);

// Define a new temp in block |parent|, with given |initial_value|, returning
// both a pointer to its identifier and an owning pointer to an expression that
// may be cloned to use it.
std::pair<const ASTIdent*, ASTRef<ASTExpr>> ASTDefineTemp(
        AST* ast,
        ASTStmtBlock* parent,
        ASTRef<ASTExpr> initial_value);

}  // namespace frontend
}  // namespace autopiper

#endif  // _AUTOPIPER_FRONTEND_AST_H_
