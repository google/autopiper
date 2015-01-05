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

#include "frontend/macro.h"
#include "common/util.h"

using namespace std;

namespace autopiper {
namespace frontend {

static bool IsOpen(Token::Type t) {
    return t == Token::LPAREN ||
           t == Token::LBRACKET ||
           t == Token::LBRACE;
}
static bool IsClose(Token::Type t) {
    return t == Token::RPAREN ||
           t == Token::RBRACKET ||
           t == Token::RBRACE;
}
static Token::Type MatchingDelimiter(Token::Type t) {
    switch (t) {
        case Token::LPAREN: return Token::RPAREN;
        case Token::LBRACKET: return Token::RBRACKET;
        case Token::LBRACE: return Token::RBRACE;
        default: return Token::EOFTOKEN;
    }
}
static bool SplitListArg(vector<Token>& list, vector<vector<Token>>& out) {
    if (list.empty()) {
        return true;
    }

    out.push_back({});
    vector<Token>* cur = &out.back();

    vector<Token::Type> delim_stack;
    for (auto& tok : list) {
        if (IsOpen(tok.type)) {
            delim_stack.push_back(tok.type);
        } else if (IsClose(tok.type)) {
            if (delim_stack.empty()) {
                return false;
            }
            if (tok.type != MatchingDelimiter(delim_stack.back())) {
                return false;
            }
            delim_stack.pop_back();
        }
        if (tok.type == Token::COMMA && delim_stack.empty()) {
            if (cur->empty()) {
                return false;
            }
            out.push_back({});
            cur = &out.back();
        } else {
            cur->push_back(tok);
        }
    }

    if (!out.empty() && out.back().empty()) {
        return false;
    }

    return true;
}

static bool IsMacroIdent(Token tok) {
    return tok.type == Token::IDENT &&
           !tok.s.empty() &&
           tok.s[tok.s.size() - 1] == '!';
}


MacroExpander::MacroExpander(Lexer* input, ErrorCollector* coll) {
    input_ = input;
    input_->SetIgnoreNewline(true);
    coll_ = coll;
    temp_num_ = 0;
    InputFill();
    Fill();
}

// -------- Peek/Have/ReadNext on output. Pulls from output queue. -------------

Token MacroExpander::Peek() const {
    return output_queue_.front();
}

bool MacroExpander::Have() const {
    return !output_queue_.empty();
}

bool MacroExpander::ReadNext() {
    if (!output_queue_.empty()) {
        output_queue_.pop_back();
    }
    if (output_queue_.empty()) {
        Fill();
    }
    return Have();
}

// Peek/Have/ReadNext on input. Pulls from input queue, filling from lexer as
// appropriate.

bool MacroExpander::InputHave() {
    return !input_queue_.empty();
}

const Token& MacroExpander::InputPeek() {
    return input_queue_.front();
}

bool MacroExpander::InputReadNext() {
    if (input_queue_.empty()) {
        return false;
    }
    input_queue_.pop_front();
    InputFill();
    return InputHave();
}

void MacroExpander::InputFill() {
    if (input_queue_.empty()) {
        if (input_->Have()) {
            input_queue_.push_back(input_->Peek());
            input_loc_.line = input_queue_.back().line;
            input_loc_.column = input_queue_.back().col;
            input_->ReadNext();
        }
    }
}

void MacroExpander::ReturnInput(const std::vector<Token>& tokens) {
    for (int i = tokens.size() - 1; i >= 0; --i) {
        input_queue_.push_front(tokens[i]);
    }
}

void MacroExpander::PlaceOutput(const Token& token) {
    output_queue_.push_back(token);
}

void MacroExpander::Error(std::string msg) {
    coll_->ReportError(input_loc_, ErrorCollector::ERROR, msg);
}

bool MacroExpander::Expect(Token::Type type) {
    if (!TryExpect(type)) {
        Token expected;
        expected.type = type;
        Error(strprintf("Unexpected token %s: expected %s",
                    CurToken().ToString().c_str(),
                    expected.ToString().c_str()));
        return false;
    }
    return true;
}

bool MacroExpander::TryExpect(Token::Type type) {
    return InputHave() && CurToken().type == type;
}

bool MacroExpander::Consume(Token::Type type) {
    if (!Expect(type)) {
        return false;
    }
    Consume();
    return true;
}

bool MacroExpander::TryConsume(Token::Type type) {
    if (!TryExpect(type)) {
        return false;
    }
    Consume();
    return true;
}

void MacroExpander::Consume() {
    InputReadNext();
}

bool MacroExpander::Fill() {
    // Read a token from the input. If it's an ident that ends in a !, it's a
    // macro invocation (or a macro! definition), and we handle it specially.
    // Otherwise, transcribe to output.
    if (!InputHave()) {
        return false;
    }
    if (TryExpect(Token::IDENT) && IsMacroIdent(CurToken())) {
        if (CurToken().s == "macro!") {
            Consume();
            if (!ParseDef()) {
                return false;
            }
            if (!Have()) {
                return Fill();
            }
        } else if (CurToken().s == "undef!") {
            Consume();
            if (!Expect(Token::IDENT)) {
                return false;
            }
            auto name = CurToken().s;
            Consume();
            auto it = macros_.find(name);
            if (it != macros_.end()) {
                macros_.erase(it);
            }
            if (!Have()) {
                return Fill();
            }
        } else {
            string macro_name = CurToken().s;
            if (!macro_name.empty())
                macro_name = macro_name.substr(0, macro_name.size() - 1);

            auto it = macros_.find(macro_name);
            if (it == macros_.end()) {
                Error(strprintf("Unknown macro: %s!", macro_name.c_str()));
                return false;
            }
            Consume();

            MacroExpansion exp;
            if (!ReadMacroArgs(&it->second, &exp)) {
                return false;
            }
            if (!ExpandIntoQueue(&it->second, &exp)) {
                return false;
            }
            if (!Have()) {
                return Fill();
            }
        }
    } else {
        PlaceOutput(CurToken());
        Consume();
    }

    return true;
}

bool MacroExpander::ParseDef() {
    if (!Expect(Token::IDENT)) {
        return false;
    }
    string name = InputPeek().s;
    Consume();

    if (macros_.find(name) != macros_.end()) {
        Error(strprintf("Macro redefinition: name '%s' already defined",
                    name.c_str()));
        return false;
    }

    Macro& def = macros_[name];
    def.name = name;

    if (!Consume(Token::LBRACE)) {
        return false;
    }

    bool first_arm = true;
    while(true) {
        if (first_arm) {
            first_arm = false;
        } else {
            if (!TryConsume(Token::COMMA)) {
                break;
            }
        }

        vector<Token> args;
        if (!ReadMatchedSeq(args, /* omit_surrounding = */ true)) {
            return false;
        }

        if (!TryConsume(Token::EQUALS)) {
            return false;
        }

        vector<Token> body;
        if (!ReadMatchedSeq(body, /* omit_surrounding = */ true)) {
            return false;
        }

        def.arms.push_back({});
        Macro::Arm& arm = def.arms.back();
        if (!ParseDefArm(arm, args, body)) {
            return false;
        }
    }

    if (!Consume(Token::RBRACE)) {
        return false;
    }

    return true;
}

bool MacroExpander::ParseDefArm(
        Macro::Arm& arm,
        vector<Token>& args,
        vector<Token>& body) {

    enum {
        S_ARG_INIT,
        S_ARG_COMMA,
        S_ARG_STAR,
    } arg_state = S_ARG_INIT;
    bool arg_in_list = false;

    for (auto& argtok : args) {
        switch (arg_state) {
            case S_ARG_INIT:
                if (argtok.type == Token::IDENT) {
                    Macro::PatternToken p;
                    p.type = Macro::PatternToken::ARG;
                    p.name = argtok.s;
                    arm.pattern.push_back(p);
                    arg_state = S_ARG_COMMA;
                } else if (argtok.type == Token::STAR) {
                    arg_state = S_ARG_STAR;
                } else if (argtok.type == Token::LBRACKET) {
                    if (arg_in_list) {
                        Error("Nested lists in macro args not supported");
                        return false;
                    }
                    Macro::PatternToken p;
                    p.type = Macro::PatternToken::STARTLIST;
                    arm.pattern.push_back(p);
                    arg_state = S_ARG_INIT;
                    arg_in_list = true;
                } else if (argtok.type == Token::RBRACKET) {
                    if (!arg_in_list) {
                        Error("Right bracket seen not in list context in macro args");
                        return false;
                    }
                    Macro::PatternToken p;
                    p.type = Macro::PatternToken::ENDLIST;
                    arm.pattern.push_back(p);
                    arg_state = S_ARG_INIT;
                    arg_in_list = false;
                } else {
                    Error(strprintf("Unexpected token %s in macro arg list",
                                argtok.ToString().c_str()));
                    return false;
                }
                break;
            case S_ARG_STAR:
                if (argtok.type == Token::IDENT) {
                    Macro::PatternToken p;
                    p.type = Macro::PatternToken::ARGREST;
                    p.name = argtok.s;
                    arm.pattern.push_back(p);
                    arg_state = S_ARG_COMMA;
                }
                break;
            case S_ARG_COMMA:
                if (argtok.type == Token::COMMA) {
                    arg_state = S_ARG_INIT;
                } else if (argtok.type == Token::RBRACKET) {
                    if (!arg_in_list) {
                        Error("Right bracket seen not in list context in macro args");
                        return false;
                    }
                    Macro::PatternToken p;
                    p.type = Macro::PatternToken::ENDLIST;
                    arm.pattern.push_back(p);
                    arg_state = S_ARG_COMMA;
                    arg_in_list = false;
                } else {
                    Error(strprintf("Expected comma in arg list -- got token: %s",
                                argtok.ToString().c_str()));
                    return false;
                }
                break;
        }
    }

    // We must have left off in COMMA state and not in a list, unless the arg
    // list was empty, in which case we should be in INIT.
    if (!((arg_state == S_ARG_COMMA ||
           (arg_state == S_ARG_INIT && arm.pattern.empty())) &&
                !arg_in_list)) {
        Error("Invalid arg list to macro");
        return false;
    }

    // Validate that ARGREST tokens only come at end or right before ENDLIST.
    bool had_argrest = false;
    for (auto& pt : arm.pattern) {
        if (pt.type == Macro::PatternToken::ARGREST) {
            had_argrest = true;
        } else if (had_argrest) {
            if (pt.type != Macro::PatternToken::ENDLIST) {
                Error("*arg 'rest' pattern not at end of list "
                      "or nested list in macro args");
                return false;
            }
            had_argrest = false;
        }
    }

    // Parse body.
    enum {
        S_BODY_INIT,
        S_BODY_DOLLAR,
    } body_state = S_BODY_INIT;

    for (auto& bodytok : body) {
        switch (body_state) {
            case S_BODY_INIT:
                if (bodytok.type == Token::DOLLAR) {
                    body_state = S_BODY_DOLLAR;
                } else {
                    Macro::BodyToken b;
                    b.type = Macro::BodyToken::LITERAL;
                    b.literal = bodytok;
                    arm.body.push_back(b);
                }
                break;
            case S_BODY_DOLLAR:
                if (bodytok.type == Token::DOLLAR) {
                    Macro::BodyToken b;
                    b.type = Macro::BodyToken::CONCAT;
                    arm.body.push_back(b);
                    body_state = S_BODY_INIT;
                } else if (bodytok.type == Token::IDENT) {
                    Macro::BodyToken b;
                    if (!bodytok.s.empty() && bodytok.s[0] == '_') {
                        b.type = Macro::BodyToken::TEMP;
                        b.arg = bodytok.s.substr(1);
                    } else {
                        b.type = Macro::BodyToken::ARG;
                        b.arg = bodytok.s;
                    }
                    arm.body.push_back(b);
                    body_state = S_BODY_INIT;
                } else {
                    Error("$ followed by unknown token in macro body");
                    return false;
                }
                break;
        }
    }

    return true;
}

bool MacroExpander::ReadMacroArgs(Macro* macro, MacroExpansion* exp) {
    exp->macro = macro;

    if (!Expect(Token::LPAREN)) {
        return false;
    }

    // Read the raw stream of tokens up to the close-paren.
    vector<Token> arglist;
    if (!ReadMatchedSeq(arglist, /* omit_surrounding = */ true)) {
        return false;
    }

    // Split the args by comma (while respecting open/close nest).
    vector<vector<Token>> split_args;
    if (!SplitListArg(arglist, split_args)) {
        return false;
    }

    // Try matching against each match-arm pattern in turn.

    bool matched = false;
    for (unsigned i = 0; i < macro->arms.size(); i++) {
        if (ArmPatternMatch(&macro->arms[i], split_args, exp)) {
            matched = true;
            exp->arm = i;
            break;
        }
    }

    if (!matched) {
        Error("No macro pattern arm matched macro arguments.");
    }

    return matched;
}

static bool MatchArgs(
        vector<Macro::PatternToken>::iterator pat_begin,
        vector<Macro::PatternToken>::iterator pat_end,
        vector<vector<Token>>& args,
        map<string, vector<Token>>& argmap) {
    auto arg_it = args.begin();

    for (; pat_begin != pat_end; /* advanced manually */) {
        auto& pt = *pat_begin;
        switch (pt.type) {
            case Macro::PatternToken::ARG:
                ++pat_begin;
                if (arg_it == args.end()) {
                    return false;
                }
                argmap[pt.name] = *arg_it;
                ++arg_it;
                break;

            case Macro::PatternToken::ARGREST: {
                ++pat_begin;
                bool first = true;
                vector<Token>& rest_out = argmap[pt.name];
                while (arg_it != args.end()) {
                    if (first) {
                        first = false;
                    } else {
                        Token commatok;
                        commatok.type = Token::COMMA;
                        rest_out.push_back(commatok);
                    }
                    for (auto& input_tok : *arg_it) {
                        rest_out.push_back(input_tok);
                    }
                    ++arg_it;
                }
                break;
            }

            case Macro::PatternToken::STARTLIST: {
                if (arg_it == args.end()) {
                    return false;
                }
                if (arg_it->size() < 2 ||
                    arg_it->front().type != Token::LBRACKET ||
                    arg_it->back().type != Token::RBRACKET) {
                    return false;
                }
                vector<Token> sublist_toks(arg_it->begin() + 1, arg_it->end() - 1);
                ++arg_it;
                ++pat_begin;
                // find matching ENDLIST.
                auto list_end = pat_begin;
                while (list_end != pat_end &&
                       list_end->type != Macro::PatternToken::ENDLIST) {
                    ++list_end;
                }
                assert(list_end != pat_end);
                // Split the token list associated with this arg by commas.
                vector<vector<Token>> sublist;
                SplitListArg(sublist_toks, sublist);
                // Match pattern sublist against arg sublist.
                if (!MatchArgs(pat_begin, list_end, sublist, argmap)) {
                    return false;
                }
                // Advance pattern past ENDLIST.
                pat_begin = list_end;
                ++pat_begin;
                break;
            }

            case Macro::PatternToken::ENDLIST:
                // Shouldn't be seen -- we advance past it when processing
                // STARTLIST.
                assert(false);
                break;
        }
    }

    if (arg_it != args.end()) {
        return false;
    }

    return true;
}

bool MacroExpander::ArmPatternMatch(
        Macro::Arm* arm,
        vector<vector<Token>>& args,
        MacroExpansion* exp) {

    map<string, vector<Token>> argmap;
    if (!MatchArgs(arm->pattern.begin(), arm->pattern.end(), args, argmap)) {
        return false;
    }

    exp->arg_bindings = argmap;
    return true;
}

bool MacroExpander::ExpandIntoQueue(Macro* macro, MacroExpansion* exp) {
    // Expand into *input* queue, not output queue!
    
    vector<Token> expansion;
    vector<int> concat_indices;

    for (auto& tok : macro->arms[exp->arm].body) {
        switch (tok.type) {
            case Macro::BodyToken::ARG: {
                auto it = exp->arg_bindings.find(tok.arg);
                if (it == exp->arg_bindings.end()) {
                    Error(strprintf("Unknown arg name in macro body: $%s",
                                tok.arg.c_str()));
                    return false;
                }
                auto& subst = it->second;
                for (auto& subst_tok : subst) {
                    expansion.push_back(subst_tok);
                }
                break;
            }

            case Macro::BodyToken::TEMP: {
                auto it = exp->temps.find(tok.arg);
                string temptok;
                if (it == exp->temps.end()) {
                    ostringstream os;
                    os << "__macro__temp__" << (temp_num_++);
                    temptok = os.str();
                    exp->temps[tok.arg] = temptok;
                } else {
                    temptok = it->second;
                }
                Token ident;
                ident.type = Token::IDENT;
                ident.s = temptok;
                expansion.push_back(ident);
                break;
            }
            case Macro::BodyToken::CONCAT:
                if (expansion.empty()) {
                    Error("Concat operator ($$) in macro body with no prior "
                          "identifier.");
                    return false;
                }
                concat_indices.push_back(expansion.size() - 1);
                break;
            case Macro::BodyToken::LITERAL:
                expansion.push_back(tok.literal);
                break;
        }
    }

    // Scan through expansion to process identifier concatenation operators
    // ($$).
    vector<Token> concatseq;
    auto concat_it = concat_indices.begin();
    for (unsigned i = 0; i < expansion.size(); i++) {
        if (concat_it != concat_indices.end() &&
            static_cast<int>(i) == *concat_it) {
            if (i == expansion.size() - 1) {
                Error("Concat operator ($$) in macro body with no following "
                       "identifier.");
                return false;
            }
            if (expansion[i].type != Token::IDENT ||
                expansion[i + 1].type != Token::IDENT) {
                Error("Concat operator ($$) in macro body does not appear "
                      "between two identifier tokens");
                return false;
            }
            Token ident = expansion[i];
            ident.s += expansion[i + 1].s;
            concatseq.push_back(ident);
            ++i;
            ++concat_it;
        } else {
            concatseq.push_back(expansion[i]);
        }
    }
    expansion.swap(concatseq);
    concatseq.clear();

    // Insert expansion at start of input queue.
    ReturnInput(expansion);

    return true;
}

bool MacroExpander::ReadMatchedSeq(std::vector<Token>& out,
        bool omit_surrounding) {

    // Stack of LPAREN, LBRACKET, or LBRACE token types waiting to be matched
    // by corresponding right variants.
    std::vector<Token::Type> delimiter_stack;

    if (!InputHave() || !IsOpen(CurToken().type)) {
        Error("Looking for a matched-delimiter token sequence in macro args "
              "or body but did not find opening paren/bracket/brace");
    }

    do {
        if (!InputHave()) {
            Error("Reached EOF while scanning for closing delimiter "
                  "(paren/bracket/brace) in macro args or body");
            return false;
        }
        if (IsOpen(CurToken().type)) {
            if (!delimiter_stack.empty() || !omit_surrounding) {
                out.push_back(CurToken());
            }
            delimiter_stack.push_back(CurToken().type);
            Consume();
        } else if (IsClose(CurToken().type)) {
            if (delimiter_stack.empty()) {
                Error("Unmatched close paren/bracket/brace in macro args "
                      "or body");
                return false;
            }
            if (CurToken().type != MatchingDelimiter(delimiter_stack.back())) {
                Error("Matching close paren/bracket/brace is of wrong type "
                      "in macro args or body");
                return false;
            }
            delimiter_stack.pop_back();
            if (!delimiter_stack.empty() || !omit_surrounding) {
                out.push_back(CurToken());
            }
            Consume();
        } else {
            out.push_back(CurToken());
            Consume();
        }
    } while (!delimiter_stack.empty());

    return true;
}

}  // namespace frontend
}  // namespace autopiper
