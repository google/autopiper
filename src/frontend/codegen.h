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

#ifndef _AUTOPIPER_FRONTEND_CODEGEN_H_
#define _AUTOPIPER_FRONTEND_CODEGEN_H_

#include "frontend/ast.h"
#include "frontend/visitor.h"
#include "backend/ir.h"

#include <vector>
#include <map>
#include <set>
#include <assert.h>

namespace autopiper {
namespace frontend {

// A CodeGenScope is a set of bindings (indexed by some arbitrary key) that can
// be overridden within nested scopes. This container type is used both for
// let-value slots (where scopes correspond to control-flow paths) and
// primitive generator functions (where overrides can occur due to higher-level
// structures).
template<typename K, typename V>
class CodeGenScope {
    public:
        CodeGenScope() {
            scopes_.push_back({});
        }

        V& operator[](const K& k) {
            for (int i = scopes_.size() - 1; i >= 0; --i) {
                auto& m = scopes_[i].bindings;
                auto it = m.find(k);
                if (it != m.end()) {
                    return it->second;
                }
            }
            // If not found, error -- new bindings must be created with Set().
            assert(false);
        }

        void Set(const K& k, V v) {
            scopes_.back().bindings[k] = v;
        }

        bool Has(const K& k) const {
            for (int i = scopes_.size() - 1; i >= 0; --i) {
                auto m = scopes_[i].bindings;
                auto it = m.find(k);
                if (it != m.end()) {
                    return true;
                }
            }
            return false;
        }

        int Push() {
            int level = scopes_.size();
            scopes_.push_back({});
            return level;
        }

        void PopTo(int level) {
            while (scopes_.size() > level) {
                scopes_.pop_back();
            }
        }

        std::map<K, V> Overlay(int from_level) const {
            std::map<K, V> ret;
            for (int i = from_level; i < scopes_.size(); i++) {
                for (auto& p : scopes_[i].bindings) {
                    ret[p.first] = p.second;
                }
            }
            return ret;
        }

        std::set<K> Keys() const {
            std::set<K> ret;
            for (auto& scope : scopes_) {
                for (auto& p : scope.bindings) {
                    ret.insert(p.first);
                }
            }
            return ret;
        }

        // Takes a set of inner-scope overlays (overwritten bindings) over a
        // comon base and collects them together into, for each variable that
        // appears, a (inner1, inner2, ...) set of values. For a given
        // variable, the value corresponding to each scope is either the
        // binding provided by the scope or, if none is provided, the binding
        // present in the baseline scope. Only variables that also appear in
        // the baseline (current) scope are included; i.e., this excludes
        // locals defined only in one inner scope.
        //
        // This function is useful, for example, in generating phi-nodes to
        // join together multiple variable definitions at control flow joins.
        std::map<K, std::vector<V>> JoinOverlays(
                const std::vector<std::map<K, V>>& inner_scopes) {
            // Take the union of all variables overwritten in inner scopes also
            // present in the base scope.
            std::set<K> vars;
            for (auto& s : inner_scopes) {
                for (auto& p : s) {
                    auto k = p.first;
                    if (vars.find(k) == vars.end() && Has(k)) {
                        vars.insert(k);
                    }
                }
            }

            // For each variable, find the binding it takes on for each path
            // (each joining scope).
            std::map<K, std::vector<V>> ret;
            for (auto& k : vars) {
                auto& v = ret[k];
                for (auto& s : inner_scopes) {
                    auto it = s.find(k);
                    if (it != s.end()) {
                        v.push_back(it->second);
                    } else {
                        v.push_back((*this)[k]);
                    }
                }
            }

            return ret;
        }

    private:
        struct Scope {
            std::map<K, V> bindings;
        };
        std::vector<Scope> scopes_;
};

class CodeGenLoopHandler;

class CodeGenContext {
    public:
        CodeGenContext(AST* ast);

        AST* ast() { return ast_; }
        IRProgram* ir() { return prog_.get(); }
        std::unique_ptr<IRProgram> Release() { return std::move(prog_); }

        // Generate a new temporary symbol.
        std::string GenSym(const char* prefix = nullptr);
        // Return a new valnum for IR.
        int Valnum() { return gensym_++; }

        // BB management. The 'current BB' is the control-flow exit point of
        // the code generated up to this point.
        IRBB* CurBB() { return curbb_; }
        void SetCurBB(IRBB* bb) { curbb_ = bb; }
        IRBB* AddBB(const char* label_prefix = nullptr);
        void AddEntry(IRBB* bb) { prog_->entries.push_back(bb); }

        // The current set of variable bindings is maintained as a 'let ->
        // expr' mapping. (Yes, I know, in a Real Functional Language a let
        // *is* the binding and there is no assignment, only creation of a new
        // binding with another let. This isn't a Real Functional Language.
        // Sorry.)
        CodeGenScope<ASTStmtLet*, const ASTExpr*>& Bindings() {
            return bindings_;
        }

        // The expr -> IRStmt mapping. Each IRStmt is one value (in the SSA
        // sense).
        IRStmt* GetIRStmt(const ASTExpr* expr) {
            return expr_to_ir_map_[expr];
        }

        // Add an IRStmt to the given BB, also recording that it computes the
        // value of |expr| (may be null if this stmt doesn't compute any expr's
        // value).
        IRStmt* AddIRStmt(IRBB* bb, std::unique_ptr<IRStmt> stmt,
                          const ASTExpr* expr = nullptr);

        // Associate an IRStmt created in some other way (e.g., already added)
        // as the value of |expr|.
        void AddIRStmt(const IRStmt* stmt, const ASTExpr* expr) {
            expr_to_ir_map_[expr] = const_cast<IRStmt*>(stmt);
        }

    private:
        std::unique_ptr<IRProgram> prog_;
        int gensym_;
        IRBB* curbb_;
        std::map<const ASTExpr*, IRStmt*> expr_to_ir_map_;
        AST* ast_;

        CodeGenScope<ASTStmtLet*, const ASTExpr*> bindings_;
};

typedef std::map<ASTStmtLet*, const ASTExpr*> SubBindingMap;

// A CodeGenPass traverses some subtree (or possibly the whole tree) of the
// AST, emitting code to a given CodeGenContext.
//
// This pass is destructive to the AST: codegen of some statements (e.g., 'if'
// and 'while') needs fine-grained control of codegen order, and so explicitly
// visits subtrees then removes them so that the visitor driver will not see
// them.
//
// TODO -- do this in a cleaner way by (i) recursively creating a CodeGenPass
// and invoking it over the if-body and else-body (for example) and (ii)
// supporting a return-value from the visit hooks that means "don't recurse
// into the subtree".
class CodeGenPass : public ASTVisitorContext {
    public:
        CodeGenPass(ErrorCollector* coll, CodeGenContext* ctx)
            : ASTVisitorContext(coll), ctx_(ctx) {}

        // Call this after the codegen pass actually runs to clean up the
        // output.
        void RemoveUnreachableBBsAndPhis();

    protected:
        // pre-hook on function: determine whether we'll do codegen or not, and
        // remove the function body if not.
        virtual Result ModifyASTFunctionDefPre(ASTRef<ASTFunctionDef>& node);

        // post-hook on function: end with a 'kill'.
        virtual Result ModifyASTFunctionDefPost(ASTRef<ASTFunctionDef>& node);

        // straight-line-code statement codegen hooks. We do codegen after
        // sub-stmts because exprs in the stmt must be gen'd first.
        virtual Result ModifyASTStmtLetPost(ASTRef<ASTStmtLet>& node);
        virtual Result ModifyASTStmtAssignPre(ASTRef<ASTStmtAssign>& node);
        virtual Result ModifyASTStmtAssignPost(ASTRef<ASTStmtAssign>& node);
        virtual Result ModifyASTStmtBreakPost(ASTRef<ASTStmtBreak>& node);
        virtual Result ModifyASTStmtContinuePost(ASTRef<ASTStmtContinue>& node);
        virtual Result ModifyASTStmtWritePost(ASTRef<ASTStmtWrite>& node);
        virtual Result ModifyASTStmtKillPost(ASTRef<ASTStmtKill>& node);
        virtual Result ModifyASTStmtKillYoungerPost(
                ASTRef<ASTStmtKillYounger>& node);
        virtual Result ModifyASTStmtKillIfPost(ASTRef<ASTStmtKillIf>& node);
        virtual Result ModifyASTStmtTimingPre(ASTRef<ASTStmtTiming>& node);
        virtual Result ModifyASTStmtTimingPost(ASTRef<ASTStmtTiming>& node);
        virtual Result ModifyASTStmtStagePost(ASTRef<ASTStmtStage>& node);
        virtual Result ModifyASTStmtExprPost(ASTRef<ASTStmtExpr>& node);

        // Expr codegen. Post-hook so that ops are already materialized.
        virtual Result ModifyASTExprPost(ASTRef<ASTExpr>& node);

        // 'if' codegen hook. This explicitly calls our visit functions on if-
        // and else-bodies after setting up the BBs appropriately, then removes
        // the bodies so the ordinary visit driver doesn't traverse into the
        // subtrees again.
        virtual Result ModifyASTStmtIfPre(ASTRef<ASTStmtIf>& node);

        // 'while' codegen hook. This also explicitly calls our visit functions
        // and then removes the subtree, for the same reasons as given above
        // for the 'if' hook.
        virtual Result ModifyASTStmtWhilePre(ASTRef<ASTStmtWhile>& node);

        virtual Result ModifyASTStmtSpawnPre(ASTRef<ASTStmtSpawn>& node);

        virtual Result ModifyASTStmtReturnPre(ASTRef<ASTStmtReturn>& node) {
            // Should have been removed by func-inlining pass.
            Error(node.get(),
                  "Return statement present at codegen: likely because "
                  "a return was attempted from an entry func.");
            return VISIT_END;
        }

        virtual Result ModifyASTPragmaPost(ASTRef<ASTPragma>& node);

    private:
        CodeGenContext* ctx_;

        const ASTExpr* FindPortDef(const ASTExpr* node, const ASTExpr* orig);

        struct LoopFrame {
            ASTStmtWhile* while_block;
            int overlay_depth;
            std::map<IRBB*, SubBindingMap> break_edges;
            std::map<IRBB*, SubBindingMap> continue_edges;
            IRBB* header;
            IRBB* footer;
            // BB that jumps to (falls into) header. Used for phi generation.
            IRBB* in_bb;
        };
        std::vector<LoopFrame> loop_frames_;

        LoopFrame* FindLoopFrame(const ASTBase* node, const ASTIdent* label);
        bool AddWhileLoopPhiNodeInputs(
                const ASTBase* node, CodeGenContext* ctx,
                std::map<ASTStmtLet*, IRStmt*>* binding_phis,
                IRBB* binding_phi_bb,
                std::map<IRBB*, SubBindingMap>& in_edges);
        void HandleBreakContinue(LoopFrame* frame,
                std::map<IRBB*, SubBindingMap>& edge_map,
                IRBB* target);

        // (lexical) stack of currently open timing {} blocks.
        std::vector<IRTimeVar*> timing_stack_;
};

}  // namespace frontend
}  // namespace autopiper

#endif  // _AUTOPIPER_FRONTEND_CODEGEN_H_
