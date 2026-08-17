#pragma once

#include <cstddef>
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
  explicit CombsOptimizer(std::string rootModule = {});
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

  // Collection-only invocations retain one bounded source hierarchy and can
  // serialize it for a later optimizer process. Process isolation prevents
  // Clang AST/PCH arenas from accumulating across very large projects.
  void setCollectionOnly(bool enabled);
  bool saveCollection(const std::string &path) const;
  bool loadCollection(const std::string &path);

  // Replace recognized bit-level arithmetic networks with equivalent scalar
  // host expressions.  This pass operates on the flattened comb graph and is
  // therefore available only through --optimize-combs modes.
  void setMathOptimization(bool enabled);

  // Evaluate independent components of the flattened combinational DAG on at
  // most this many persistent worker lanes.  A value greater than one requires
  // an optimize-combs mode.
  void setThreadCount(std::size_t count);

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
