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

#ifndef _AUTOPIPER_FRONTEND_PARSER_EXPR_H_
#define _AUTOPIPER_FRONTEND_PARSER_EXPR_H_

#include <stack>
#include <vector>
#include <functional>
#include <map>

#include "frontend/ast.h"

namespace autopiper {
namespace frontend {

template<typename T>
class ExprParser {
    public:
        typedef ASTRef<ASTExpr> E;
        typedef std::function<bool(const Token&)> OpPredicate;
        typedef std::function<E(E, E)> OpCombiner;

        struct OpDef {
            OpPredicate pred;
            OpCombiner comb;
            bool unary;
        };

        class PrecGroupProxy {
            public:
                PrecGroupProxy(vector<OpDef>& vec) : prec_group_(vec)  {}

                void Add(OpPredicate pred, OpCombiner comb, bool unary) {
                    OpDef def;
                    def.pred = pred;
                    def.comb = comb;
                    def.unary = unary;
                    prec_group_.push_back(def);
                }

                void Bin(OpPredicate pred, OpCombiner comb) {
                    Add(pred, comb, false);
                }
                void Bin(Token::Type pred_tok, OpCombiner comb) {
                    Bin([pred_tok](const Token& tok) {
                            return tok.type == pred_tok;
                        }, comb, false);
                }
                void Bin(Token::Type pred_tok, ASTExpr::Op op) {
                    Bin(pred_tok,
                        [op](E arg1, E arg2) {
                            ASTRef<ASTExpr> ret(new ASTExpr());
                            ret->op = op;
                            ret->ops.push_back(std::move(arg1));
                            ret->ops.push_back(std::move(arg2));
                            return ret;
                        });
                }

            private:
                std::vector<OpDef>& prec_group_;
        };
        
    public:
        ExprParser()  {}
        ~ExprParser()  {}

        // Parse an expression given the parser of type T. The type T must
        // expose TryConsume()/Consume(), TryExpect()/Expect(), CurToken(), and
        // Error(), as ParserBase does. In practice, you should 'friend' this
        // class from your ParserBase-derived concrete parser class in order to
        // make this work.
        //
        // Returns a pointer set to nullptr on error and sets the error state
        // on the given parser.
        template<typename T>
        ASTRef<ASTExpr> Parse(T* parser);

        // Add a precedence group of ops to this parser.
        PrecGroupProxy& PrecGroup() {
            op_prec_groups_.emplace_back();
            return PrecGroupProxy(op_prec_groups_.last());
        }

    protected:
        std::vector<vector<OpDef>> op_prec_groups_;

        std::stack<ASTRef<ASTExpr>> expr_stack_;
        std::stack<int> op_stack_;
};

template<typename T>
ASTRef<ASTExpr> ExprParser::Parse(T* parser) {
    return std::make_unique(nullptr);
}

}  // namespace frontend
}  // namespace autopiper

#endif  // _AUTOPIPER_FRONTEND_PARSER_EXPR_H_
