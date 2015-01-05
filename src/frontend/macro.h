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

#ifndef _AUTOPIPER_FRONTEND_MACRO_H_
#define _AUTOPIPER_FRONTEND_MACRO_H_

#include "common/parser-utils.h"

#include <vector>
#include <map>
#include <string>
#include <deque>

namespace autopiper {
namespace frontend {

// Macro definitions look like this:
// 
// macro! with_vars {
//     ( [varname, value, *others], body ) =
//         (
//             let $varname = $value;
//             with_vars!([$others], $body);
//         ),
//     ( [], body ) =
//         (
//             $body;
//         )
// }
//
// - The macro system works at the tokenizer level, prior to the AST parser. A
//   defined macro can replace an arbitrary sequence of tokens after the
//   invocation. The input to the macro invocation is determined by balancing
//   tokens and observing commas at the top level of nesting to demarcate
//   arguments -- so, for example, "with_vars!([a, 1, b, 2, c, 3], { body; })"
//   matches "[a, 1, b, 2, c, 3]" as the first arg and { body; }" as the second
//   arg.
// - Each 'match arm' consists of a series of match terms in parens. A match
//   term can either be a match arg name, in which case that arg name is bound to
//   the arg value within the expanded body, or it can be a list of term names in
//   brackets [], which is a list destructuring. In the latter case, arg names
//   are bound to list elements as they match, and a final element with a
//   preceding '*' is allowed, bound to all remaining elements. A match fails
//   if the list of args terminates before the list of term names in brackets
//   ends, or vice versa.
// - Multiple match arms may exist, and the first matching one is taken. The
//   only reason a match may fail is because of a mismatch of arg count or list
//   destructuring length count.
// - The RHS (macro body) is substituted for the macro call, with arg names
//   (preceded by '$') substituted with those args' contents. A macro may
//   invoke other macros -- the result is evaluated by the macro expander just
//   like any other input.
// - Macro invocations are always suffixed by a '!' (and are recognized by the
//   tokenizer as such). Macro definitions are defined at any level with
//   'macro!', and are recognized by the tokenizer, not the parser.
//
// - Two special identifier-construction constructs are supported:
//
//   'x $$ y' glues together 'x' and 'y' into a single identifier token 'xy'.
//   '$_<tempname>' creates a new temporary identifier valid only within the
//   current macro expansion, consistent for all uses with the same <tempname>.

struct Macro {
    std::string name;
    struct PatternToken {
        enum Type {
            STARTLIST,
            ENDLIST,
            ARG,
            ARGREST,
        } type;
        std::string name;
    };
    struct BodyToken {
        enum Type {
            ARG,
            TEMP,
            CONCAT,
            LITERAL,
        } type;

        std::string arg;
        Token literal;
    };

    struct Arm {
        std::vector<PatternToken> pattern;
        std::vector<BodyToken> body;
    };
    std::vector<Arm> arms;
};

struct MacroExpansion {
    Macro* macro;
    int arm;
    std::map<std::string, std::vector<Token>> arg_bindings;
    std::map<std::string, std::string> temps;
};

class MacroExpander : public Lexer {
    public:
        MacroExpander(Lexer* input, ErrorCollector* coll);

        virtual Token Peek() const;
        virtual bool Have() const;
        virtual bool ReadNext();

        // always true -- just ignore this call.
        virtual void SetIgnoreNewline(bool ignore_newline) {}

    private:
        Lexer* input_;
        std::map<std::string, Macro> macros_;
        std::deque<Token> input_queue_;
        std::deque<Token> output_queue_;
        Location input_loc_;
        int temp_num_;

        ErrorCollector* coll_;

        bool InputHave();
        const Token& InputPeek();
        bool InputReadNext();
        void InputFill();

        void ReturnInput(const std::vector<Token>& tokens);
        void PlaceOutput(const Token& token);

        // Read a balanced-parens/brackets/braces sequence. First token on
        // input must be an open paren/bracket/brace.
        bool ReadMatchedSeq(std::vector<Token>& out,
                // Omit the outermost surrounding delimiters (parens or braces)
                bool omit_surrounding);

        bool Fill();

        // Input parse routines -- same API as ParserBase.
        void Error(std::string msg);
        const Token& CurToken() { return InputPeek(); }
        bool Expect(Token::Type type);
        bool TryExpect(Token::Type type);
        bool Consume(Token::Type type);
        void Consume();
        bool TryConsume(Token::Type type);

        bool ParseDef();
        bool ParseDefArm(
                Macro::Arm& arm,
                std::vector<Token>& args,
                std::vector<Token>& body);

        bool ReadMacroArgs(Macro* macro, MacroExpansion* exp);
        bool ArmPatternMatch(
                Macro::Arm* arm,
                std::vector<std::vector<Token>>& args,
                MacroExpansion* exp);

        bool ExpandIntoQueue(
                Macro* macro,
                MacroExpansion* expansion);
};

}  // namespace frontend
}  // namespace autopiper

#endif  // _AUTOPIPER_FRONTEND_MACRO_H_
