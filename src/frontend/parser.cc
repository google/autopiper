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
#include "frontend/parser.h"

using namespace std;

// TODO: make this a little nicer with some parser combinator-like helpers.
// Each term function should return a unique_ptr of its parse node or failure.
// We should have an easy helper (macro) to call a term function and place the
// result in a field in the result, returning a failure if term function
// returns a failure.

namespace autopiper {
namespace frontend {

template<typename T>
ASTRef<T> Parser::New() {
    ASTRef<T> t(new T());
    t->loc.filename = filename_;
    t->loc.line = CurToken().line;
    t->loc.column = CurToken().col;
    return t;
}

bool Parser::Parse(AST* ast) {
    while (true) {
        // A program is a series of defs.
        if (CurToken().type == Token::EOFTOKEN) {
            return true;
        }

        Expect(Token::IDENT);

        if (CurToken().s == "type") {
            Consume();
            ASTRef<ASTTypeDef> td = New<ASTTypeDef>();
            if (!ParseTypeDef(td.get())) {
                return false;
            }
            ast->types.push_back(move(td));
        } else if (CurToken().s == "func") {
            Consume();
            ASTRef<ASTFunctionDef> fd = New<ASTFunctionDef>();
            if (!ParseFuncDef(fd.get())) {
                return false;
            }
            ast->functions.push_back(move(fd));
        } else {
            Error("Expected 'type' or 'func' keyword.");
            return false;
        }
    }

    return true;
}

bool Parser::ParseFuncDef(ASTFunctionDef* def) {
    def->name = New<ASTIdent>();
    if (!Expect(Token::IDENT)) {
        return false;
    }
    if (CurToken().s == "entry") {
        def->is_entry = true;
        Consume();
        if (!Expect(Token::IDENT)) {
            return false;
        }
    }

    if (!ParseIdent(def->name.get())) {
        return false;
    }
    def->name->type = ASTIdent::FUNC;

    if (!Consume(Token::LPAREN)) {
        return false;
    }
    if (!ParseFuncArgList(def)) {
        return false;
    }
    if (!Consume(Token::RPAREN)) {
        return false;
    }
    if (!Consume(Token::COLON)) {
        return false;
    }
    def->return_type = New<ASTType>();
    if (!ParseType(def->return_type.get())) {
        return false;
    }

    def->block = New<ASTStmtBlock>();
    if (!ParseBlock(def->block.get())) {
        return false;
    }

    return true;
}

bool Parser::ParseFuncArgList(ASTFunctionDef* def) {
    while (true) {
        if (CurToken().type == Token::RPAREN) {
            break;
        }
        if (!def->params.empty()) {
            if (!Consume(Token::COMMA)) {
                return false;
            }
        }

        ASTRef<ASTParam> param = New<ASTParam>();
        param->ident = New<ASTIdent>();
        if (!ParseIdent(param->ident.get())) {
            return false;
        }
        param->ident->type = ASTIdent::VAR;
        if (!Consume(Token::COLON)) {
            return false;
        }
        param->type = New<ASTType>();
        if (!ParseType(param->type.get())) {
            return false;
        }
        def->params.push_back(move(param));
    }

    return true;
}

bool Parser::ParseBlock(ASTStmtBlock* block) {
    if (!Consume(Token::LBRACE)) {
        return false;
    }

    while (true) {
        if (CurToken().type == Token::RBRACE) {
            break;
        }

        ASTRef<ASTStmt> stmt = New<ASTStmt>();
        if (!ParseStmt(stmt.get())) {
            return false;
        }
        block->stmts.push_back(move(stmt));
    }

    if (!Consume(Token::RBRACE)) {
        return false;
    }

    return true;
}

bool Parser::ParseTypeDef(ASTTypeDef* def) {
    def->ident = New<ASTIdent>();
    if (!ParseIdent(def->ident.get())) {
        return false;
    }
    def->ident->type = ASTIdent::TYPE;
    if (!Consume(Token::LBRACE)) {
        return false;
    }
    while (true) {
        if (TryConsume(Token::RBRACE)) {
            break;
        }
        ASTRef<ASTTypeField> field = New<ASTTypeField>();
        field->ident = New<ASTIdent>();
        if (!ParseIdent(field->ident.get())) {
            return false;
        }
        field->ident->type = ASTIdent::FIELD;
        if (!Consume(Token::COLON)) {
            return false;
        }
        field->type = New<ASTType>();
        if (!ParseType(field->type.get())) {
            return false;
        }
        if (!Consume(Token::SEMICOLON)) {
            return false;
        }
        def->fields.push_back(move(field));
    }
    return true;
}

bool Parser::ParseIdent(ASTIdent* id) {
    if (!Expect(Token::IDENT)) {
        return false;
    }
    id->name = CurToken().s;
    Consume();
    return true;
}

bool Parser::ParseType(ASTType* ty) {
    if (!Expect(Token::IDENT)) {
        return false;
    }
    if (CurToken().s == "port") {
        ty->is_port = true;
        Consume();
        if (!Expect(Token::IDENT)) {
            return false;
        }
    } else if (CurToken().s == "chan") {
        ty->is_chan = true;
        Consume();
        if (!Expect(Token::IDENT)) {
            return false;
        }
    }
    ty->ident = New<ASTIdent>();
    if (!ParseIdent(ty->ident.get())) {
        return false;
    }
    ty->ident->type = ASTIdent::TYPE;

    if (TryExpect(Token::LBRACKET)) {
        Consume();
        if (!Expect(Token::INT_LITERAL)) {
            return false;
        }
        ty->is_array = true;
        ty->array_length = static_cast<int>(CurToken().int_literal);
        Consume();
        if (!Consume(Token::RBRACKET)) {
            return false;
        }
    }

    return true;
}

bool Parser::ParseStmt(ASTStmt* st) {
    if (TryExpect(Token::LBRACE)) {
        st->block = New<ASTStmtBlock>();
        return ParseBlock(st->block.get());
    }

#define HANDLE_STMT_TYPE(str, field, name)                   \
    if (TryExpect(Token::IDENT) && CurToken().s == str) {    \
        Consume();                                           \
        st-> field = New<ASTStmt ## name>();                 \
        return ParseStmt ## name(st-> field .get());         \
    }

    HANDLE_STMT_TYPE("let", let, Let);
    HANDLE_STMT_TYPE("if", if_, If);
    HANDLE_STMT_TYPE("while", while_, While);
    HANDLE_STMT_TYPE("break", break_, Break);
    HANDLE_STMT_TYPE("continue", continue_, Continue);
    HANDLE_STMT_TYPE("write", write, Write);
    HANDLE_STMT_TYPE("spawn", spawn, Spawn);
    HANDLE_STMT_TYPE("return", return_, Return);

#undef HANDLE_STMT_TYPE

    // No keywords matched, so we must be seeing the left-hand side identifier
    // in an assignment or simply an expression statement.
    return ParseStmtAssignOrExpr(st);
}

bool Parser::ParseStmtLet(ASTStmtLet* let) {
    // parent already consumed "def" keyword.
    
    let->lhs = New<ASTIdent>();
    if (!ParseIdent(let->lhs.get())) {
        return false;
    }
    let->lhs->type = ASTIdent::VAR;

    if (TryConsume(Token::COLON)) {
        let->type = New<ASTType>();
        if (!ParseType(let->type.get())) {
            return false;
        }
    }

    if (!Consume(Token::EQUALS)) {
        return false;
    }

    let->rhs = ParseExpr();
    if (!let->rhs) {
        return false;
    }

    return Consume(Token::SEMICOLON);
}

bool Parser::ParseStmtAssignOrExpr(ASTStmt* parent) {
    // Parse one expression: this may be the LHS of an assignment or may be a
    // bare expression statement.
    ASTRef<ASTExpr> lhs = ParseExpr();

    if (TryConsume(Token::SEMICOLON)) {
        // An expression statement.
        parent->expr.reset(new ASTStmtExpr());
        parent->expr->expr = move(lhs);
        return true;
    } else {
        // An assignment statement.
        parent->assign.reset(new ASTStmtAssign());

        // Allow any expression on the LHS at parse time; type-lowering will check
        // that it's valid (VAR, FIELD_REF with aggregate object an LHS, or
        // ARRAY_REF with array object an ident).
        parent->assign->lhs = move(lhs);

        if (!Consume(Token::EQUALS)) {
            return false;
        }

        parent->assign->rhs = ParseExpr();
        if (!parent->assign->rhs) {
            return false;
        }

        return Consume(Token::SEMICOLON);
    }
}

bool Parser::ParseStmtIf(ASTStmtIf* if_) {
    if (!Consume(Token::LPAREN)) {
        return false;
    }
    if_->condition = ParseExpr();
    if (!if_->condition) {
        return false;
    }
    if (!Consume(Token::RPAREN)) {
        return false;
    }
    if_->if_body = New<ASTStmt>();
    if (!ParseStmt(if_->if_body.get())) {
        return false;
    }
    if (TryExpect(Token::IDENT) && CurToken().s == "else") {
        Consume();
        if_->else_body = New<ASTStmt>();
        if (!ParseStmt(if_->else_body.get())) {
            return false;
        }
    }

    return true;
}

bool Parser::ParseStmtWhile(ASTStmtWhile* while_) {
    if (!Consume(Token::LPAREN)) {
        return false;
    }
    while_->condition = ParseExpr();
    if (!while_->condition) {
        return false;
    }
    if (!Consume(Token::RPAREN)) {
        return false;
    }
    while_->body = New<ASTStmt>();
    if (!ParseStmt(while_->body.get())) {
        return false;
    }
    return true;
}

bool Parser::ParseStmtBreak(ASTStmtBreak* break_) {
    return Consume(Token::SEMICOLON);
}

bool Parser::ParseStmtContinue(ASTStmtContinue* continue_) {
    return Consume(Token::SEMICOLON);
}

bool Parser::ParseStmtWrite(ASTStmtWrite* write) {
    write->port = New<ASTExpr>();
    write->port->op = ASTExpr::VAR;
    write->port->ident.reset(new ASTIdent());
    if (!ParseIdent(write->port->ident.get())) {
        return false;
    }
    if (!Consume(Token::COMMA)) {
        return false;
    }
    write->rhs = ParseExpr();
    if (!write->rhs) {
        return false;
    }
    return Consume(Token::SEMICOLON);
}

bool Parser::ParseStmtSpawn(ASTStmtSpawn* spawn) {
    spawn->body = New<ASTStmt>();
    return ParseStmt(spawn->body.get());
}

bool Parser::ParseStmtReturn(ASTStmtReturn* return_) {
    return_->value = ParseExpr();
    if (!return_->value) {
        return false;
    }
    return Consume(Token::SEMICOLON);
}

ASTRef<ASTExpr> Parser::ParseExpr() {
    return ParseExprGroup1();
}

// Group 1: ternary op
ASTRef<ASTExpr> Parser::ParseExprGroup1() {
    auto expr = ParseExprGroup2();
    if (TryConsume(Token::QUESTION)) {
        auto op1 = ParseExprGroup2();
        if (!Consume(Token::COLON)) {
            return astnull<ASTExpr>();
        }
        auto op2 = ParseExprGroup1();
        ASTRef<ASTExpr> ret = New<ASTExpr>();
        ret->op = ASTExpr::SEL;
        ret->ops.push_back(move(expr));
        ret->ops.push_back(move(op1));
        ret->ops.push_back(move(op2));
        return ret;
    }
    return expr;
}

// This is a little hacky. We're abstracting out the left-associative
// recursive-descent logic, and we want to take a template argument for the
// next-lower nonterminal (precedence group). We actually take a member
// function pointer, but devirtualization *should* reduce this down to a direct
// function call given sufficient optimization settings.
template<
    Parser::ExprGroupParser this_level,
    Parser::ExprGroupParser next_level,
    typename ...Args>
ASTRef<ASTExpr> Parser::ParseLeftAssocBinops(Args&&... args) {
  ASTRef<ASTExpr> ret = (this->*next_level)();
  ASTRef<ASTExpr> op_node = New<ASTExpr>();
  op_node->ops.push_back(move(ret));
  if (ParseLeftAssocBinopsRHS<this_level, next_level>(
      op_node.get(), args...)) {
    return op_node;
  } else {
    ret = move(op_node->ops[0]);
    return ret;
  }
}

template<
    Parser::ExprGroupParser this_level,
    Parser::ExprGroupParser next_level,
    typename ...Args>
bool Parser::ParseLeftAssocBinopsRHS(ASTExpr* expr,
                                     Token::Type op_token,
                                     ASTExpr::Op op,
                                     Args&&... args) {
  if (TryExpect(op_token)) {
    Consume();
    auto rhs = (this->*this_level)();
    if (!rhs) {
      return false;
    }
    expr->op = op;
    expr->ops.push_back(move(rhs));
    return true;
  }
  return ParseLeftAssocBinopsRHS<this_level, next_level>(expr, args...);
}

template<
    Parser::ExprGroupParser this_level,
    Parser::ExprGroupParser next_level>
bool Parser::ParseLeftAssocBinopsRHS(ASTExpr* expr) {
  return false;
}

// Group 2: logical bitwise or
ASTRef<ASTExpr> Parser::ParseExprGroup2() {
  return ParseLeftAssocBinops<
      &Parser::ParseExprGroup2,
      &Parser::ParseExprGroup3>(
            Token::PIPE, ASTExpr::OR);
}

// Group 3: logical bitwise xor
ASTRef<ASTExpr> Parser::ParseExprGroup3() {
  return ParseLeftAssocBinops<
      &Parser::ParseExprGroup3,
      &Parser::ParseExprGroup4>(
            Token::CARET, ASTExpr::XOR);
}

// Group 4: logical bitwise and
ASTRef<ASTExpr> Parser::ParseExprGroup4() {
  return ParseLeftAssocBinops<
      &Parser::ParseExprGroup4,
      &Parser::ParseExprGroup5>(
            Token::AMPERSAND, ASTExpr::AND);
}

// Group 5: equality operators
ASTRef<ASTExpr> Parser::ParseExprGroup5() {
  return ParseLeftAssocBinops<
      &Parser::ParseExprGroup5,
      &Parser::ParseExprGroup6>(
            Token::DOUBLE_EQUAL, ASTExpr::EQ,
            Token::NOT_EQUAL, ASTExpr::NE);
}

// Group 6: comparison operators
ASTRef<ASTExpr> Parser::ParseExprGroup6() {
  return ParseLeftAssocBinops<
      &Parser::ParseExprGroup6,
      &Parser::ParseExprGroup7>(
            Token::LANGLE, ASTExpr::LT,
            Token::RANGLE, ASTExpr::GT,
            Token::LESS_EQUAL, ASTExpr::LE,
            Token::GREATER_EQUAL, ASTExpr::GE);
}

// Group 7: bitshift operators
ASTRef<ASTExpr> Parser::ParseExprGroup7() {
  return ParseLeftAssocBinops<
      &Parser::ParseExprGroup7,
      &Parser::ParseExprGroup8>(
            Token::LSH, ASTExpr::LSH,
            Token::RSH, ASTExpr::RSH);
}

// Group 8: add/sub
ASTRef<ASTExpr> Parser::ParseExprGroup8() {
  return ParseLeftAssocBinops<
      &Parser::ParseExprGroup8,
      &Parser::ParseExprGroup9>(
            Token::PLUS, ASTExpr::ADD,
            Token::DASH, ASTExpr::SUB);
}

// Group 9: mul/div/rem
ASTRef<ASTExpr> Parser::ParseExprGroup9() {
  return ParseLeftAssocBinops<
      &Parser::ParseExprGroup9,
      &Parser::ParseExprGroup10>(
            Token::STAR, ASTExpr::MUL,
            Token::SLASH, ASTExpr::DIV,
            Token::PERCENT, ASTExpr::REM);
}

// Group 10: unary ops (~, unary +, unary -)
ASTRef<ASTExpr> Parser::ParseExprGroup10() {
    if (TryExpect(Token::TILDE) ||
        TryExpect(Token::PLUS) ||
        TryExpect(Token::DASH)) {
        Token::Type tok = CurToken().type;
        Consume();

        auto op = ParseExprGroup11();
        if (!op) {
            return op;
        }
        ASTRef<ASTExpr> ret = New<ASTExpr>();
        ret->ops.push_back(move(op));

        if (tok == Token::TILDE) {
            // bitwise complement.
            ret->op = ASTExpr::NOT;
        } else if (tok == Token::DASH) {
            // unary minus -- desuguar to 0 - x.
            ret->op = ASTExpr::SUB;
            ASTRef<ASTExpr> const_0 = New<ASTExpr>();
            const_0->op = ASTExpr::CONST;
            const_0->constant = 0;
            ret->ops.push_back(move(const_0));
            std::swap(ret->ops[0], ret->ops[1]);
        } else if (tok == Token::PLUS) {
            // unary plus -- just elide it.
            ret = move(op);
        }

        return ret;
    }
    return ParseExprGroup11();
}

// Group 11: array subscripting ([]), field dereferencing (.), function calls
ASTRef<ASTExpr> Parser::ParseExprGroup11() {
    auto op = ParseExprAtom();
    while (true) {
        if (TryConsume(Token::DOT)) {
            // field dereference

            if (!Expect(Token::IDENT)) {
                return astnull<ASTExpr>();
            }
            string field_name = CurToken().s;
            Consume();
            ASTRef<ASTExpr> field_ref = New<ASTExpr>();
            field_ref->op = ASTExpr::FIELD_REF;
            field_ref->ops.push_back(move(op));
            field_ref->ident = New<ASTIdent>();
            field_ref->ident->name = field_name;
            field_ref->ident->type = ASTIdent::FIELD;
            op = move(field_ref);

            continue;
        }
        if (TryConsume(Token::LBRACKET)) {
            // array dereference or bitslicing

            ASTRef<ASTExpr> leftindex;
            ASTRef<ASTExpr> rightindex;
            leftindex = move(ParseExpr());
            if (!leftindex) {
                return leftindex;
            }

            if (TryConsume(Token::COLON)) {
                rightindex = move(ParseExpr());
            }

            if (!Consume(Token::RBRACKET)) {
                return astnull<ASTExpr>();
            }

            ASTRef<ASTExpr> ret = New<ASTExpr>();
            ret->ops.push_back(move(op));
            ret->ops.push_back(move(leftindex));
            if (rightindex) {
                ret->op = ASTExpr::BITSLICE;
                ret->ops.push_back(move(rightindex));
            } else {
                ret->op = ASTExpr::ARRAY_REF;
            }
            op = move(ret);

            continue;
        }
        if (TryConsume(Token::LPAREN)) {
            // function call
 
            ASTVector<ASTExpr> args;
            while (true) {
                if (TryConsume(Token::RPAREN)) {
                    break;
                }
                if (!args.empty() &&
                    !Consume(Token::COMMA)) {
                    return astnull<ASTExpr>();
                }
                auto arg = ParseExpr();
                args.push_back(move(arg));
            }

            ASTRef<ASTExpr> func_call = New<ASTExpr>();
            func_call->op = ASTExpr::FUNCCALL;
            func_call->ident = move(op->ident);
            for (auto& arg : args) {
                func_call->ops.push_back(move(arg));
            }
            op = move(func_call);

            continue;
        }
        break;
    }

    return op;
}

// Atoms/terminals: identifiers, literals, parenthesized expressions,
// concatenation
ASTRef<ASTExpr> Parser::ParseExprAtom() {
    if (TryExpect(Token::IDENT)) {
        const string& ident = CurToken().s;
        ASTRef<ASTExpr> ret = New<ASTExpr>();
        ret->ident = New<ASTIdent>();

        if (ident == "read") {
            Consume();
            ret->op = ASTExpr::PORTREAD;
            ASTRef<ASTExpr> var_ref(new ASTExpr());
            var_ref->op = ASTExpr::VAR;
            var_ref->ident.reset(new ASTIdent());
            if (!ParseIdent(var_ref->ident.get())) {
                return astnull<ASTExpr>();
            }
            ret->ops.push_back(move(var_ref));
            return ret;
        }
        
        if (ident == "port" || ident == "chan") {
            Consume();
            ret->op = ASTExpr::PORTDEF;
            if (TryExpect(Token::QUOTED_STRING)) {
                ret->ident = New<ASTIdent>();
                ret->ident->name = CurToken().s;
                ret->ident->type = ASTIdent::PORT;
                Consume();
            }
            return ret;
        }

        // Otherwise, it's a variable reference.
        ret->op = ASTExpr::VAR;
        if (!ParseIdent(ret->ident.get())) {
            return astnull<ASTExpr>();
        }
        ret->ident->type = ASTIdent::VAR;
        return ret;
    }

    if (TryExpect(Token::INT_LITERAL)) {
        ASTRef<ASTExpr> ret = New<ASTExpr>();
        ret->op = ASTExpr::CONST;
        ret->constant = CurToken().int_literal;
        Consume();
        return ret;
    }

    if (TryConsume(Token::LPAREN)) {
        // Parenthesized expression
        auto expr = ParseExpr();
        if (!Consume(Token::RPAREN)) {
            return astnull<ASTExpr>();
        }
        return expr;
    }

    if (TryConsume(Token::LBRACE)) {
        // Concatenation, as in Verilog: { sig1, sig2, sig3 }
        ASTRef<ASTExpr> ret = New<ASTExpr>();
        ret->op = ASTExpr::CONCAT;
        while (true) {
            if (TryConsume(Token::RBRACE)) {
                break;
            }
            if (!ret->ops.empty() && !Consume(Token::COMMA)) {
                return astnull<ASTExpr>();
            }
            auto expr = ParseExpr();
            if (!expr) {
                return expr;
            }
            ret->ops.push_back(move(expr));
        }
        return ret;
    }

    if (TryConsume(Token::LBRACKET)) {
        // Aggregate type literal
        ASTRef<ASTExpr> ret = New<ASTExpr>();
        ret->op = ASTExpr::AGGLITERAL;
        while (true) {
            if (TryConsume(Token::RBRACKET)) {
                break;
            }
            if (!ret->ops.empty() && !Consume(Token::COMMA)) {
                return astnull<ASTExpr>();
            }
            ASTRef<ASTExpr> field = New<ASTExpr>();
            field->op = ASTExpr::AGGLITERALFIELD;
            field->ident = New<ASTIdent>();
            if (!ParseIdent(field->ident.get())) {
                return astnull<ASTExpr>();
            }
            field->ident->type = ASTIdent::FIELD;
            if (!TryConsume(Token::EQUALS)) {
                return astnull<ASTExpr>();
            }
            auto expr = ParseExpr();
            if (!expr) {
                return expr;
            }
            field->ops.push_back(move(expr));
            ret->ops.push_back(move(field));
        }
        return ret;
    }

    return astnull<ASTExpr>();
}

}  // namespace frontend
}  // namespace autopiper
