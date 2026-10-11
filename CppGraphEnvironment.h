#pragma once

#include "include/cpphdl_graph.h"
#include <algorithm>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace cpphdl::cpp_graph {

// Immutable AVL paths make branch checkpoints constant-time without retaining
// copies of every unrelated cell. Shared subtrees also bound merge work.
class Environment {
public:
    using SharedValue = std::shared_ptr<const graph::Value>;
    using Entry = std::pair<std::string, SharedValue>;

private:
    struct Node;
    using Tree = std::shared_ptr<const Node>;
    struct Node {
        std::shared_ptr<const Entry> entry;
        Tree left, right;
        unsigned height;
        std::size_t count;
        Node(std::shared_ptr<const Entry> entry, Tree left, Tree right)
            : entry(std::move(entry)), left(std::move(left)), right(std::move(right)),
              height(1 + std::max(depth(this->left), depth(this->right))),
              count(1 + size(this->left) + size(this->right)) {}
    };
    Tree root;
    static unsigned depth(const Tree& tree) { return tree ? tree->height : 0; }
    static std::size_t size(const Tree& tree) { return tree ? tree->count : 0; }
    static Tree make(std::shared_ptr<const Entry> entry, Tree left, Tree right) {
        return std::make_shared<const Node>(std::move(entry), std::move(left), std::move(right));
    }
    static Tree balance(std::shared_ptr<const Entry> entry, Tree left, Tree right) {
        if (depth(left) > depth(right) + 1) {
            if (depth(left->left) >= depth(left->right))
                return make(left->entry, left->left, make(entry, left->right, right));
            const auto& pivot = left->right;
            return make(pivot->entry, make(left->entry, left->left, pivot->left),
                        make(entry, pivot->right, right));
        }
        if (depth(right) > depth(left) + 1) {
            if (depth(right->right) >= depth(right->left))
                return make(right->entry, make(entry, left, right->left), right->right);
            const auto& pivot = right->left;
            return make(pivot->entry, make(entry, left, pivot->left),
                        make(right->entry, pivot->right, right->right));
        }
        return make(std::move(entry), std::move(left), std::move(right));
    }
    static Tree update(const Tree& tree, const std::string& key, SharedValue bits) {
        if (!tree) return bits ? make(std::make_shared<const Entry>(key, std::move(bits)), {}, {}) : tree;
        const auto& current = tree->entry->first;
        if (key < current) {
            auto child = update(tree->left, key, std::move(bits));
            return child == tree->left ? tree : balance(tree->entry, child, tree->right);
        }
        if (key > current) {
            auto child = update(tree->right, key, std::move(bits));
            return child == tree->right ? tree : balance(tree->entry, tree->left, child);
        }
        if (bits) {
            if (bits == tree->entry->second) return tree;
            return make(std::make_shared<const Entry>(key, std::move(bits)), tree->left, tree->right);
        }
        if (!tree->left) return tree->right;
        if (!tree->right) return tree->left;
        auto successor = tree->right;
        while (successor->left) successor = successor->left;
        return balance(successor->entry, tree->left, update(tree->right, successor->entry->first, {}));
    }
    const Node* findNode(const std::string& key) const {
        auto current = root.get();
        while (current) {
            if (key == current->entry->first) return current;
            current = (key < current->entry->first ? current->left : current->right).get();
        }
        return nullptr;
    }
    static void changes(const Tree& tree, const Environment& other, std::set<std::string>& keys) {
        if (!tree) return;
        auto previous = other.findNode(tree->entry->first);
        if (previous == tree.get()) return;
        if (!previous || previous->entry->second != tree->entry->second) keys.insert(tree->entry->first);
        changes(tree->left, other, keys);
        changes(tree->right, other, keys);
    }

public:
    class Iterator {
        std::vector<const Node*> pending;
        void descend(const Node* tree) {
            while (tree) { pending.push_back(tree); tree = tree->left.get(); }
        }
    public:
        explicit Iterator(const Node* tree = nullptr) { descend(tree); }
        const Entry& operator*() const { return *pending.back()->entry; }
        Iterator& operator++() {
            auto current = pending.back(); pending.pop_back(); descend(current->right.get()); return *this;
        }
        bool operator==(const Iterator& other) const {
            return (pending.empty() ? nullptr : pending.back()) ==
                   (other.pending.empty() ? nullptr : other.pending.back());
        }
    };
    Iterator begin() const { return Iterator(root.get()); }
    Iterator end() const { return Iterator(); }
    std::size_t size() const { return size(root); }
    const SharedValue* lookup(const std::string& key) const {
        auto node = findNode(key); return node ? &node->entry->second : nullptr;
    }
    bool count(const std::string& key) const { return findNode(key) != nullptr; }
    const graph::Value& at(const std::string& key) const {
        auto bits = lookup(key);
        if (!bits) throw std::out_of_range("missing graph storage: " + key);
        return **bits;
    }
    void setShared(const std::string& key, SharedValue bits) { root = update(root, key, std::move(bits)); }
    void set(const std::string& key, graph::Value bits) {
        setShared(key, std::make_shared<const graph::Value>(std::move(bits)));
    }
    void erase(const std::string& key) { root = update(root, key, {}); }
    std::set<std::string> changedKeys(const Environment& other) const {
        std::set<std::string> keys;
        changes(root, other, keys); changes(other.root, *this, keys);
        return keys;
    }
};

}
