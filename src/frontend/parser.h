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

#ifndef _AUTOPIPER_FRONTEND_PARSER_H_
#define _AUTOPIPER_FRONTEND_PARSER_H_

#include <vector>
#include <memory>
#include <string>
#include <map>

#include "frontend/ast.h"
#include "common/parser-utils.h"

namespace autopiper {
namespace frontend {

class Parser : public ParserBase {
    public:
        Parser(std::string filename, Lexer* lexer, ErrorCollector* collector)
            : ParserBase(filename, lexer, collector) {
            lexer->SetIgnoreNewline(true);
        }

        bool Parse(AST* ast);

    protected:
        template<typename T> ASTRef<T> New();

        bool ParseFuncDef(ASTFunctionDef* def);
        bool ParseFuncArgList(ASTFunctionDef* def);

        bool ParseTypeDef(ASTTypeDef* def);

        bool ParseBlock(ASTStmtBlock* block);

        bool ParseIdent(ASTIdent* id);
        bool ParseType(ASTType* ty);

        bool ParseStmt(ASTStmt* st);

        bool ParseStmtLet(ASTStmtLet* let);
        bool ParseStmtAssignOrExpr(ASTStmt* parent);
        bool ParseStmtIf(ASTStmtIf* if_);
        bool ParseStmtWhile(ASTStmtWhile* while_);
        bool ParseStmtBreak(ASTStmtBreak* break_);
        bool ParseStmtContinue(ASTStmtContinue* continue_);
        bool ParseStmtWrite(ASTStmtWrite* write);
        bool ParseStmtSpawn(ASTStmtSpawn* spawn);
        bool ParseStmtReturn(ASTStmtReturn* return_);
        bool ParseStmtKill(ASTStmtKill* kill);
        bool ParseStmtKillYounger(ASTStmtKillYounger* killyounger);
        bool ParseStmtKillIf(ASTStmtKillIf* killif);
        bool ParseStmtTiming(ASTStmtTiming* timing);
        bool ParseStmtStage(ASTStmtStage* stage);
        bool ParseStmtNestedFunc(ASTStmtNestedFunc* func);

        ASTRef<ASTExpr>  ParseExpr();
        ASTRef<ASTExpr>  ParseExprGroup1();   // group 1:  ternary op  (?:)
        ASTRef<ASTExpr>  ParseExprGroup2();   // group 2:  logical bitwise or  (|)
        ASTRef<ASTExpr>  ParseExprGroup3();   // group 3:  logical bitwise xor (^)
        ASTRef<ASTExpr>  ParseExprGroup4();   // group 4:  logical bitwise and (&)
        ASTRef<ASTExpr>  ParseExprGroup5();   // group 5:  equality operators (==, !=)
        ASTRef<ASTExpr>  ParseExprGroup6();   // group 6:  comparison operators (<, <=, >, >=)
        ASTRef<ASTExpr>  ParseExprGroup7();   // group 7:  bitshift operators (<<, >>)
        ASTRef<ASTExpr>  ParseExprGroup8();   // group 8:  add/sub (+, -)
        ASTRef<ASTExpr>  ParseExprGroup9();   // group 9:  mul/div/rem (*, /, %)
        ASTRef<ASTExpr>  ParseExprGroup10();  // group 10: unary ops (~, unary +, unary -)
        ASTRef<ASTExpr>  ParseExprGroup11();  // group 11: array subscripting ([]), field dereferencing (.), function calls (())
        ASTRef<ASTExpr>  ParseExprAtom();     // terminals: identifiers, literals

        // Helper: left-associative binary op parse.
        typedef ASTRef<ASTExpr> (Parser::*ExprGroupParser)();
        template<
            ExprGroupParser this_level,
            ExprGroupParser next_level,
            typename ...Args>
        ASTRef<ASTExpr> ParseLeftAssocBinops(Args&&... args);

        // Helpers to ParseLeftAssocBinops impl: these implement a first/rest
        // pattern to, at compile time, build a parse function that handles all
        // ops at this precedence level.
        template<
            ExprGroupParser this_level,
            ExprGroupParser next_level,
            typename ...Args>
        bool ParseLeftAssocBinopsRHS(ASTExpr* expr,
                Token::Type op_token, ASTExpr::Op op, Args&&... args);

        template<
            ExprGroupParser this_level,
            ExprGroupParser next_level>
        bool ParseLeftAssocBinopsRHS(ASTExpr* expr);

        std::map<std::string, ASTBignum> consts_;
};

}  // namespace frontend
}  // namespace autopiper

#endif  // _AUTOPIPER_FRONTEND_PARSER_H_
