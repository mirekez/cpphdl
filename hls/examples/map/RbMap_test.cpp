#include "RbMap.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <map>
#include <random>
#include <set>
#include <stdexcept>

using Tree = RbMap<uint32_t, uint32_t>;
static void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

static unsigned audit(const Tree::Node* n, const Tree::Node* parent,
                      uint64_t low, uint64_t high, std::set<const Tree::Node*>& visited) {
    if (!n) return 1;
    require(visited.insert(n).second, "node cycle or shared child");
    require(n->parent == parent, "broken parent link");
    require(n->key >= low && n->key < high, "keys not strictly ordered");
    if (n->red) {
        require(!n->child[0] || !n->child[0]->red, "red left child of red node");
        require(!n->child[1] || !n->child[1]->red, "red right child of red node");
    }
    unsigned left = audit(n->child[0], n, low, n->key, visited);
    unsigned right = audit(n->child[1], n, uint64_t(n->key) + 1, high, visited);
    require(left == right, "unequal black height");
    return left + !n->red;
}

static void check(const Tree& tree, const std::map<uint32_t, uint32_t>& reference) {
    std::set<const Tree::Node*> visited;
    require(!tree.root() || !tree.root()->red, "root must be black");
    audit(tree.root(), nullptr, 0, uint64_t(UINT32_MAX) + 1, visited);
    require(visited.size() == reference.size() && tree.size() == reference.size(), "size mismatch");
    auto expected = reference.begin();
    for (auto* n = tree.first(); n; n = tree.next(n)) {
        require(expected != reference.end(), "extra traversal node");
        require(n->key == expected->first && n->value == expected->second, "traversal mismatch");
        require(tree.find(n->key) == n, "lookup mismatch");
        ++expected;
    }
    require(expected == reference.end(), "incomplete traversal");
}

int main() {
    try {
        Tree tree;
        std::map<uint32_t, uint32_t> reference;
        auto put = [&](uint32_t key, uint32_t value) {
            require(tree.insert_or_assign(key, value)->value == value, "insert result");
            reference[key] = value; check(tree, reference);
        };
        auto erase = [&](uint32_t key) {
            require(tree.erase(key) == (reference.erase(key) != 0), "erase result");
            check(tree, reference);
        };
        check(tree, reference); erase(0);
        // All five-key insertion/deletion permutations, including both rotations.
        std::array<uint32_t, 5> inserts{0, 1, 2, 3, 4};
        do {
            std::array<uint32_t, 5> removes{0, 1, 2, 3, 4};
            do {
                for (auto k : inserts) put(k, k * 17);
                for (auto k : removes) erase(k);
            } while (std::next_permutation(removes.begin(), removes.end()));
        } while (std::next_permutation(inserts.begin(), inserts.end()));
        for (unsigned direction = 0; direction < 2; ++direction) {
            for (unsigned i = 0; i < 128; ++i) put(direction ? 127 - i : i, i);
            while (tree.root()) erase(tree.root()->key);
        }
        put(0, 1); put(UINT32_MAX, 2); put(0, UINT32_MAX);
        tree.clear(); tree.clear(); reference.clear(); check(tree, reference);
        std::mt19937 random(0x734921u);
        for (unsigned i = 0; i < 20000; ++i) {
            uint32_t key = random() % 256;
            if (i % 997 == 0) { tree.clear(); reference.clear(); check(tree, reference); }
            else if (random() % 3 == 0) erase(key);
            else put(key, random());
        }
        std::puts("red-black invariants, exhaustive small trees, root deletion, clear and random mutations passed");
        return 0;
    } catch (const std::exception& error) { std::fprintf(stderr, "%s\n", error.what()); return 1; }
}
