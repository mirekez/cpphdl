#include "Combs.h"

#include "clang/AST/ASTContext.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Lex/Lexer.h"
#include "llvm/Support/raw_ostream.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace cpphdl {
namespace {

std::string trim(std::string text) {
  const auto first = text.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) {
    return {};
  }
  const auto last = text.find_last_not_of(" \t\r\n");
  return text.substr(first, last - first + 1);
}

bool identifierStart(char value) {
  return std::isalpha(static_cast<unsigned char>(value)) || value == '_';
}

bool identifierPart(char value) {
  return std::isalnum(static_cast<unsigned char>(value)) || value == '_';
}

size_t skipSpace(const std::string &text, size_t position) {
  while (position < text.size() &&
         std::isspace(static_cast<unsigned char>(text[position]))) {
    ++position;
  }
  return position;
}

std::optional<size_t> matchingDelimiter(const std::string &text, size_t opening,
                                        char open, char close) {
  if (opening >= text.size() || text[opening] != open) {
    return std::nullopt;
  }
  int depth = 0;
  bool inString = false;
  bool inCharacter = false;
  bool escaped = false;
  for (size_t index = opening; index < text.size(); ++index) {
    const char value = text[index];
    if (escaped) {
      escaped = false;
      continue;
    }
    if ((inString || inCharacter) && value == '\\') {
      escaped = true;
      continue;
    }
    if (!inCharacter && value == '"') {
      inString = !inString;
      continue;
    }
    if (!inString && value == '\'') {
      inCharacter = !inCharacter;
      continue;
    }
    if (inString || inCharacter) {
      continue;
    }
    if (value == open) {
      ++depth;
    } else if (value == close && --depth == 0) {
      return index;
    }
  }
  return std::nullopt;
}

std::string sourceText(const clang::Stmt *statement,
                       clang::ASTContext &context) {
  if (!statement) {
    return {};
  }
  const auto &sourceManager = context.getSourceManager();
  const auto range =
      clang::CharSourceRange::getTokenRange(statement->getSourceRange());
  return clang::Lexer::getSourceText(range, sourceManager,
                                     context.getLangOpts())
      .str();
}

bool derivesModule(const clang::CXXRecordDecl *record) {
  if (!record) {
    return false;
  }
  record = record->getDefinition() ? record->getDefinition() : record;
  for (const auto &base : record->bases()) {
    const auto *baseRecord = base.getType()->getAsCXXRecordDecl();
    if (!baseRecord) {
      continue;
    }
    const std::string name = baseRecord->getQualifiedNameAsString();
    if (name == "cpphdl::Module" || derivesModule(baseRecord)) {
      return true;
    }
  }
  return false;
}

enum class FieldKind { Other, Port, Reg, Child };

struct FieldInfo {
  std::string name;
  std::string type;
  std::string childClass;
  FieldKind kind = FieldKind::Other;
};

struct ClassInfo {
  std::string name;
  std::string header;
  std::map<std::string, FieldInfo> fields;
  std::map<std::string, std::string> methods;
  std::map<std::string, std::string> combExpressions;
  std::string constructorBody;
  std::string destructorBody;
  std::string assignBody;
  std::string workBody;
  std::string strobeBody;
};

struct Collector : clang::RecursiveASTVisitor<Collector> {
  explicit Collector(std::map<std::string, ClassInfo> &classes,
                     std::set<std::string> &sourceIncludes,
                     clang::ASTContext &context)
      : classes(classes), sourceIncludes(sourceIncludes), context(context) {}

  void collectMainFileIncludes() {
    const auto &sourceManager = context.getSourceManager();
    bool invalid = false;
    const llvm::StringRef buffer =
        sourceManager.getBufferData(sourceManager.getMainFileID(), &invalid);
    if (invalid) {
      return;
    }
    static const std::regex includePattern(
        R"(^\s*#\s*include\s*[\"<]([^\">]+)[\">])");
    std::istringstream input(buffer.str());
    std::string line;
    std::smatch match;
    while (std::getline(input, line)) {
      if (std::regex_search(line, match, includePattern)) {
        sourceIncludes.insert(match[1].str());
      }
    }
  }

  void collectClass(const clang::CXXRecordDecl *declaration) {
    declaration = declaration->getDefinition() ? declaration->getDefinition()
                                               : declaration;
    const std::string name = declaration->getQualifiedNameAsString();
    if (name.empty()) {
      return;
    }
    auto &info = classes[name];
    if (!info.fields.empty()) {
      return;
    }
    info.name = name;
    const auto location =
        context.getSourceManager().getSpellingLoc(declaration->getLocation());
    if (location.isValid()) {
      info.header = std::filesystem::path(
                        context.getSourceManager().getFilename(location).str())
                        .filename()
                        .string();
    }
    for (const auto *field : declaration->fields()) {
      FieldInfo item;
      item.name = field->getNameAsString();
      item.type = field->getType().getAsString();
      if (item.type.find("cpphdl::function_ref<") != std::string::npos) {
        item.kind = FieldKind::Port;
      } else if (item.type.find("cpphdl::reg<") != std::string::npos) {
        item.kind = FieldKind::Reg;
      } else if (field->getType()->isPointerType()) {
        const auto pointee = field->getType()->getPointeeType();
        if (const auto *child = pointee->getAsCXXRecordDecl();
            child && derivesModule(child)) {
          item.kind = FieldKind::Child;
          item.childClass = child->getQualifiedNameAsString();
        }
      }
      info.fields[item.name] = std::move(item);
    }
    for (const auto *child : declaration->decls()) {
      const auto *variable = clang::dyn_cast<clang::VarDecl>(child);
      if (!variable || !variable->isStaticDataMember()) {
        continue;
      }
      FieldInfo item;
      item.name = variable->getNameAsString();
      item.type = variable->getType().getAsString();
      item.kind = FieldKind::Other;
      info.fields[item.name] = std::move(item);
    }
  }

  bool VisitCXXMethodDecl(clang::CXXMethodDecl *declaration) {
    if (!declaration->doesThisDeclarationHaveABody()) {
      return true;
    }
    const auto &sourceManager = context.getSourceManager();
    const auto methodLocation =
        sourceManager.getSpellingLoc(declaration->getLocation());
    // Each generated .cpp owns exactly one module's definitions.  Limiting
    // collection to that file avoids traversing implementation records from
    // the standard library and cpphdl support headers.
    if (!methodLocation.isValid() ||
        !sourceManager.isWrittenInMainFile(methodLocation)) {
      return true;
    }
    const auto *parent = declaration->getParent();
    if (!derivesModule(parent)) {
      return true;
    }
    collectClass(parent);
    const std::string className = parent->getQualifiedNameAsString();
    const std::string methodName = declaration->getNameAsString();
    std::string body = sourceText(declaration->getBody(), context);
    if (body.empty()) {
      return true;
    }
    auto &info = classes[className];
    info.name = className;
    if (clang::isa<clang::CXXConstructorDecl>(declaration)) {
      info.constructorBody = body;
      return true;
    }
    if (clang::isa<clang::CXXDestructorDecl>(declaration)) {
      info.destructorBody = body;
      return true;
    }
    info.methods[methodName] = body;
    if (methodName == "_assign") {
      info.assignBody = body;
    } else if (methodName == "_work") {
      info.workBody = body;
    } else if (methodName == "_strobe") {
      info.strobeBody = body;
    }
    return true;
  }

  std::map<std::string, ClassInfo> &classes;
  std::set<std::string> &sourceIncludes;
  clang::ASTContext &context;
};

std::string bodyInterior(const std::string &body) {
  const auto opening = body.find('{');
  const auto closing = body.rfind('}');
  if (opening == std::string::npos || closing == std::string::npos ||
      closing <= opening) {
    return body;
  }
  return body.substr(opening + 1, closing - opening - 1);
}

std::optional<std::string> combExpression(const std::string &body,
                                          const std::string &method) {
  const std::string marker = "return " + method + "_cache";
  const auto returnPosition = body.rfind(marker);
  if (returnPosition == std::string::npos) {
    return std::nullopt;
  }
  const auto equals = body.find('=', returnPosition + marker.size());
  const auto semicolon =
      body.find(';', equals == std::string::npos ? returnPosition : equals);
  if (equals == std::string::npos || semicolon == std::string::npos) {
    return std::nullopt;
  }
  return trim(body.substr(equals + 1, semicolon - equals - 1));
}

struct Assignment {
  std::string targetInstance;
  std::string targetPort;
  std::string expression;
  std::string macro;
};

std::vector<Assignment> parseAssignments(const std::string &body,
                                         std::string &error) {
  std::vector<Assignment> result;
  std::istringstream input(bodyInterior(body));
  std::string line;
  while (std::getline(input, line)) {
    line = trim(line);
    if (line.empty() || line.find("->_assign()") != std::string::npos) {
      continue;
    }
    const auto macro = line.find("_ASSIGN");
    if (macro == std::string::npos) {
      error = "unsupported statement in _assign(): " + line;
      return {};
    }
    const auto equals = line.find('=');
    const auto opening = line.find('(', macro);
    const auto closing = matchingDelimiter(line, opening, '(', ')');
    if (equals == std::string::npos || opening == std::string::npos ||
        !closing) {
      error = "malformed _ASSIGN statement: " + line;
      return {};
    }
    Assignment assignment;
    std::string target = trim(line.substr(0, equals));
    const auto arrow = target.find("->");
    if (arrow == std::string::npos) {
      assignment.targetPort = target;
    } else {
      assignment.targetInstance = trim(target.substr(0, arrow));
      assignment.targetPort = trim(target.substr(arrow + 2));
    }
    assignment.expression =
        trim(line.substr(opening + 1, *closing - opening - 1));
    assignment.macro = trim(line.substr(macro, opening - macro));
    result.push_back(std::move(assignment));
  }
  return result;
}

struct Instance {
  size_t id = 0;
  std::string path;
  std::string alias;
  const ClassInfo *type = nullptr;
  Instance *parent = nullptr;
  std::string parentField;
  std::map<std::string, Instance *> children;
};

enum class NodeKind { Port, Comb };

struct Node {
  size_t id = 0;
  NodeKind kind = NodeKind::Comb;
  Instance *instance = nullptr;
  std::string name;
  std::string expression;
  Instance *expressionContext = nullptr;
  std::vector<size_t> dependencies;
  std::optional<size_t> aliasOf;
  std::optional<std::string> inlineExpression;
  bool expressionReady = false;
  bool referenced = false;
  int visitState = 0;
};

std::string nodeKey(size_t instance, NodeKind kind, const std::string &name) {
  return std::to_string(instance) + (kind == NodeKind::Port ? ":p:" : ":c:") +
         name;
}

} // namespace

struct CombsOptimizer::Impl {
  std::map<std::string, ClassInfo> classes;
  std::set<std::string> sourceIncludes;
  std::vector<std::unique_ptr<Instance>> instanceStorage;
  std::vector<Instance *> instances;
  std::vector<Node> nodes;
  std::unordered_map<std::string, size_t> nodeIds;
  std::unordered_map<std::string, std::pair<Instance *, std::string>> bindings;
  std::vector<size_t> schedule;
  std::vector<CombDeps> trees;
  std::string error;

  Instance *addInstance(const ClassInfo &type, Instance *parent,
                        const std::string &parentField,
                        std::unordered_set<std::string> &activeClasses) {
    if (activeClasses.contains(type.name)) {
      error = "recursive module class in concrete hierarchy: " + type.name;
      return nullptr;
    }
    auto value = std::make_unique<Instance>();
    value->id = instances.size();
    value->alias = "n" + std::to_string(value->id);
    value->type = &type;
    value->parent = parent;
    value->parentField = parentField;
    value->path = parent ? parent->path + "." + parentField : type.name;
    Instance *instance = value.get();
    instanceStorage.push_back(std::move(value));
    instances.push_back(instance);

    activeClasses.insert(type.name);
    for (const auto &[fieldName, field] : type.fields) {
      if (field.kind != FieldKind::Child) {
        continue;
      }
      const auto found = classes.find(field.childClass);
      if (found == classes.end()) {
        error = "missing class definition for child " + instance->path + "." +
                fieldName + " (" + field.childClass + ")";
        return nullptr;
      }
      Instance *child =
          addInstance(found->second, instance, fieldName, activeClasses);
      if (!child) {
        return nullptr;
      }
      instance->children[fieldName] = child;
    }
    activeClasses.erase(type.name);
    return instance;
  }

  size_t getNode(Instance &instance, NodeKind kind, const std::string &name) {
    const std::string key = nodeKey(instance.id, kind, name);
    const auto existing = nodeIds.find(key);
    if (existing != nodeIds.end()) {
      return existing->second;
    }
    Node node;
    node.id = nodes.size();
    node.kind = kind;
    node.instance = &instance;
    node.name = name;
    node.expressionContext = &instance;
    nodes.push_back(std::move(node));
    nodeIds[key] = nodes.back().id;
    return nodes.back().id;
  }

  std::optional<size_t> callableNode(Instance &instance,
                                     const std::string &name) {
    const auto field = instance.type->fields.find(name);
    if (field != instance.type->fields.end() &&
        field->second.kind == FieldKind::Port) {
      return getNode(instance, NodeKind::Port, name);
    }
    if (instance.type->combExpressions.contains(name)) {
      return getNode(instance, NodeKind::Comb, name);
    }
    return std::nullopt;
  }

  std::string nodeValue(size_t id) const {
    const Node &node = nodes[id];
    if (node.kind == NodeKind::Port) {
      return "s.p" + std::to_string(id);
    }
    return node.instance->alias + "." + node.name + "_cache";
  }

  std::set<size_t>
  referencedInstances(const std::string &text,
                      const std::set<size_t> &initial = {}) const {
    std::set<size_t> result = initial;
    static const std::regex aliasPattern(R"(\bn([0-9]+)\b)");
    for (auto iterator =
             std::sregex_iterator(text.begin(), text.end(), aliasPattern);
         iterator != std::sregex_iterator(); ++iterator) {
      const size_t id = std::stoull((*iterator)[1].str());
      if (id < instances.size()) {
        result.insert(id);
      }
    }
    std::vector<size_t> values(result.begin(), result.end());
    for (const size_t id : values) {
      for (Instance *current = instances[id]->parent; current;
           current = current->parent) {
        result.insert(current->id);
      }
    }
    return result;
  }

  void emitAliases(std::ostream &output, const std::set<size_t> &used) const {
    if (used.contains(0)) {
      output << "  auto& n0 = obj;\n";
    }
    for (size_t index = 1; index < instances.size(); ++index) {
      if (!used.contains(index)) {
        continue;
      }
      const Instance &instance = *instances[index];
      output << "  auto& " << instance.alias << " = *" << instance.parent->alias
             << "." << instance.parentField << ";\n";
    }
  }

  std::string rewrite(const std::string &input, Instance &context,
                      std::vector<size_t> *dependencies = nullptr,
                      const std::unordered_set<std::string> &locals = {}) {
    std::string output;
    output.reserve(input.size() + input.size() / 4);
    size_t index = 0;
    while (index < input.size()) {
      if (input[index] == '"' || input[index] == '\'') {
        const char quote = input[index];
        size_t end = index + 1;
        bool escaped = false;
        for (; end < input.size(); ++end) {
          if (escaped) {
            escaped = false;
          } else if (input[end] == '\\') {
            escaped = true;
          } else if (input[end] == quote) {
            ++end;
            break;
          }
        }
        output.append(input, index, end - index);
        index = end;
        continue;
      }
      if (!identifierStart(input[index])) {
        output.push_back(input[index++]);
        continue;
      }
      const size_t start = index++;
      while (index < input.size() && identifierPart(input[index])) {
        ++index;
      }
      const std::string identifier = input.substr(start, index - start);
      const size_t afterIdentifier = skipSpace(input, index);
      const bool qualifiedBefore =
          start >= 2 && (input.substr(start - 2, 2) == "::" ||
                         input.substr(start - 2, 2) == "->");
      const bool memberBefore = start > 0 && input[start - 1] == '.';

      // Resolve a direct child port/comb call as one concrete graph node.
      const auto child = context.children.find(identifier);
      if (!qualifiedBefore && !memberBefore &&
          child != context.children.end() &&
          afterIdentifier + 2 <= input.size() &&
          input.substr(afterIdentifier, 2) == "->") {
        size_t methodStart = skipSpace(input, afterIdentifier + 2);
        if (methodStart < input.size() && identifierStart(input[methodStart])) {
          size_t methodEnd = methodStart + 1;
          while (methodEnd < input.size() && identifierPart(input[methodEnd])) {
            ++methodEnd;
          }
          const std::string method =
              input.substr(methodStart, methodEnd - methodStart);
          const size_t opening = skipSpace(input, methodEnd);
          if (opening < input.size() && input[opening] == '(') {
            const auto closing = matchingDelimiter(input, opening, '(', ')');
            if (closing &&
                trim(input.substr(opening + 1, *closing - opening - 1))
                    .empty()) {
              if (auto node = callableNode(*child->second, method)) {
                nodes[*node].referenced = true;
                if (dependencies)
                  dependencies->push_back(*node);
                output += nodeValue(*node);
                index = *closing + 1;
                continue;
              }
            }
          }
        }
        output += child->second->alias;
        index = afterIdentifier + 2;
        continue;
      }

      if (!qualifiedBefore && !memberBefore && identifier == "this") {
        output += "std::addressof(" + context.alias + ")";
        continue;
      }

      if (!qualifiedBefore && !memberBefore && !locals.contains(identifier) &&
          afterIdentifier < input.size() && input[afterIdentifier] == '(') {
        const auto closing =
            matchingDelimiter(input, afterIdentifier, '(', ')');
        if (closing && trim(input.substr(afterIdentifier + 1,
                                         *closing - afterIdentifier - 1))
                           .empty()) {
          if (auto node = callableNode(context, identifier)) {
            nodes[*node].referenced = true;
            if (dependencies)
              dependencies->push_back(*node);
            output += nodeValue(*node);
            index = *closing + 1;
            continue;
          }
        }
      }

      const auto field = context.type->fields.find(identifier);
      if (!qualifiedBefore && !memberBefore && !locals.contains(identifier) &&
          field != context.type->fields.end()) {
        if (field->second.kind == FieldKind::Port) {
          const size_t node = getNode(context, NodeKind::Port, identifier);
          nodes[node].referenced = true;
          if (dependencies)
            dependencies->push_back(node);
          output += nodeValue(node);
        } else if (field->second.kind == FieldKind::Child) {
          const auto instance = context.children.find(identifier);
          if (instance == context.children.end()) {
            error = "missing concrete child " + context.path + "." + identifier;
            return {};
          }
          output += instance->second->alias;
        } else {
          output += context.alias + "." + identifier;
        }
        continue;
      }

      output += identifier;
    }
    if (dependencies) {
      std::sort(dependencies->begin(), dependencies->end());
      dependencies->erase(
          std::unique(dependencies->begin(), dependencies->end()),
          dependencies->end());
    }
    return output;
  }

  std::string rewriteBinding(const std::string &input, Instance &context) {
    std::string output;
    output.reserve(input.size() + input.size() / 4);
    size_t index = 0;
    while (index < input.size()) {
      if (input[index] == '"' || input[index] == '\'') {
        const char quote = input[index];
        size_t end = index + 1;
        bool escaped = false;
        for (; end < input.size(); ++end) {
          if (escaped) {
            escaped = false;
          } else if (input[end] == '\\') {
            escaped = true;
          } else if (input[end] == quote) {
            ++end;
            break;
          }
        }
        output.append(input, index, end - index);
        index = end;
        continue;
      }
      if (!identifierStart(input[index])) {
        output.push_back(input[index++]);
        continue;
      }
      const size_t start = index++;
      while (index < input.size() && identifierPart(input[index])) {
        ++index;
      }
      const std::string identifier = input.substr(start, index - start);
      const size_t afterIdentifier = skipSpace(input, index);
      const bool qualifiedBefore =
          start >= 2 && (input.substr(start - 2, 2) == "::" ||
                         input.substr(start - 2, 2) == "->");
      const bool memberBefore = start > 0 && input[start - 1] == '.';

      const auto child = context.children.find(identifier);
      if (!qualifiedBefore && !memberBefore && child != context.children.end() &&
          afterIdentifier + 2 <= input.size() &&
          input.substr(afterIdentifier, 2) == "->") {
        output += child->second->alias + ".";
        index = afterIdentifier + 2;
        continue;
      }

      if (!qualifiedBefore && !memberBefore && identifier == "this") {
        output += "std::addressof(" + context.alias + ")";
        continue;
      }

      const auto field = context.type->fields.find(identifier);
      if (!qualifiedBefore && !memberBefore &&
          field != context.type->fields.end()) {
        if (field->second.kind == FieldKind::Child) {
          if (child == context.children.end()) {
            error = "missing concrete child " + context.path + "." + identifier;
            return {};
          }
          output += child->second->alias;
        } else {
          output += context.alias + "." + identifier;
        }
        continue;
      }

      if (!qualifiedBefore && !memberBefore &&
          context.type->combExpressions.contains(identifier) &&
          afterIdentifier < input.size() && input[afterIdentifier] == '(') {
        output += context.alias + "." + identifier;
        continue;
      }

      output += identifier;
    }
    return output;
  }

  bool prepareNode(size_t id) {
    if (nodes[id].expressionReady) {
      return true;
    }
    const NodeKind kind = nodes[id].kind;
    Instance *instance = nodes[id].instance;
    const std::string name = nodes[id].name;
    std::string expressionText;
    Instance *expressionContext = instance;
    if (kind == NodeKind::Comb) {
      const auto expression = instance->type->combExpressions.find(name);
      if (expression == instance->type->combExpressions.end()) {
        error = "missing comb expression for " + instance->path + "." + name;
        return false;
      }
      expressionText = expression->second;
    } else {
      const std::string key = nodeKey(instance->id, NodeKind::Port, name);
      const auto binding = bindings.find(key);
      if (binding != bindings.end()) {
        expressionContext = binding->second.first;
        expressionText = binding->second.second;
      } else if (!instance->parent) {
        nodes[id].expression = instance->alias + "." + name + "()";
        nodes[id].expressionContext = instance;
        nodes[id].expressionReady = true;
        return true;
      } else {
        error = "unresolved non-root port " + instance->path + "." + name;
        return false;
      }
    }
    std::vector<size_t> dependencies;
    expressionText = rewrite(expressionText, *expressionContext, &dependencies);
    if (!error.empty()) {
      return false;
    }
    nodes[id].expression = std::move(expressionText);
    nodes[id].expressionContext = expressionContext;
    nodes[id].dependencies = std::move(dependencies);
    nodes[id].expressionReady = true;
    return true;
  }

  bool visitNode(size_t id, std::vector<size_t> &stack) {
    if (nodes[id].visitState == 2) {
      return true;
    }
    if (nodes[id].visitState == 1) {
      std::ostringstream cycle;
      cycle << "combinational dependency cycle:";
      const auto begin = std::find(stack.begin(), stack.end(), id);
      for (auto iterator = begin; iterator != stack.end(); ++iterator) {
        const Node &item = nodes[*iterator];
        cycle << " " << item.instance->path << "." << item.name << " ->";
      }
      cycle << " " << nodes[id].instance->path << "." << nodes[id].name;
      error = cycle.str();
      return false;
    }
    if (!prepareNode(id)) {
      return false;
    }
    nodes[id].visitState = 1;
    stack.push_back(id);
    const std::vector<size_t> dependencies = nodes[id].dependencies;
    for (const size_t dependency : dependencies) {
      if (!visitNode(dependency, stack)) {
        return false;
      }
    }
    stack.pop_back();
    nodes[id].visitState = 2;
    schedule.push_back(id);
    return true;
  }

  size_t canonicalNode(size_t id) const {
    while (nodes[id].aliasOf) {
      id = *nodes[id].aliasOf;
    }
    return id;
  }

  std::optional<size_t> valueNode(const std::smatch &match) const {
    if (match[1].matched) {
      const size_t id = std::stoull(match[1].str());
      if (id < nodes.size()) {
        return id;
      }
      return std::nullopt;
    }
    const size_t instance = std::stoull(match[2].str());
    if (instance >= instances.size()) {
      return std::nullopt;
    }
    const auto found =
        nodeIds.find(nodeKey(instance, NodeKind::Comb, match[3].str()));
    if (found == nodeIds.end()) {
      return std::nullopt;
    }
    return found->second;
  }

  std::string replaceAliases(const std::string &input) const {
    static const std::regex valuePattern(
        R"(s\.p([0-9]+)|n([0-9]+)\.([A-Za-z_][A-Za-z0-9_]*)_cache)");
    std::string output;
    size_t position = 0;
    for (auto iterator =
             std::sregex_iterator(input.begin(), input.end(), valuePattern);
         iterator != std::sregex_iterator(); ++iterator) {
      const auto &match = *iterator;
      output.append(input, position,
                    static_cast<size_t>(match.position()) - position);
      const auto id = valueNode(match);
      if (id && (nodes[*id].aliasOf || nodes[*id].inlineExpression)) {
        const Node &target = nodes[canonicalNode(*id)];
        if (target.inlineExpression) {
          output += "(" + *target.inlineExpression + ")";
        } else {
          output += nodeValue(target.id);
        }
      } else {
        output += match.str();
      }
      position = static_cast<size_t>(match.position() + match.length());
    }
    output.append(input, position, input.size() - position);
    return output;
  }

  std::string materializeBindingValue(size_t id,
                                      std::unordered_set<size_t> &active) {
    id = canonicalNode(id);
    const Node &node = nodes[id];
    if (node.kind == NodeKind::Comb) {
      return nodeValue(id);
    }
    if (!node.expressionReady) {
      error = "binding node was not prepared: " + node.instance->path + "." +
              node.name;
      return {};
    }
    if (!active.insert(id).second) {
      error = "recursive optimized port binding: " + node.instance->path +
              "." + node.name;
      return {};
    }

    const std::string input =
        node.inlineExpression ? *node.inlineExpression : node.expression;
    static const std::regex portPattern(R"(s\.p([0-9]+))");
    std::string output;
    size_t position = 0;
    for (auto iterator =
             std::sregex_iterator(input.begin(), input.end(), portPattern);
         iterator != std::sregex_iterator(); ++iterator) {
      const auto &match = *iterator;
      output.append(input, position,
                    static_cast<size_t>(match.position()) - position);
      const size_t dependency = std::stoull(match[1].str());
      if (dependency >= nodes.size()) {
        error = "invalid optimized port binding node " +
                std::to_string(dependency);
        active.erase(id);
        return {};
      }
      output += "(" + materializeBindingValue(dependency, active) + ")";
      if (!error.empty()) {
        active.erase(id);
        return {};
      }
      position = static_cast<size_t>(match.position() + match.length());
    }
    output.append(input, position, input.size() - position);
    active.erase(id);
    return output;
  }

  void collectNodeUses(const std::string &input,
                       std::unordered_map<size_t, size_t> &uses) const {
    static const std::regex valuePattern(
        R"(s\.p([0-9]+)|n([0-9]+)\.([A-Za-z_][A-Za-z0-9_]*)_cache)");
    for (auto iterator =
             std::sregex_iterator(input.begin(), input.end(), valuePattern);
         iterator != std::sregex_iterator(); ++iterator) {
      if (const auto id = valueNode(*iterator)) {
        ++uses[*id];
      }
    }
  }

  void addConcreteDependencies(size_t id, std::vector<size_t> &output) const {
    id = canonicalNode(id);
    if (nodes[id].inlineExpression) {
      for (const size_t dependency : nodes[id].dependencies) {
        addConcreteDependencies(dependency, output);
      }
    } else {
      output.push_back(id);
    }
  }

  void optimizeGraph(std::string &work) {
    // The schedule is topological, so an alias target has already been
    // canonicalized when its consumer is inspected.
    for (const size_t id : schedule) {
      Node &node = nodes[id];
      if (node.kind != NodeKind::Port || node.dependencies.size() != 1) {
        continue;
      }
      const size_t dependency = node.dependencies.front();
      if (trim(node.expression) == nodeValue(dependency)) {
        node.aliasOf = canonicalNode(dependency);
      }
    }

    for (Node &node : nodes) {
      if (!node.expressionReady || node.aliasOf) {
        continue;
      }
      node.expression = replaceAliases(node.expression);
      for (size_t &dependency : node.dependencies) {
        dependency = canonicalNode(dependency);
      }
      std::sort(node.dependencies.begin(), node.dependencies.end());
      node.dependencies.erase(
          std::unique(node.dependencies.begin(), node.dependencies.end()),
          node.dependencies.end());
    }
    work = replaceAliases(work);
    schedule.erase(std::remove_if(schedule.begin(), schedule.end(),
                                  [this](size_t id) {
                                    return nodes[id].aliasOf.has_value();
                                  }),
                   schedule.end());

    std::unordered_map<size_t, size_t> uses;
    for (const size_t id : schedule) {
      collectNodeUses(nodes[id].expression, uses);
    }
    collectNodeUses(work, uses);
    static const std::regex directFieldPattern(
        R"(^n[0-9]+\.[A-Za-z_][A-Za-z0-9_]*$)");
    for (const size_t id : schedule) {
      Node &node = nodes[id];
      if (node.kind != NodeKind::Port || !node.instance->parent) {
        continue;
      }
      if (uses[id] == 1 ||
          std::regex_match(node.expression, directFieldPattern)) {
        // Dependencies precede consumers in schedule, so this also
        // expands any already-selected inline port exactly once.
        node.inlineExpression = replaceAliases(node.expression);
      }
    }

    for (Node &node : nodes) {
      if (!node.expressionReady || node.aliasOf || node.inlineExpression) {
        continue;
      }
      node.expression = replaceAliases(node.expression);
      std::vector<size_t> concreteDependencies;
      for (const size_t dependency : node.dependencies) {
        addConcreteDependencies(dependency, concreteDependencies);
      }
      std::sort(concreteDependencies.begin(), concreteDependencies.end());
      concreteDependencies.erase(
          std::unique(concreteDependencies.begin(), concreteDependencies.end()),
          concreteDependencies.end());
      node.dependencies = std::move(concreteDependencies);
    }
    work = replaceAliases(work);
    schedule.erase(
        std::remove_if(schedule.begin(), schedule.end(),
                       [this](size_t id) {
                         return nodes[id].inlineExpression.has_value();
                       }),
        schedule.end());
  }

  std::unordered_set<std::string>
  localVariables(const std::string &body) const {
    std::unordered_set<std::string> result{"edge"};
    static const std::regex declaration(
        R"((?:^|[;{}])\s*(?:const\s+)?(?:auto|bool|size_t|u?int(?:8|16|32|64)_t|[A-Za-z_]\w*(?:::\w+)*(?:<[^;=]+>)?)\s+([A-Za-z_]\w*)\s*(?:[=;{]))");
    for (auto iterator =
             std::sregex_iterator(body.begin(), body.end(), declaration);
         iterator != std::sregex_iterator(); ++iterator) {
      result.insert((*iterator)[1].str());
    }
    return result;
  }

  std::vector<std::string> splitWork(const std::string &work,
                                     size_t statementsPerChunk,
                                     size_t bytesPerChunk = 250000) const {
    // A generated _work body normally consists only of assignments and
    // balanced conditional blocks.  Keep a conservative fallback for
    // hand-written models whose local variables may be live across
    // multiple top-level statements.
    std::vector<std::string> units;
    std::istringstream input(work);
    std::ostringstream unit;
    std::string line;
    int braceDepth = 0;
    while (std::getline(input, line)) {
      unit << line << '\n';
      for (const char character : line) {
        if (character == '{') {
          ++braceDepth;
        } else if (character == '}') {
          --braceDepth;
        }
      }
      if (braceDepth < 0) {
        return {work};
      }
      if (braceDepth == 0) {
        units.push_back(unit.str());
        unit.str({});
        unit.clear();
      }
    }
    if (braceDepth != 0 || !trim(unit.str()).empty()) {
      return {work};
    }

    static const std::regex topLevelDeclaration(
        R"(^\s*(?:const\s+)?(?:auto|bool|size_t|u?int(?:8|16|32|64)_t|[A-Za-z_]\w*(?:::\w+)*(?:<[^;=]+>)?)\s+[A-Za-z_]\w*\s*(?:[=;{]))");
    for (const std::string &item : units) {
      // A declaration nested in an if/loop block remains in that unit
      // and is safe.  A declaration which itself is a top-level unit
      // may be consumed by later units, so retain the monolithic work
      // function for that uncommon hand-written case.
      if (std::regex_search(item, topLevelDeclaration)) {
        return {work};
      }
    }

    std::vector<std::string> chunks;
    std::ostringstream chunk;
    size_t statementCount = 0;
    for (const std::string &item : units) {
      if (statementCount != 0 &&
          (statementCount == statementsPerChunk ||
           chunk.tellp() + static_cast<std::streamoff>(item.size()) >
               static_cast<std::streamoff>(bytesPerChunk))) {
        chunks.push_back(chunk.str());
        chunk.str({});
        chunk.clear();
        statementCount = 0;
      }
      chunk << item;
      ++statementCount;
    }
    if (!chunk.str().empty()) {
      chunks.push_back(chunk.str());
    }
    return chunks;
  }

  std::string flattenWork(Instance &instance, const std::string &indent,
                          std::unordered_set<size_t> &active) {
    if (active.contains(instance.id)) {
      error = "recursive _work call at " + instance.path;
      return {};
    }
    active.insert(instance.id);
    const std::string interior = bodyInterior(instance.type->workBody);
    const auto locals = localVariables(interior);
    std::ostringstream output;
    std::istringstream input(interior);
    std::string line;
    while (std::getline(input, line)) {
      const std::string stripped = trim(line);
      if (stripped.empty()) {
        continue;
      }
      const auto workCall = stripped.find("->_work(");
      if (workCall != std::string::npos) {
        size_t childStart = workCall;
        while (childStart > 0 && identifierPart(stripped[childStart - 1])) {
          --childStart;
        }
        const std::string childName =
            stripped.substr(childStart, workCall - childStart);
        const auto child = instance.children.find(childName);
        if (child == instance.children.end()) {
          error = "cannot resolve child _work call in " + instance.path + ": " +
                  stripped;
          active.erase(instance.id);
          return {};
        }
        std::string nested = flattenWork(*child->second, indent + "  ", active);
        if (!error.empty()) {
          active.erase(instance.id);
          return {};
        }
        const auto ifPosition = stripped.find("if");
        if (ifPosition == 0) {
          const auto opening = stripped.find('(');
          const auto closing = matchingDelimiter(stripped, opening, '(', ')');
          if (!closing || *closing > childStart) {
            error = "unsupported conditional child _work call in " +
                    instance.path + ": " + stripped;
            active.erase(instance.id);
            return {};
          }
          std::string condition =
              stripped.substr(opening + 1, *closing - opening - 1);
          condition = rewrite(condition, instance, nullptr, locals);
          for (const std::string &nestedChunk : splitWork(nested, 400)) {
            output << indent << "if (" << condition << ") {\n"
                   << nestedChunk << indent << "}\n";
          }
        } else {
          output << nested;
        }
        continue;
      }
      std::string rewritten = rewrite(stripped, instance, nullptr, locals);
      if (!error.empty()) {
        active.erase(instance.id);
        return {};
      }
      // _work's bool argument is always false in the generated runtime.
      rewritten =
          std::regex_replace(rewritten, std::regex(R"(\bedge\b)"), "false");
      output << indent << rewritten << '\n';
    }
    active.erase(instance.id);
    return output.str();
  }

  std::string flattenStrobe(Instance &instance, const std::string &indent,
                            std::unordered_set<size_t> &active) {
    if (active.contains(instance.id)) {
      error = "recursive _strobe call at " + instance.path;
      return {};
    }
    active.insert(instance.id);
    const std::string interior = bodyInterior(instance.type->strobeBody);
    const auto locals = localVariables(interior);
    std::ostringstream output;
    std::istringstream input(interior);
    std::string line;
    while (std::getline(input, line)) {
      const std::string stripped = trim(line);
      if (stripped.empty()) {
        continue;
      }
      const auto strobeCall = stripped.find("->_strobe(");
      if (strobeCall != std::string::npos) {
        size_t childStart = strobeCall;
        while (childStart > 0 && identifierPart(stripped[childStart - 1])) {
          --childStart;
        }
        const std::string childName =
            stripped.substr(childStart, strobeCall - childStart);
        const auto child = instance.children.find(childName);
        if (child == instance.children.end()) {
          error = "cannot resolve child _strobe call in " + instance.path +
                  ": " + stripped;
          active.erase(instance.id);
          return {};
        }
        output << flattenStrobe(*child->second, indent, active);
        if (!error.empty()) {
          active.erase(instance.id);
          return {};
        }
        continue;
      }
      const std::string rewritten =
          rewrite(stripped, instance, nullptr, locals);
      if (!error.empty()) {
        active.erase(instance.id);
        return {};
      }
      output << indent << rewritten << '\n';
    }
    active.erase(instance.id);
    return output.str();
  }

  bool buildBindings() {
    for (Instance *instance : instances) {
      std::string parseError;
      const auto assignments =
          parseAssignments(instance->type->assignBody, parseError);
      if (!parseError.empty()) {
        error = instance->path + ": " + parseError;
        return false;
      }
      for (const Assignment &assignment : assignments) {
        Instance *target = instance;
        if (!assignment.targetInstance.empty()) {
          const auto child = instance->children.find(assignment.targetInstance);
          if (child == instance->children.end()) {
            error = "unknown _assign target " + instance->path + "." +
                    assignment.targetInstance;
            return false;
          }
          target = child->second;
        }
        const auto field = target->type->fields.find(assignment.targetPort);
        if (field == target->type->fields.end() ||
            field->second.kind != FieldKind::Port) {
          error = "_assign target is not a port: " + target->path + "." +
                  assignment.targetPort;
          return false;
        }
        const std::string key =
            nodeKey(target->id, NodeKind::Port, assignment.targetPort);
        if (bindings.contains(key)) {
          error = "port assigned more than once: " + target->path + "." +
                  assignment.targetPort;
          return false;
        }
        bindings[key] = {instance, assignment.expression};
      }
    }
    return true;
  }

  void findCombExpressions() {
    for (auto &[className, info] : classes) {
      for (const auto &[methodName, body] : info.methods) {
        if (methodName == "_assign" || methodName == "_work" ||
            methodName == "_strobe") {
          continue;
        }
        if (info.fields.contains(methodName + "_cache")) {
          if (auto expression = combExpression(body, methodName)) {
            info.combExpressions[methodName] = *expression;
          }
        }
      }
    }
  }

  bool emit(const std::string &rootName, const std::string &directory) {
    findCombExpressions();
    const auto rootType = classes.find(rootName);
    if (rootType == classes.end()) {
      error = "root module class not found: " + rootName;
      return false;
    }
    std::unordered_set<std::string> activeClasses;
    Instance *root = addInstance(rootType->second, nullptr, {}, activeClasses);
    if (!root || !buildBindings()) {
      return false;
    }

    // The roots are state updates in _work and externally visible root
    // outputs.  prepareNode() discovers their transitive comb/port
    // dependencies.  Do not make every named comb a root: generated RTL
    // contains many intermediates that are never consumed, and evaluating
    // those would defeat the purpose of the global dependency schedule.
    for (const auto &[name, field] : root->type->fields) {
      if (field.kind == FieldKind::Port &&
          bindings.contains(nodeKey(root->id, NodeKind::Port, name))) {
        nodes[getNode(*root, NodeKind::Port, name)].referenced = true;
      }
    }

    std::unordered_set<size_t> activeWork;
    std::string work = flattenWork(*root, "  ", activeWork);
    if (!error.empty()) {
      return false;
    }
    std::unordered_set<size_t> activeStrobe;
    std::string strobe = flattenStrobe(*root, "  ", activeStrobe);
    if (!error.empty()) {
      return false;
    }

    // External models still consume their ports through the generated
    // function_ref API.  Make those bindings graph roots so their lambdas can
    // read already-scheduled caches instead of calling legacy comb methods.
    for (Instance *instance : instances) {
      if (instance->type->workBody.find("firtool_cpphdl_external::work") ==
          std::string::npos) {
        continue;
      }
      for (const auto &[name, field] : instance->type->fields) {
        if (field.kind != FieldKind::Port ||
            !bindings.contains(nodeKey(instance->id, NodeKind::Port, name))) {
          continue;
        }
        nodes[getNode(*instance, NodeKind::Port, name)].referenced = true;
      }
    }

    // getNode may grow nodes while expressions are rewritten.  Iterate by
    // index and visit every referenced node after the set stabilizes.
    std::vector<size_t> stack;
    for (size_t id = 0; id < nodes.size(); ++id) {
      if (nodes[id].referenced && !visitNode(id, stack)) {
        return false;
      }
    }
    optimizeGraph(work);

    struct BindingStatement {
      std::string text;
      std::set<size_t> usedInstances;
    };
    std::vector<BindingStatement> bindingStatements;
    for (Instance *instance : instances) {
      std::string parseError;
      const auto assignments =
          parseAssignments(instance->type->assignBody, parseError);
      if (!parseError.empty()) {
        error = instance->path + ": " + parseError;
        return false;
      }
      for (const Assignment &assignment : assignments) {
        Instance *target = instance;
        if (!assignment.targetInstance.empty()) {
          const auto child = instance->children.find(assignment.targetInstance);
          if (child == instance->children.end()) {
            error = "unknown flattened binding target " + instance->path +
                    "." + assignment.targetInstance;
            return false;
          }
          target = child->second;
        }
        if (target->type->workBody.find("firtool_cpphdl_external::work") ==
            std::string::npos) {
          continue;
        }
        const auto node = nodeIds.find(
            nodeKey(target->id, NodeKind::Port, assignment.targetPort));
        if (node == nodeIds.end()) {
          error = "missing optimized external port node " + target->path +
                  "." + assignment.targetPort;
          return false;
        }
        std::unordered_set<size_t> activeBindings;
        const std::string expression =
            materializeBindingValue(node->second, activeBindings);
        if (!error.empty()) {
          return false;
        }
        const std::string statement =
            "  " + target->alias + "." + assignment.targetPort + " = " +
            assignment.macro + "(" + expression + ");\n";
        bindingStatements.push_back(BindingStatement{
            statement,
            referencedInstances(statement, {target->id, instance->id})});
      }
    }

    trees.clear();
    for (const size_t id : schedule) {
      const Node &node = nodes[id];
      CombDeps item{
          node.instance->path, node.instance->type->name, node.name, {}};
      for (const size_t dependency : node.dependencies) {
        const Node &dep = nodes[dependency];
        item.dependencies.push_back(CombDeps{
            dep.instance->path, dep.instance->type->name, dep.name, {}});
      }
      trees.push_back(std::move(item));
    }

    std::filesystem::create_directories(directory);
    const std::string shortRoot = rootName.substr(
        rootName.rfind("::") == std::string::npos ? 0
                                                  : rootName.rfind("::") + 2);
    const std::regex staleChunkPattern(
        "^" + shortRoot +
        R"(_optimized_combs_(?:(?:work|bind|strobe|model)_)?[0-9]+\.cpp$)");
    for (const auto &entry : std::filesystem::directory_iterator(directory)) {
      if (entry.is_regular_file() &&
          std::regex_match(entry.path().filename().string(),
                           staleChunkPattern)) {
        std::filesystem::remove(entry.path());
      }
    }
    const std::filesystem::path headerPath =
        std::filesystem::path(directory) / (shortRoot + "_optimized_combs.h");
    const std::filesystem::path sourcePath =
        std::filesystem::path(directory) / (shortRoot + "_optimized_combs.cpp");
    const std::filesystem::path internalPath =
        std::filesystem::path(directory) /
        (shortRoot + "_optimized_combs_internal.h");
    const auto finishOutput = [this](std::ofstream &output,
                                     const std::filesystem::path &path) {
      output.close();
      if (output) {
        return true;
      }
      error = "cannot finish writing " + path.string();
      return false;
    };

    std::ofstream header(headerPath);
    if (!header) {
      error = "cannot write " + headerPath.string();
      return false;
    }
    header << "#pragma once\n\nclass " << shortRoot << ";\n"
           << "void bind_optimized_ports(" << shortRoot << "& obj);\n"
           << "void calc_all(" << shortRoot << "& obj);\n"
           << "void commit_optimized_regs(" << shortRoot << "& obj);\n";
    if (!finishOutput(header, headerPath)) {
      return false;
    }

    std::set<std::string> headers;
    for (const auto &[name, info] : classes) {
      if (!info.header.empty() && info.header.ends_with(".h")) {
        headers.insert(info.header);
      }
    }

    constexpr size_t valuesPerChunk = 400;
    constexpr size_t bindingsPerChunk = 400;
    constexpr size_t modelTypesPerChunk = 50;
    const size_t chunkCount =
        (schedule.size() + valuesPerChunk - 1) / valuesPerChunk;
    const size_t bindChunkCount =
        (bindingStatements.size() + bindingsPerChunk - 1) / bindingsPerChunk;
    const std::vector<std::string> workChunks = splitWork(work, 400);
    const std::vector<std::string> strobeChunks = splitWork(strobe, 400);
    std::map<std::string, const ClassInfo *> concreteTypes;
    for (const Instance *instance : instances) {
      if (!instance->type->constructorBody.empty() ||
          !instance->type->destructorBody.empty()) {
        concreteTypes[instance->type->name] = instance->type;
      }
    }
    std::vector<const ClassInfo *> modelTypes;
    for (const auto &[name, type] : concreteTypes) {
      modelTypes.push_back(type);
    }
    const size_t modelChunkCount =
        (modelTypes.size() + modelTypesPerChunk - 1) / modelTypesPerChunk;

    std::ofstream internal(internalPath);
    if (!internal) {
      error = "cannot write " + internalPath.string();
      return false;
    }
    internal << "#pragma once\n#include <memory>\n#include "
                "<type_traits>\n#include <utility>\n";
    for (const std::string &include : sourceIncludes) {
      if (!include.ends_with(".h") || headers.contains(include) ||
          include == shortRoot + ".h" || include == "cpphdl_support.h") {
        continue;
      }
      internal << "#include \"" << include << "\"\n";
    }
    internal << "#define private public\n";
    // Root first makes the declaration in the generated API obvious.
    internal << "#include \"" << shortRoot << ".h\"\n";
    headers.erase(shortRoot + ".h");
    for (const std::string &include : headers) {
      internal << "#include \"" << include << "\"\n";
    }
    internal << "#undef private\n\n"
             << "struct " << shortRoot << "_optimized_combs_state {\n";
    for (const size_t id : schedule) {
      const Node &node = nodes[id];
      if (node.kind != NodeKind::Port) {
        continue;
      }
      internal << "  std::remove_reference_t<decltype(std::declval<"
               << node.instance->type->name << "&>()." << node.name << "())> p"
               << id << ";\n";
    }
    internal << "};\n\n";
    for (size_t chunk = 0; chunk < chunkCount; ++chunk) {
      internal << "void " << shortRoot << "_optimized_combs_chunk_" << chunk
               << "(" << shortRoot << "&, " << shortRoot
               << "_optimized_combs_state&);\n";
    }
    for (size_t chunk = 0; chunk < bindChunkCount; ++chunk) {
      internal << "void " << shortRoot << "_optimized_combs_bind_chunk_"
               << chunk << "(" << shortRoot << "&);\n";
    }
    for (size_t chunk = 0; chunk < workChunks.size(); ++chunk) {
      internal << "void " << shortRoot << "_optimized_combs_work_chunk_"
               << chunk << "(" << shortRoot << "&, " << shortRoot
               << "_optimized_combs_state&);\n";
    }
    for (size_t chunk = 0; chunk < strobeChunks.size(); ++chunk) {
      internal << "void " << shortRoot << "_optimized_combs_strobe_chunk_"
               << chunk << "(" << shortRoot << "&);\n";
    }

    for (size_t chunk = 0; chunk < bindChunkCount; ++chunk) {
      const size_t begin = chunk * bindingsPerChunk;
      const size_t end =
          std::min(bindingStatements.size(), begin + bindingsPerChunk);
      std::set<size_t> usedInstances{0};
      std::ostringstream body;
      for (size_t position = begin; position < end; ++position) {
        body << bindingStatements[position].text;
        usedInstances.insert(bindingStatements[position].usedInstances.begin(),
                             bindingStatements[position].usedInstances.end());
      }
      const std::filesystem::path chunkPath =
          std::filesystem::path(directory) /
          (shortRoot + "_optimized_combs_bind_" + std::to_string(chunk) +
           ".cpp");
      std::ofstream chunkSource(chunkPath);
      if (!chunkSource) {
        error = "cannot write " + chunkPath.string();
        return false;
      }
      chunkSource << "#include \"" << shortRoot
                  << "_optimized_combs_internal.h\"\n\nvoid " << shortRoot
                  << "_optimized_combs_bind_chunk_" << chunk << "("
                  << shortRoot << "& obj) {\n";
      emitAliases(chunkSource, referencedInstances({}, usedInstances));
      chunkSource << body.str() << "}\n";
      if (!finishOutput(chunkSource, chunkPath)) {
        return false;
      }
    }
    if (!finishOutput(internal, internalPath)) {
      return false;
    }

    for (size_t chunk = 0; chunk < modelChunkCount; ++chunk) {
      const size_t begin = chunk * modelTypesPerChunk;
      const size_t end =
          std::min(modelTypes.size(), begin + modelTypesPerChunk);
      const std::filesystem::path chunkPath =
          std::filesystem::path(directory) /
          (shortRoot + "_optimized_combs_model_" + std::to_string(chunk) +
           ".cpp");
      std::ofstream chunkSource(chunkPath);
      if (!chunkSource) {
        error = "cannot write " + chunkPath.string();
        return false;
      }
      chunkSource << "#include \"" << shortRoot
                  << "_optimized_combs_internal.h\"\n\n";
      for (size_t position = begin; position < end; ++position) {
        const ClassInfo &type = *modelTypes[position];
        const size_t separator = type.name.rfind("::");
        const std::string baseName =
            separator == std::string::npos ? type.name
                                           : type.name.substr(separator + 2);
        if (!type.constructorBody.empty()) {
          chunkSource << type.name << "::" << baseName << "() "
                      << type.constructorBody << "\n\n";
        }
        if (!type.destructorBody.empty()) {
          chunkSource << type.name << "::~" << baseName << "() "
                      << type.destructorBody << "\n\n";
        }
      }
      if (!finishOutput(chunkSource, chunkPath)) {
        return false;
      }
    }

    std::unordered_set<size_t> emitted;
    for (size_t chunk = 0; chunk < chunkCount; ++chunk) {
      const size_t begin = chunk * valuesPerChunk;
      const size_t end = std::min(schedule.size(), begin + valuesPerChunk);
      std::ostringstream body;
      std::set<size_t> usedInstances{0};
      for (size_t position = begin; position < end; ++position) {
        const size_t id = schedule[position];
        if (!emitted.insert(id).second) {
          error = "internal error: scheduled comb twice";
          return false;
        }
        const Node &node = nodes[id];
        for (const size_t dependency : node.dependencies) {
          if (!emitted.contains(dependency)) {
            error = "internal error: dependency emitted after consumer " +
                    node.instance->path + "." + node.name;
            return false;
          }
        }
        usedInstances.insert(node.instance->id);
        usedInstances = referencedInstances(node.expression, usedInstances);
        // The flattened schedule evaluates every cache exactly once.  The
        // accessor timestamps are irrelevant because calc_all never calls the
        // memoized accessors.
        body << "  " << nodeValue(id) << " = " << node.expression << ";\n";
      }
      const std::filesystem::path chunkPath =
          std::filesystem::path(directory) /
          (shortRoot + "_optimized_combs_" + std::to_string(chunk) + ".cpp");
      std::ofstream chunkSource(chunkPath);
      if (!chunkSource) {
        error = "cannot write " + chunkPath.string();
        return false;
      }
      chunkSource << "#include \"" << shortRoot
                  << "_optimized_combs_internal.h\"\n\nvoid " << shortRoot
                  << "_optimized_combs_chunk_" << chunk << "(" << shortRoot
                  << "& obj, " << shortRoot << "_optimized_combs_state& s) {\n";
      emitAliases(chunkSource, referencedInstances({}, usedInstances));
      chunkSource << body.str() << "}\n";
      if (!finishOutput(chunkSource, chunkPath)) {
        return false;
      }
    }

    for (size_t chunk = 0; chunk < workChunks.size(); ++chunk) {
      const std::string &workChunk = workChunks[chunk];
      const std::filesystem::path chunkPath =
          std::filesystem::path(directory) /
          (shortRoot + "_optimized_combs_work_" + std::to_string(chunk) +
           ".cpp");
      std::ofstream chunkSource(chunkPath);
      if (!chunkSource) {
        error = "cannot write " + chunkPath.string();
        return false;
      }
      chunkSource << "#include \"" << shortRoot
                  << "_optimized_combs_internal.h\"\n\nvoid " << shortRoot
                  << "_optimized_combs_work_chunk_" << chunk << "(" << shortRoot
                  << "& obj, " << shortRoot << "_optimized_combs_state& s) {\n";
      emitAliases(chunkSource, referencedInstances(workChunk, {0}));
      chunkSource << '\n' << workChunk << "}\n";
      if (!finishOutput(chunkSource, chunkPath)) {
        return false;
      }
    }

    for (size_t chunk = 0; chunk < strobeChunks.size(); ++chunk) {
      const std::string &strobeChunk = strobeChunks[chunk];
      const std::filesystem::path chunkPath =
          std::filesystem::path(directory) /
          (shortRoot + "_optimized_combs_strobe_" + std::to_string(chunk) +
           ".cpp");
      std::ofstream chunkSource(chunkPath);
      if (!chunkSource) {
        error = "cannot write " + chunkPath.string();
        return false;
      }
      chunkSource << "#include \"" << shortRoot
                  << "_optimized_combs_internal.h\"\n\nvoid " << shortRoot
                  << "_optimized_combs_strobe_chunk_" << chunk << "("
                  << shortRoot << "& obj) {\n";
      emitAliases(chunkSource, referencedInstances(strobeChunk, {0}));
      chunkSource << '\n' << strobeChunk << "}\n";
      if (!finishOutput(chunkSource, chunkPath)) {
        return false;
      }
    }

    std::ofstream source(sourcePath);
    if (!source) {
      error = "cannot write " + sourcePath.string();
      return false;
    }
    source << "#include \"" << shortRoot << "_optimized_combs_internal.h\"\n"
           << "#include \"" << shortRoot << "_optimized_combs.h\"\n\n"
           << "void bind_optimized_ports(" << shortRoot << "& obj) {\n";
    for (size_t chunk = 0; chunk < bindChunkCount; ++chunk) {
      source << "  " << shortRoot << "_optimized_combs_bind_chunk_" << chunk
             << "(obj);\n";
    }
    source << "}\n\n"
           << "void calc_all(" << shortRoot << "& obj) {\n"
           << "  " << shortRoot << "_optimized_combs_state s;\n";
    for (size_t chunk = 0; chunk < chunkCount; ++chunk) {
      source << "  " << shortRoot << "_optimized_combs_chunk_" << chunk
             << "(obj, s);\n";
    }
    for (size_t chunk = 0; chunk < workChunks.size(); ++chunk) {
      source << "  " << shortRoot << "_optimized_combs_work_chunk_" << chunk
             << "(obj, s);\n";
    }
    source << "}\n\nvoid commit_optimized_regs(" << shortRoot << "& obj) {\n";
    for (size_t chunk = 0; chunk < strobeChunks.size(); ++chunk) {
      source << "  " << shortRoot << "_optimized_combs_strobe_chunk_" << chunk
             << "(obj);\n";
    }
    source << "}\n";
    if (!finishOutput(source, sourcePath)) {
      return false;
    }
    llvm::outs() << "CppHDL comb optimizer: " << instances.size()
                 << " instances, " << schedule.size() << " scheduled values, "
                 << chunkCount << " comb chunks, " << bindChunkCount
                 << " bind chunks, " << workChunks.size() << " work chunks, "
                 << strobeChunks.size() << " strobe chunks, "
                 << modelChunkCount << " model chunks\n"
                 << "  " << headerPath.string() << "\n"
                 << "  " << sourcePath.string() << "\n"
                 << "  " << internalPath.string() << "\n";
    return true;
  }
};

CombsOptimizer::CombsOptimizer() : impl(std::make_unique<Impl>()) {}
CombsOptimizer::~CombsOptimizer() = default;
CombsOptimizer::CombsOptimizer(CombsOptimizer &&) noexcept = default;
CombsOptimizer &CombsOptimizer::operator=(CombsOptimizer &&) noexcept = default;

void CombsOptimizer::collect(clang::ASTContext &context) {
  Collector collector(impl->classes, impl->sourceIncludes, context);
  collector.collectMainFileIncludes();
  collector.TraverseDecl(context.getTranslationUnitDecl());
}

bool CombsOptimizer::generate(const std::string &rootModule,
                              const std::string &outputDirectory) {
  impl->error.clear();
  if (impl->emit(rootModule, outputDirectory)) {
    return true;
  }
  llvm::errs() << "cpphdl --optimize-combs: " << impl->error << "\n";
  return false;
}

const std::vector<CombDeps> &CombsOptimizer::dependencyTrees() const {
  return impl->trees;
}

} // namespace cpphdl
