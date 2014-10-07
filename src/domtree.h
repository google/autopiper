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

#ifndef _AUTOPIPER_DOMTREE_H_
#define _AUTOPIPER_DOMTREE_H_

#include "ir.h"
#include "rpo.h"

#include <map>
#include <vector>
#include <set>
#include <memory>
#include <string>
#include <sstream>

namespace autopiper {

// See KD Cooper et al., "A Simple, Fast Dominance Algorithm", Rice CS
// TR-06-33870, available at: http://www.cs.rice.edu/~keith/EMBED/dom.pdf
template<typename T, typename SuccFunc>
class DomTree {
 private:
  struct DomNode {
      const T* bb;
      DomNode* parent;

      DomNode(const T* bb_) : bb(bb_), parent(nullptr) {}
  };

  std::map<const T*, std::unique_ptr<DomNode>> t_to_node_map_;
  std::map<const DomNode*, const T*> node_to_t_map_;

 public:
  DomTree() {}

  bool Dom(const T* parent, const T* child) const {
      DomNode* parent_node = nullptr;
      DomNode* child_node = nullptr;
      if ((parent_node = Node(parent)) == nullptr ||
          (child_node = Node(child)) == nullptr) {
          return false;
      }
      while (child_node != nullptr) {
          if (child_node == parent_node) return true;
          child_node = child_node->parent;
      }
      return false;
  }

  bool IDom(const T* parent, const T* child) const {
      DomNode* parent_node = nullptr;
      DomNode* child_node = nullptr;
      return ((parent_node = Node(parent)) &&
              (child_node = Node(child)) &&
              child_node->parent == parent_node);
  }

  const T* IDomParent(const T* node) const {
      DomNode* child_node = Node(node);
      if (!child_node) return nullptr;
      if (!child_node->parent) return nullptr;
      return FromNode(child_node->parent);
  }

  std::string ToString() const {
      std::ostringstream os;
      for (const auto& i : t_to_node_map_) {
          const T* node = i.first;
          const DomNode* domnode = i.second.get();
          os << "BB '" << node->label.c_str() << "': parent '";
          if (domnode->parent) {
              os << domnode->parent->bb->label.c_str();
          } else {
              os << "(root)";
          }
          os << "'" << std::endl;
      }
      return os.str();
  }

  void Compute(const std::vector<const T*>& roots) {
      // Compute a DFS reverse postorder starting from the roots.
      BBReversePostorder rpo;
      rpo.Compute(roots);

      // Create DomNodes for each node.
      for (auto* node : rpo.RPO())
          Create(node);

      // Perform refinement iterations as long as something keeps changing.
      bool changed = true;
      while (changed) {
          changed = false;
          for (auto* T_node : rpo.RPO()) {
              DomNode* node = Node(T_node);
              const auto& node_preds = rpo.Preds(T_node);
              int rponum = rpo.RPONum(T_node);

              // We compute an immediate dominator (domtree parent) for |node|.
              // If this is different than what we had before, the domtree has
              // changed and we must perform at least one more iteration.
              DomNode* parent = nullptr;
              if (node_preds.size() == 0) {
                  // Zero predecessors -- is a root, parent remains null.
              } else {
                  // One or more preds: pick an already-visited (earlier in
                  // rpo) pred as initial parent, then merge the other preds.
                  for (auto* pred : node_preds) {
                      int pred_rpo = rpo.RPONum(pred);
                      if (pred_rpo < rponum) {
                          parent = Node(pred);
                          break;
                      }
                  }
                  // Note that at this point, parent *may* still be null if a
                  // node's only predecessor is itself (e.g., program starts
                  // with a self-loop.) So we close the scope here and retest
                  // this below.
              }

              if (parent != nullptr) {
                  // Now for the rest of the preds, perform a merge.
                  for (auto* pred : node_preds) {
                      DomNode* pred_node = Node(pred);
                      if (pred_node == parent) continue;
                      parent = Merge(parent, pred_node, &rpo);
                  }
              }

              // If this is a new parent, the domtree has changed.
              if (parent != node->parent)
                  changed = true;
              node->parent = parent;
          }
      }
  }

 private:
  DomNode* Merge(DomNode* node1, DomNode* node2,
                 const BBReversePostorder* rpo) {
      while (node1 != node2) {
          if (!node1 || !node2) return nullptr;

          int rpo1 = rpo->RPONum(node1->bb), rpo2 = rpo->RPONum(node2->bb);
          if (rpo1 > rpo2) {
              node1 = node1->parent;
          } else {
              node2 = node2->parent;
          }
      }
      return node1;
  }

  DomNode* Node(const T* t) const {
      auto i = t_to_node_map_.find(t);
      if (i != t_to_node_map_.end()) {
          return i->second.get();
      } else {
          return nullptr;
      }
  }
  const T* FromNode(const DomNode* node) const {
      auto i = node_to_t_map_.find(node);
      if (i != node_to_t_map_.end()) {
          return i->second;
      } else {
          return nullptr;
      }
  }
  DomNode* Create(const T* t) {
      std::unique_ptr<DomNode> node(new DomNode(t));
      DomNode* ret = node.get();
      node_to_t_map_.insert(std::make_pair(ret, t));
      t_to_node_map_.insert(std::make_pair(t, std::move(node)));
      return ret;
  }

};

// IRBB-specialized convenience typedef.
typedef DomTree<IRBB, BBSuccFunc> BBDomTree;

}  // namespace autopiper

#endif
