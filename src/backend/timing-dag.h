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

#ifndef _AUTOPIPER_TIMING_DAG_H_
#define _AUTOPIPER_TIMING_DAG_H_

#include <vector>
#include <map>
#include <set>
#include <memory>
#include <string>
#include <stack>

#include "backend/ir.h"
#include "backend/rpo.h"
#include "common/util.h"

namespace autopiper {

// Represents a generic DAG with (i) constraints based on pipestage variables,
// so that a node can specify e.g. 'node X must be in stage i and node Y must
// be in stage i+1', (ii) dependence arcs with delays (in a particular delay
// unit, usually gate delays) between nodes, and (iii) a method for "solving"
// that places nodes into pipestages given a maximum delay per stage.
//
// Nodes are of type T and timing variables are of type U. Both T and U are
// opaque and are used as pointers.
template<typename T, typename U>
class TimingDAG {
    public:
        TimingDAG() {}

        // Add a node to the graph.
        inline void AddNode(const T* node, int delay);
        // Add a constraint from a node to a timing variable, offset by a
        // certain number of stages from that variable's origin.
        inline void AddVar(const T* node, const U* var, int offset);
        // Add a dependence edge.
        inline void AddEdge(const T* from, const T* to);
        // Note a node as 'lifted'. A lifted node cannot sink past its
        // earliest-possible time during the sink phase.
        inline void LiftNode(const T* node);

        // Solves the DAG. Requires ErrorReporter with the method:
        //   ReportError(const T* node, const U* var,
        //               const std::string& message);
        //   any or both of `node`, `var` may be NULL.
        template<typename ErrorReporter>
        bool Solve(int delay_per_stage, ErrorReporter* err);

        // Reports the global stage number for a given node (after solving).
        int GetStage(const T* t) const;
        // Reports the number of stages (after solving).
        int StageCount() const;
        // Reports all nodes in a given stage.
        std::vector<const T*> NodesInStage(int stage) const;

    private:
        // Forward decls.
        struct Node;
        struct Edge;
        struct Var;
        struct Stage;
        struct NodeSucc;

        // A specialization of the generic reverse-postorder computation for
        // timing-DAG nodes.
        typedef ReversePostorder<Node, NodeSucc> NodeReversePostorder;

        // Helpers.
        template<typename ErrorReporter>
        bool CheckForCycles(ErrorReporter* err);
        template<typename ErrorReporter>
        bool DoPhase(const NodeReversePostorder& rpo,
                int delay_per_stage, ErrorReporter* err,
                bool forward, bool respectLifted);
        void SetAnchors(const NodeReversePostorder& rpo);
        void FindStageSets();
        
        // DAG data strctures.

        static const int kUnknown = -1;

        struct Edge {
            Node* from;
            Node* to;

            Edge(Node* from_, Node* to_)
                : from(from_), to(to_) {}
        };

        struct Node {
            Node(const T* t_, int delay_)
                : t(t_),
                  lifted(false),
                  delay(delay_),
                  stage(kUnknown),
                  stage_offset(kUnknown),
                  anchored(false),
                  anchored_stage(kUnknown),
                  cycle_check_visiting(false),
                  cycle_check_visited(false)
            {}

            const T* t;
            bool lifted;
            int delay;
            int stage;
            int stage_offset;  // gate delays from start of stage
            bool anchored;  // anchored to this stage
            int anchored_stage;

            bool cycle_check_visiting;
            bool cycle_check_visited;

            std::vector<Edge*> in, out;
            std::vector<std::pair<Var*, int>> vars;  // (var, stage_offset) pairs
        };

        // Successor functor for Node, and convenience RPO typedef.
        struct NodeSucc {
            std::vector<const Node*> operator()(const Node* node) {
                std::vector<const Node*> ret;
                for (auto* edge : node->out) {
                    ret.push_back(edge->to);
                }
                return ret;
            }
        };

        // Limit to iterations for unsolvable systems: each var may be updated
        // at most this many times. Note that we limit *updates per var* and
        // not *overall iterations* because we want to support bad (but
        // solvable) cases that require iterations linear in the number of
        // variables (e.g., long dependence chain and we see updates in exactly
        // the reverse order).
        static const int kMaxVarUpdates = 100;

        struct Var {
            Var(const U* u_)
                : u(u_),
                  stage(kUnknown),
                  updates(0)
            {}

            const U* u;
            int stage;
            int updates;  // tracked during solving
            std::vector<std::pair<Node*, int>> nodes;  // (node, stage_offset) pairs
        };

        struct Stage {
            std::vector<Node*> nodes;
        };

        std::vector<std::unique_ptr<Node>> nodes_;
        std::vector<std::unique_ptr<Edge>> edges_;
        std::vector<std::unique_ptr<Var>> vars_;
        std::vector<std::unique_ptr<Stage>> stages_;
        std::map<const T*, Node*> node_map_;
        std::map<const U*, Var*> var_map_;
};

template<typename T, typename U>
void TimingDAG<T, U>::AddNode(const T* t, int delay) {
    std::unique_ptr<Node> node(new Node(t, delay));
    node_map_.insert(std::make_pair(t, node.get()));
    nodes_.push_back(std::move(node));
}

template<typename T, typename U>
void TimingDAG<T, U>::AddVar(const T* node, const U* var, int offset) {
    Var* v = nullptr;
    if (var_map_.find(var) != var_map_.end()) {
        v = var_map_[var];
    } else {
        std::unique_ptr<Var> new_v(new Var(var));
        v = new_v.get();
        var_map_.insert(std::make_pair(var, new_v.get()));
        vars_.push_back(std::move(new_v));
    }
    assert(node_map_.find(node) != node_map_.end());
    Node* n = node_map_[node];
    v->nodes.push_back(std::make_pair(n, offset));
    n->vars.push_back(std::make_pair(v, offset));
}

template<typename T, typename U>
void TimingDAG<T, U>::AddEdge(const T* from, const T* to) {
    assert(node_map_.find(from) != node_map_.end());
    assert(node_map_.find(to) != node_map_.end());
    Node* f = node_map_[from];
    Node* t = node_map_[to];

    std::unique_ptr<Edge> edge(new Edge(f, t));
    f->out.push_back(edge.get());
    t->in.push_back(edge.get());
    edges_.push_back(std::move(edge));
}

template<typename T, typename U>
void TimingDAG<T, U>::LiftNode(const T* node) {
    assert(node_map_.find(node) != node_map_.end());
    Node* n = node_map_[node];
    n->lifted = true;
}

template<typename T, typename U>
template<typename ErrorReporter>
bool TimingDAG<T, U>::CheckForCycles(ErrorReporter* err) {
    // Do a simple non-recursive DFS to check for cycles.
    struct StackEntry {
        Node* node;
        int idx;
    };
    std::stack<StackEntry> dfs_stack;
    for (auto& node : nodes_) {
        node->cycle_check_visiting = false;
        node->cycle_check_visited = false;
    }
    for (auto& node : nodes_) {
        if (node->cycle_check_visited) continue;
        dfs_stack.push(StackEntry { node.get(), 0 });
        while (!dfs_stack.empty()) {
            Node* n = dfs_stack.top().node;
            int idx = dfs_stack.top().idx;
            if (idx == n->out.size()) {
                n->cycle_check_visiting = false;
                n->cycle_check_visited = true;
                dfs_stack.pop();
                continue;
            }
            n->cycle_check_visiting = true;
            Edge* e = n->out[idx];
            dfs_stack.top().idx++;
            if (e->to->cycle_check_visiting) {
                // Back-edge: cycle. Extract the cycle from the stack.
                std::vector<Node*> cycle;
                while (!dfs_stack.empty()) {
                    cycle.push_back(dfs_stack.top().node);
                    if (dfs_stack.top().node == e->to) break;
                    dfs_stack.pop();
                }
                for (auto* node : cycle) {
                    err->ReportError(node->t, NULL,
                            std::string("Node is involved in timing-DAG cycle."));
                    return false;
                }
            } else if (e->to->cycle_check_visited) {
                // Cross-edge: ignore.
            } else {
                // Forward edge.
                dfs_stack.push(StackEntry { e->to, 0 });
                continue;
            }
        }
    }
    return true;
}

// Algorithm: over a maximum of N iterations:
//
// 1. Do a forward pass over the dependence graph (in RPO), computing
//    the latest input ready time and determining whether this node fits in
//    the same stage as the latest-arriving input or if it goes to the next
//    stage. This is the 'natural stage' of a node.
//
// 2. Attempt to assign this stage to the node:
//
//    a. If no vars are attached to this node, it goes into its natural stage.
//    b. If any vars are attached, then determine whether any has a known stage.
//       i. If any do, and its stage implies a *later* stage than this
//          node's natural stage for this node, push this node's stage later.
//       ii. If none do, pick the node's natural stage as its stage.
//    b. For all attached vars, set the var's stage if previously unknown,
//       or set if known, based on this node's chosen stage. Only push vars
//       later. If any vars' stages were updated, queue another iteration
//       of this algorithm.
//
// 3. After all nodes are assigned to stages, if any vars' stages were
//    updated, perform another pass, up to a maximum of N passes. To bail out
//    if no convergence occurs, allow each var a certain number of updates,
//    and fail if a particular var reaches this limit. (This scheme, rather
//    than a global maximum iteration count, is used to allow cases where a
//    number of iterations linear in the number of variables is required. We
//    still converge because the number of vars is finite.)
//
// 4. Sink: First, set all nodes with no successors as anchored, so the
//    overall DAG length (i.e., pipeline length) does not become longer than
//    necessary. Also anchor user-specified nodes so that they remain at
//    earliest-possible positions ("lifted" nodes). Then, working backward
//    (so, in forward postorder) and with reversed edges, run the above
//    algorithm again to sink non-anchored nodes as late as possible.
//
// TODO: notion of live-set? Costs of edges; minimize the edge-cost cut
// at each pipeline boundary.

// Solve() implements the two-phase algorithm, while DoPhase() performs one
// phase in a given direction and either respecting or not respecting existing
// anchored nodes.

template<typename T, typename U>
template<typename ErrorReporter>
bool TimingDAG<T, U>::Solve(int delay_per_stage, ErrorReporter* err) {
    // Check for cycles.
    if (!CheckForCycles(err)) return false;

    // Compute reverse postorder over nodes with respect to dependence edges.
    NodeReversePostorder rpo;
    std::vector<const Node*> nodes;
    for (auto& n : nodes_) { nodes.push_back(n.get()); }
    rpo.Compute(rpo.FindRoots(nodes.begin(), nodes.end()));

    // Perform initial forward pass to find earliest point for each node.
    // These earliest points will be the anchors for port writes and 'final'
    // nodes (nodes with no successors).
    if (!DoPhase(rpo, delay_per_stage, err,
                /* forward = */ true,
                /* anchors = */ false)) return false;
    // Set anchors. This anchors both lifted nodes and nodes with no
    // successors.
    SetAnchors(rpo);
    // Perform final backward pass to sink each node to its latest possible
    // point, subject to anchors. This heuristic minimizes live-set sizes by
    // computing results only when necessary.
    if (!DoPhase(rpo, delay_per_stage, err,
                /* forward = */ false,
                /* anchors = */ true)) return false;

    // Post-process to extract nodes into stage sets.
    FindStageSets();

    return true;
}

template<typename T, typename U>
template<typename ErrorReporter>
bool TimingDAG<T, U>::DoPhase(
        const NodeReversePostorder& rpo,
        int delay_per_stage,
        ErrorReporter* err,
        bool forward,
        bool respectAnchors) {

    // Repeat until convergence.
    bool changed = true;
    while (changed) {
        changed = false;

        // In either reverse postorder (forward) or forward postorder
        // (!forward) over nodes...
        auto& rpo_seq = rpo.RPO();
        for (int i = forward ? 0 : static_cast<int>(rpo_seq.size() - 1);
             forward ? (i < static_cast<int>(rpo_seq.size())) : (i >= 0);
             forward ? ++i : --i) {
            auto* node = const_cast<Node*>(rpo_seq[i]);

            // First, ensure node's delay is not greater than delay_per_stage,
            // or else no solution will exist (the op must be able to fit in a
            // single stage).
            if (node->delay > delay_per_stage) {
                err->ReportError(node->t, NULL,
                                 strprintf("Node delay %d greater than delay-per-stage %d",
                                           node->delay, delay_per_stage));
                return false;
            }

            int node_stage = kUnknown;
            int node_offset = kUnknown;

            // If we respect anchors, snap the node to the appropriate stage.
            if (respectAnchors && node->anchored) {
                if (forward) {
                    node_offset = 0;
                } else {
                    node_offset = delay_per_stage - node->delay;
                }
                node_stage = node->anchored_stage;
            }

            // Compute natural stage based on in-edges.
            for (auto* in_edge : (forward ? node->in : node->out)) {
                // Compute natural stage according to only this predecessor node.
                int start_stage_from_this_input = kUnknown;
                int stage_offset_from_this_input = 0;
                if (forward) {
                    // Forward direction: compute natural stage based on
                    // predecessors, and push node to higher stage if
                    // necessary.
                    if (in_edge->from->stage != kUnknown) {
                        // Compute timing offset in predecessor node's output from
                        // the beginning of its stage, in gate delays.
                        int start_delay = in_edge->from->stage_offset + in_edge->from->delay;
                        assert(start_delay <= delay_per_stage);
                        // If our output would exceed this pred node's stage output
                        // time, bump this node to next stage; else, stay in same
                        // stage.
                        if ((start_delay + node->delay) > delay_per_stage) {
                            start_stage_from_this_input = in_edge->from->stage + 1;
                            stage_offset_from_this_input = 0;
                        } else {
                            start_stage_from_this_input = in_edge->from->stage;
                            stage_offset_from_this_input = start_delay;
                        }
                    }
                    if (start_stage_from_this_input != kUnknown &&
                        (node_stage == kUnknown ||
                         (start_stage_from_this_input > node_stage) ||
                         ((start_stage_from_this_input == node_stage) &&
                          (stage_offset_from_this_input > node_offset)))) {
                        node_stage = start_stage_from_this_input;
                        node_offset = stage_offset_from_this_input;
                    }
                } else {
                    // Reverse direction: compute natural stage based on
                    // successors, and push node to lower stage if necessary.
                    if (in_edge->to->stage != kUnknown) {
                        int start_delay = in_edge->to->stage_offset - node->delay;
                        if (start_delay < 0) {
                            start_stage_from_this_input = in_edge->to->stage - 1;
                            stage_offset_from_this_input = delay_per_stage - node->delay;
                        } else {
                            start_stage_from_this_input = in_edge->to->stage;
                            stage_offset_from_this_input = in_edge->to->stage_offset - node->delay;
                        }
                    }
                    if (start_stage_from_this_input != kUnknown &&
                        (node_stage == kUnknown ||
                         (start_stage_from_this_input < node_stage) ||
                         ((start_stage_from_this_input == node_stage) &&
                          (stage_offset_from_this_input < node_offset)))) {
                        node_stage = start_stage_from_this_input;
                        node_offset = stage_offset_from_this_input;
                    }
                }
            }

            // If no in-edges, default to stage 0.
            if (node_stage == kUnknown) {
                node_stage = 0;
                node_offset = 0;
            }

            // Now examine vars: see if any var implies a stage for this node,
            // and bump our stage later (but not earlier! -- to ensure forward
            // progress) if required.
            for (auto& p : node->vars) {
                auto* var = p.first;
                int offset = p.second;

                if (var->stage != kUnknown &&
                    ((forward &&
                      ((var->stage + offset) > node_stage)) ||
                     (!forward &&
                      ((var->stage + offset) < node_stage)))) {
                    node_stage = var->stage + offset;
                    // Start at 'beginning' of offset within stage -- we can
                    // monotonically approach solution but we must ensure we
                    // don't overshoot it.
                    if (forward) {
                        node_offset = 0;
                    } else {
                        node_offset = delay_per_stage;
                    }
                }
            }

            // Compare to current node location, if any, and set 'changed' flag
            // if node moved.
            if (node->stage != kUnknown &&
                    ((node->stage != node_stage) ||
                     (node->stage_offset != node_offset))) {
                changed = true;
            }

            // Now update vars: if any var attached to this node is unknown, or
            // is known but this node's placement implies a later stage for the
            // var, we're allowed to update it.
            for (auto& p : node->vars) {
                auto* var = p.first;
                int offset = p.second;

                if (var->stage == kUnknown ||
                    ((node_stage - offset) > var->stage)) {
                    var->stage = node->stage - offset;
                    var->updates++;
                    changed = true;
                    if (var->updates > kMaxVarUpdates) {
                        err->ReportError(node->t, var->u,
                                         strprintf("Var experienced more than "
                                                   "the maximum of %d updates during "
                                                   "timing-DAG solving. Please "
                                                   "reconsider timing constraints.",
                                                   kMaxVarUpdates));
                        return false;
                    }
                }
            }

            node->stage = node_stage;
            node->stage_offset = node_offset;
        }
    }
    
    return true;
}

template<typename T, typename U>
void TimingDAG<T, U>::SetAnchors(const NodeReversePostorder& rpo) {
    for (auto& node : nodes_) {
        if (node->lifted || node->out.empty()) {
            node->anchored = true;
            node->anchored_stage = node->stage;
        }
    }
}

template<typename T, typename U>
void TimingDAG<T, U>::FindStageSets() {
    // Discover the maximum stage number.
    int max_stage = kUnknown;
    for (auto& node : nodes_) {
        if (node->stage != kUnknown &&
                (max_stage == kUnknown || node->stage > max_stage)) {
            max_stage = node->stage;
        }
    }
    // Post-processing: collect nodes into stages.
    for (int i = 0; i <= max_stage; i++) {
        std::unique_ptr<Stage> stage(new Stage());
        stages_.push_back(std::move(stage));
    }
    for (auto& node : nodes_) {
        stages_[node->stage]->nodes.push_back(node.get());
    }
}

template<typename T, typename U>
int TimingDAG<T, U>::GetStage(const T* t) const {
    assert(node_map_.find(t) != node_map_.end());
    Node* n = node_map_[t];
    return n->stage;
}

template<typename T, typename U>
int TimingDAG<T, U>::StageCount() const {
    return stages_.size();
}

template<typename T, typename U>
std::vector<const T*> TimingDAG<T,  U>::NodesInStage(int stage) const {
    std::vector<const T*> ret;
    assert(stage >= 0 && stage < static_cast<int>(stages_.size()));
    for (auto* node : stages_[stage]->nodes) {
        ret.push_back(node->t);
    }
    return ret;
}

}  // namespace autopiper

#endif
