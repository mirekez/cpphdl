#pragma once

#include <memory>
#include <string>
#include <vector>

namespace clang {
class ASTContext;
}

namespace cpphdl {

// Public dependency-tree representation used by the comb optimizer and by
// diagnostics.  Concrete objectPath values make repeated module instances
// distinct even when they share the same C++ class and method definition.
struct CombDeps {
  std::string objectPath;
  std::string className;
  std::string functionName;
  std::vector<CombDeps> dependencies;
};

class CombsOptimizer {
public:
  CombsOptimizer();
  ~CombsOptimizer();
  CombsOptimizer(CombsOptimizer &&) noexcept;
  CombsOptimizer &operator=(CombsOptimizer &&) noexcept;

  CombsOptimizer(const CombsOptimizer &) = delete;
  CombsOptimizer &operator=(const CombsOptimizer &) = delete;

  // Collect module declarations and method bodies from one Clang AST.  A
  // single optimizer is intentionally shared by every translation unit.
  void collect(clang::ASTContext &context);

  // Include cache-backed procedural comb methods in the global schedule.
  // This removes their per-cycle memoization call path and evaluates each
  // method body exactly once in dependency order.
  void setL1Scheduling(bool enabled);

  // Replace recognized bit-level arithmetic networks with equivalent scalar
  // host expressions.  This pass operates on the flattened comb graph and is
  // therefore available only through --optimize-combs modes.
  void setMathOptimization(bool enabled);

  // Generate the optimized API, internal state, and bounded comb/work C++
  // translation units in outputDirectory.  Returns false after printing a
  // precise graph/source diagnostic.
  bool generate(const std::string &rootModule,
                const std::string &outputDirectory);

  const std::vector<CombDeps> &dependencyTrees() const;

private:
  struct Impl;
  std::unique_ptr<Impl> impl;
};

} // namespace cpphdl
