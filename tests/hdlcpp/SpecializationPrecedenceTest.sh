#!/usr/bin/env bash
set -euo pipefail
HDLCPP=$(realpath "$1")
CXX=$(command -v "$2")
REPO=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
WORK=$(mktemp -d /tmp/cpphdl-precedence.XXXXXX)
trap 'rm -r -- "$WORK"' EXIT
cd "$WORK"
mkdir generated
cat > generated/model.h <<'EOF'
#pragma once
#include "cpphdl.h"
using namespace cpphdl;
template<typename Data, unsigned Tag, typename Stored>
class Child : public Module {
public: Child() {}
void _assign() {} void _work(bool) {} void _strobe() {}
};
template<unsigned Ports, unsigned Width = (Ports != 1) ? 4 : 1, typename Data = logic<Width>, unsigned Depth = Width + 1>
class Parent : public Module {
public: Parent() {}
Child<logic<Width - 1>, Depth * 2, Data> child;
void _assign() {} void _work(bool) {} void _strobe() {}
};
EOF
echo '#include "generated/model.h"' > all_generated.h
cat > seed.cpp <<'EOF'
#include "all_generated.h"
Parent<10> first;
Parent<5 + 5, 2 + 2, logic<4>, 2 + 3> second;
EOF
"$HDLCPP" --optimize seed.cpp
mapfile -t aliases < <(sed -n 's/^using \(cpphdl_opt_t[0-9]*\) = Child<.*/\1/p' cpphdl_optimized_externs.h)
((${#aliases[@]}>=2))
{
    echo '#include "cpphdl_optimized_externs.h"'
    echo '#include <type_traits>'
    for alias in "${aliases[@]}"; do echo "static_assert(std::is_same_v<$alias, Child<logic<3>, 10, logic<4>>>);"; done
} > probe.cpp
"$CXX" -std=c++23 -fsyntax-only -I"$REPO/include" -I"$WORK" probe.cpp
