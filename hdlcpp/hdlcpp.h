#pragma once

#include "Project.h"
#include "Module.h"
#include "Field.h"
#include "Method.h"
#include "Struct.h"
#include "Expr.h"
#include "Enum.h"

#include "slang/syntax/AllSyntax.h"
#include "slang/syntax/SyntaxTree.h"
#include "slang/syntax/SyntaxVisitor.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

using namespace slang;
using namespace slang::syntax;

using cpphdl::Expr;
using cpphdl::Field;
using cpphdl::Method;
using cpphdl::Module;
using cpphdl::Project;
using cpphdl::Struct;
