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

#include "frontend/ast.h"
#include "common/parser-utils.h"

namespace autopiper {
namespace frontend {

class Parser : public ParserBase {
    public:
        Parser(std::string filename, Lexer* lexer, ErrorCollector* collector)
            : ParserBase(filename, lexer, collector) {}

        bool Parse(AST* ast);

    protected:
        bool ParseFuncDef(ASTFunctionDef* def);
        bool ParseFuncArgList(ASTFunctionDef* def);

        bool ParseTypeDef(ASTTypeDef* def);

        bool ParseBlock(ASTStmtBlock* block);

        bool ParseIdent(ASTIdent* id);
        bool ParseType(ASTType* ty);

        bool ParseStmt(ASTStmt* st);

        bool ParseStmtLet(ASTStmtLet* let);
        bool ParseStmtAssign(ASTStmtAssign* assign);
        bool ParseStmtIf(ASTStmtIf* if_);
        bool ParseStmtWhile(ASTStmtWhile* while_);
        bool ParseStmtBreak(ASTStmtBreak* break_);
        bool ParseStmtContinue(ASTStmtContinue* continue_);
        bool ParseStmtWrite(ASTStmtWrite* write);
        bool ParseStmtSpawn(ASTStmtSpawn* spawn);

        bool ParseExpr(ASTExpr* expr);
};

}  // namespace frontend
}  // namespace autopiper

#endif  // _AUTOPIPER_FRONTEND_PARSER_H_
