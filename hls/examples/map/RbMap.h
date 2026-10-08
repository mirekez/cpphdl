#pragma once
#include <cstdint>

// A small unique-key map. Node handles are invalidated by erase or clear.
template<class Key, class Value> class RbMap {
public:
    struct Node {
        Node* child[2];
        Node* parent;
        Key key;
        Value value;
        bool red;
        Node(Key k, Value v, Node* p) : child{nullptr, nullptr}, parent(p), key(k), value(v), red(true) {}
    };
private:
    Node* root_ = nullptr;
    uint32_t size_ = 0;

    static bool red(Node* n) { return n && n->red; }
    void replace(Node* old, Node* n) {
        if (!old->parent) root_ = n;
        else old->parent->child[old == old->parent->child[1]] = n;
        if (n) n->parent = old->parent;
    }
    // right=true rotates right; the same body handles the mirrored case.
    void rotate(Node* n, bool right) {
        Node* top = n->child[!right];
        n->child[!right] = top->child[right];
        if (n->child[!right]) n->child[!right]->parent = n;
        replace(n, top);
        top->child[right] = n;
        n->parent = top;
    }
    void fix_insert(Node* n) {
        while (n->parent && n->parent->red) {
            Node* p = n->parent;
            Node* g = p->parent;
            bool side = p == g->child[1];
            Node* uncle = g->child[!side];
            if (red(uncle)) {
                p->red = false; uncle->red = false; g->red = true; n = g;
            } else {
                if (n == p->child[!side]) { n = p; rotate(n, side); p = n->parent; }
                p->red = false; g->red = true; rotate(g, !side);
            }
        }
        root_->red = false;
    }
    // A null replacement has no parent link, so carry its parent and side.
    void fix_erase(Node* n, Node* p, bool side) {
        while (n != root_ && !red(n)) {
            Node* sibling = p->child[!side];
            if (red(sibling)) {
                sibling->red = false; p->red = true; rotate(p, side);
                sibling = p->child[!side];
            }
            if (!red(sibling->child[0]) && !red(sibling->child[1])) {
                sibling->red = true; n = p; p = n->parent;
                side = p && n == p->child[1];
            } else {
                if (!red(sibling->child[!side])) {
                    sibling->child[side]->red = false; sibling->red = true;
                    rotate(sibling, !side); sibling = p->child[!side];
                }
                sibling->red = p->red; p->red = false;
                sibling->child[!side]->red = false; rotate(p, side);
                n = root_;
            }
        }
        if (n) n->red = false;
    }
public:
    RbMap() = default;
    RbMap(const RbMap&) = delete;
    RbMap& operator=(const RbMap&) = delete;
    ~RbMap() { clear(); }

    uint32_t size() const { return size_; }
    const Node* root() const { return root_; }
    Node* find(Key key) const {
        Node* n = root_;
        while (n && n->key != key) n = n->child[n->key < key];
        return n;
    }
    Node* insert_or_assign(Key key, Value value) {
        Node* p = nullptr;
        Node* n = root_;
        bool side = false;
        while (n) {
            if (n->key == key) { n->value = value; return n; }
            p = n; side = n->key < key; n = n->child[side];
        }
        n = new Node(key, value, p);
        if (p) p->child[side] = n; else root_ = n;
        ++size_; fix_insert(n);
        return n;
    }
    bool erase(Key key) {
        Node* n = find(key);
        if (!n) return false;
        if (n->child[0] && n->child[1]) {
            Node* successor = minimum(n->child[1]);
            n->key = successor->key; n->value = successor->value; n = successor;
        }
        Node* child = n->child[n->child[0] == nullptr];
        Node* parent = n->parent;
        bool side = parent && n == parent->child[1];
        bool was_red = n->red;
        replace(n, child); delete n; --size_;
        if (!was_red) fix_erase(child, parent, side);
        return true;
    }
    static Node* minimum(Node* n) {
        while (n && n->child[0]) n = n->child[0];
        return n;
    }
    Node* first() const { return minimum(root_); }
    static Node* next(Node* n) {
        if (n->child[1]) return minimum(n->child[1]);
        while (n->parent && n == n->parent->child[1]) n = n->parent;
        return n->parent;
    }
    void clear() {
        Node* n = root_;
        while (n) {
            if (n->child[0]) n = n->child[0];
            else if (n->child[1]) n = n->child[1];
            else {
                Node* p = n->parent;
                if (p) p->child[n == p->child[1]] = nullptr;
                delete n; n = p;
            }
        }
        root_ = nullptr; size_ = 0;
    }
};
