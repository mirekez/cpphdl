#include "Combs.h"

#include "clang/AST/ASTContext.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/TypeLoc.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Lex/Lexer.h"
#include "llvm/Support/raw_ostream.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#if defined(__GLIBC__)
#include <malloc.h>
#endif

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

void releaseTemporaryAllocatorPages() {
#if defined(__GLIBC__)
  malloc_trim(0);
#endif
}

void replaceText(std::string &text, const std::string &from,
                 const std::string &to) {
  if (from.empty()) {
    return;
  }
  size_t position = 0;
  while ((position = text.find(from, position)) != std::string::npos) {
    text.replace(position, from.size(), to);
    position += to.size();
  }
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

std::string sourceText(clang::SourceRange sourceRange,
                       clang::ASTContext &context) {
  if (sourceRange.isInvalid()) {
    return {};
  }
  const auto range = clang::CharSourceRange::getTokenRange(sourceRange);
  return clang::Lexer::getSourceText(range, context.getSourceManager(),
                                     context.getLangOpts())
      .str();
}

std::string astStatementText(const clang::Stmt *statement,
                             clang::ASTContext &context,
                             const clang::PrintingPolicy &policy) {
  if (!statement) {
    return {};
  }
  if (const auto *conditional = clang::dyn_cast<clang::IfStmt>(statement);
      conditional && conditional->isConstexpr()) {
    const auto selected = conditional->getNondiscardedCase(context);
    if (selected) {
      return *selected ? astStatementText(*selected, context, policy) : "{}";
    }
  }
  if (const auto *compound = clang::dyn_cast<clang::CompoundStmt>(statement)) {
    std::ostringstream body;
    body << "{\n";
    for (const clang::Stmt *child : compound->body()) {
      std::string text = astStatementText(child, context, policy);
      // _LAZY_COMB contributes an early-return guard and its clock update.
      // calc_all() evaluates each scheduled node once, so retaining either
      // statement would incorrectly couple optimized evaluation to that guard.
      if (text.find("__prev__system_clock_") != std::string::npos) {
        continue;
      }
      body << text;
      if (!text.empty() && text.back() != '\n') {
        body << '\n';
      }
    }
    body << "}";
    return body.str();
  }

  const auto &sourceManager = context.getSourceManager();
  const auto statementRange = statement->getSourceRange();
  const bool statementInOneFile =
      statementRange.isValid() &&
      sourceManager.isWrittenInSameFile(
          sourceManager.getSpellingLoc(statementRange.getBegin()),
          sourceManager.getSpellingLoc(statementRange.getEnd()));
  std::string text =
      statementInOneFile ? sourceText(statement, context) : std::string{};
  if (text.empty()) {
    llvm::raw_string_ostream stream(text);
    statement->printPretty(stream, nullptr, policy, 2, "\n", &context);
    stream.flush();
  }
  // Clang prints dependent template members with an explicit `this->`.
  // The optimizer rewrites unqualified module members through the concrete
  // instance, so normalize the printer-added qualification before parsing.
  replaceText(text, "this->", "");
  if ((clang::isa<clang::Expr>(statement) ||
       clang::isa<clang::ReturnStmt>(statement) ||
       clang::isa<clang::DeclStmt>(statement) ||
       clang::isa<clang::BreakStmt>(statement) ||
       clang::isa<clang::ContinueStmt>(statement) ||
       clang::isa<clang::GotoStmt>(statement)) &&
      (text.empty() || text.back() != ';')) {
    text += ';';
  }
  return text;
}

std::string astCombBody(const clang::Stmt *statement,
                        clang::ASTContext &context) {
  if (!clang::isa_and_nonnull<clang::CompoundStmt>(statement)) {
    return {};
  }
  clang::PrintingPolicy policy(context.getLangOpts());
  return astStatementText(statement, context, policy);
}

bool derivesModule(const clang::CXXRecordDecl *record) {
  if (!record) {
    return false;
  }
  record = record->getDefinition();
  if (!record) {
    return false;
  }
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

std::string recordTypeName(const clang::CXXRecordDecl *record,
                           clang::ASTContext &context) {
  if (!record) {
    return {};
  }
  clang::PrintingPolicy policy(context.getLangOpts());
  policy.SuppressTagKeyword = true;
  policy.FullyQualifiedName = true;
  return clang::QualType(record->getTypeForDecl(), 0).getAsString(policy);
}

enum class FieldKind { Other, Port, Reg, Child, ChildArray };

struct FieldInfo {
  std::string name;
  std::string type;
  std::string childClass;
  std::string portModuleClass;
  std::string initializer;
  std::vector<size_t> portModuleDimensions;
  size_t childCount = 0;
  std::vector<size_t> childDimensions;
  bool childPointer = false;
  FieldKind kind = FieldKind::Other;
};

struct ClassInfo {
  using Body = std::shared_ptr<const std::string>;

  std::string name;
  std::string templateBase;
  std::string header;
  std::vector<std::string> templateParameterOrder;
  std::map<std::string, std::string> templateParameterDeclarations;
  std::map<std::string, std::string> templateSubstitutions;
  std::map<std::string, std::string> templateTypeAccess;
  // A type parameter can be exposed only as an element of an array field.
  // Retain that element depth so rewriting recovers the declared value_type,
  // avoiding packed operator[] proxy types and concrete structural spellings.
  std::map<std::string, size_t> templateTypeAccessDepth;
  std::map<std::string, std::string> templateParameterTypes;
  std::set<std::string> typeTemplateParameters;
  std::set<std::string> integralTemplateParameters;
  std::set<std::string> structuralTemplateParameters;
  std::map<std::string, std::set<std::string>> methodTemplateParameters;
  std::map<std::string, uint64_t> constantValues;
  std::set<std::string> nestedTypes;
  std::map<std::string, FieldInfo> fields;
  std::map<std::string, Body> methods;
  std::set<std::string> concreteMethods;
  std::map<std::string, Body> combExpressions;
  std::map<std::string, std::string> combStorage;
  // Semantic work-write metadata is kept beside the collected method body.
  // It records ordinary storage only; register next-state updates remain
  // invisible to current-clock comb evaluation until the strobe commits them.
  std::set<std::string> workMutatedFields;
  Body constructorBody;
  Body destructorBody;
  Body assignBody;
  Body workBody;
  Body strobeBody;
  bool useTemplatePatternMethods = false;
  bool supportsTemplateArgumentDeduction = true;
};

// Flattened work can pass ordinary module fields to output-reference helpers.
// Capture that semantic write boundary without treating reg::_next updates as
// mutations of the current registered value visible to same-clock combs.
struct FieldReferenceCollector
    : clang::RecursiveASTVisitor<FieldReferenceCollector> {
  FieldReferenceCollector(const ClassInfo &info,
                          std::set<std::string> &fields)
      : info(info), fields(fields) {}

  bool VisitMemberExpr(clang::MemberExpr *expression) {
    const auto *field =
        clang::dyn_cast<clang::FieldDecl>(expression->getMemberDecl());
    if (field) {
      const auto item = info.fields.find(field->getNameAsString());
      if (item != info.fields.end() && item->second.kind == FieldKind::Other) {
        fields.insert(field->getNameAsString());
      }
    }
    return true;
  }

  const ClassInfo &info;
  std::set<std::string> &fields;
};

struct WorkMutationCollector
    : clang::RecursiveASTVisitor<WorkMutationCollector> {
  WorkMutationCollector(const ClassInfo &info,
                        std::set<std::string> &fields)
      : info(info), fields(fields) {}

  void collect(const clang::Expr *expression) {
    if (!expression) {
      return;
    }
    FieldReferenceCollector collector(info, fields);
    collector.TraverseStmt(const_cast<clang::Expr *>(expression));
  }

  bool VisitBinaryOperator(clang::BinaryOperator *expression) {
    if (expression->isAssignmentOp()) {
      collect(expression->getLHS());
    }
    return true;
  }

  bool VisitUnaryOperator(clang::UnaryOperator *expression) {
    if (expression->isIncrementDecrementOp()) {
      collect(expression->getSubExpr());
    }
    return true;
  }

  bool VisitCallExpr(clang::CallExpr *expression) {
    const clang::FunctionDecl *callee = expression->getDirectCallee();
    if (!callee) {
      return true;
    }
    const size_t count = std::min<size_t>(expression->getNumArgs(),
                                          callee->getNumParams());
    for (size_t index = 0; index < count; ++index) {
      clang::QualType type = callee->getParamDecl(index)->getType();
      if (type->isReferenceType()) {
        type = type.getNonReferenceType();
      } else if (type->isPointerType()) {
        type = type->getPointeeType();
      } else {
        continue;
      }
      if (!type.isConstQualified()) {
        collect(expression->getArg(index));
      }
    }
    return true;
  }

  const ClassInfo &info;
  std::set<std::string> &fields;
};

const std::string &bodyText(const ClassInfo::Body &body) {
  static const std::string empty;
  return body ? *body : empty;
}

void appendChildElementNames(const std::string &prefix,
                             const std::vector<size_t> &dimensions,
                             size_t dimension,
                             std::vector<std::string> &output) {
  if (dimension == dimensions.size()) {
    output.push_back(prefix);
    return;
  }
  for (size_t index = 0; index < dimensions[dimension]; ++index) {
    appendChildElementNames(prefix + "[" + std::to_string(index) + "]",
                            dimensions, dimension + 1, output);
  }
}

std::vector<std::string> childElementNames(const std::string &fieldName,
                                           const FieldInfo &field) {
  if (field.kind != FieldKind::ChildArray) {
    return {fieldName};
  }
  std::vector<std::string> result;
  appendChildElementNames(fieldName, field.childDimensions, 0, result);
  return result;
}

bool containsIdentifier(const std::string &text,
                        const std::string &identifier);

struct Collector : clang::RecursiveASTVisitor<Collector> {
  explicit Collector(std::map<std::string, ClassInfo> &classes,
                     std::set<std::string> &sourceIncludes,
                     std::unordered_map<std::string, uint64_t> &constantValues,
                     std::string rootName,
                     clang::ASTContext &context)
      : classes(classes), sourceIncludes(sourceIncludes),
        constantValues(constantValues), rootName(std::move(rootName)),
        context(context) {}

  bool TraverseStmt(clang::Stmt *) {
    // Relevant method bodies are captured once by VisitCXXMethodDecl as
    // source or semantic AST text. Walking every expression beneath those
    // methods duplicates that work and cannot discover module declarations.
    return true;
  }

  const clang::CXXRecordDecl *findRoot(const clang::DeclContext *scope) const {
    for (const clang::Decl *declaration : scope->decls()) {
      if (const auto *record = clang::dyn_cast<clang::CXXRecordDecl>(declaration);
          record && record->isCompleteDefinition() &&
          recordTypeName(record, context) == rootName) {
        return record;
      }
      if ((clang::isa<clang::NamespaceDecl>(declaration) ||
           clang::isa<clang::LinkageSpecDecl>(declaration)) &&
          clang::isa<clang::DeclContext>(declaration)) {
        if (const auto *record = findRoot(
                clang::cast<clang::DeclContext>(declaration))) {
          return record;
        }
      }
    }
    return nullptr;
  }

  void collectRootHierarchy() {
    if (rootName.empty()) {
      return;
    }
    const auto *root = findRoot(context.getTranslationUnitDecl());
    if (!root) {
      return;
    }

    // The optimization root fixes the concrete module hierarchy up front.
    // Collect its child fields recursively before the AST walk, allowing that
    // walk to skip method extraction for unrelated header-only models.
    reachableOnly = true;
    collectClass(root);
  }

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
    const std::string name = recordTypeName(declaration, context);
    if (name.empty() || collecting.contains(name)) {
      return;
    }
    auto &info = classes[name];
    if (!info.fields.empty()) {
      return;
    }
    collecting.insert(name);
    info.name = name;
    std::map<std::string, clang::QualType> templateTypeArguments;
    if (const auto *specialization =
            clang::dyn_cast<clang::ClassTemplateSpecializationDecl>(
                declaration)) {
      info.templateBase =
          specialization->getSpecializedTemplate()->getQualifiedNameAsString();
      info.useTemplatePatternMethods =
          specialization->getSpecializationKind() !=
          clang::TSK_ExplicitSpecialization;
      const auto *parameters =
          specialization->getSpecializedTemplate()->getTemplateParameters();
      const auto &arguments = specialization->getTemplateArgs();
      clang::PrintingPolicy policy(context.getLangOpts());
      policy.SuppressTagKeyword = true;
      policy.FullyQualifiedName = true;
      const size_t count = std::min(parameters->size(), arguments.size());
      for (size_t index = 0; index < count; ++index) {
        const auto *parameterDeclaration = parameters->getParam(index);
        const std::string parameter =
            parameterDeclaration->getNameAsString();
        if (parameter.empty()) {
          info.supportsTemplateArgumentDeduction = false;
          continue;
        }
        info.templateParameterOrder.push_back(parameter);
        if (const auto *typeParameter =
                clang::dyn_cast<clang::TemplateTypeParmDecl>(
                    parameterDeclaration)) {
          info.templateParameterDeclarations[parameter] =
              std::string("typename ") +
              (typeParameter->isParameterPack() ? "..." : "") + parameter;
        } else if (const auto *nonType =
                       clang::dyn_cast<clang::NonTypeTemplateParmDecl>(
                           parameterDeclaration)) {
          info.templateParameterDeclarations[parameter] =
              nonType->getType().getAsString(policy) + " " +
              (nonType->isParameterPack() ? "..." : "") + parameter;
        } else {
          info.supportsTemplateArgumentDeduction = false;
        }
        std::string value;
        // Structural template values are not guaranteed to round-trip through
        // TemplateArgument::print. A default argument already has valid source
        // spelling, so preserve it before falling back to semantic printing.
        if (arguments[index].getIsDefaulted()) {
          if (const auto *nonType = clang::dyn_cast<clang::NonTypeTemplateParmDecl>(
                  parameterDeclaration);
              nonType && nonType->hasDefaultArgument()) {
            value = trim(sourceText(
                nonType->getDefaultArgument().getSourceRange(), context));
          }
        }
        if (arguments[index].getKind() ==
            clang::TemplateArgument::StructuralValue) {
          info.structuralTemplateParameters.insert(parameter);
        }
        if (arguments[index].getKind() == clang::TemplateArgument::Type) {
          info.typeTemplateParameters.insert(parameter);
        }
        if (const auto *nonType =
                clang::dyn_cast<clang::NonTypeTemplateParmDecl>(
                    parameterDeclaration)) {
          info.templateParameterTypes[parameter] =
              nonType->getType().getAsString(policy);
          if (nonType->getType()->isIntegralOrEnumerationType()) {
            info.integralTemplateParameters.insert(parameter);
          }
        }
        if (value.empty()) {
          llvm::raw_string_ostream stream(value);
          arguments[index].print(policy, stream, true);
          stream.flush();
        }
        info.templateSubstitutions[parameter] = value;
        if (arguments[index].getKind() == clang::TemplateArgument::Type) {
          templateTypeArguments[parameter] = arguments[index].getAsType();
        }
      }
    }
    const auto location =
        context.getSourceManager().getSpellingLoc(declaration->getLocation());
    if (location.isValid()) {
      info.header =
          context.getSourceManager().getFilename(location).str();
    }
    for (const auto *field : declaration->fields()) {
      FieldInfo item;
      item.name = field->getNameAsString();
      item.type = field->getType().getAsString();
      if (item.type.find("cpphdl::function_ref<") != std::string::npos) {
        item.kind = FieldKind::Port;
        if (field->hasInClassInitializer()) {
          item.initializer =
              sourceText(field->getInClassInitializer(), context);
        }
        if (const auto *portRecord =
                field->getType()->getAsCXXRecordDecl()) {
          if (const auto *specialization =
                  clang::dyn_cast<clang::ClassTemplateSpecializationDecl>(
                      portRecord)) {
            const auto &arguments = specialization->getTemplateArgs();
            if (arguments.size() >= 1 &&
                arguments[0].getKind() == clang::TemplateArgument::Type) {
              clang::QualType valueType = arguments[0].getAsType();
              size_t valueTypeDepth = 0;
              for (const auto &[parameter, argumentType] :
                   templateTypeArguments) {
                if (context.hasSameType(valueType, argumentType)) {
                  info.templateTypeAccess.try_emplace(
                      parameter, "." + item.name + "()");
                  info.templateTypeAccessDepth.try_emplace(parameter,
                                                           valueTypeDepth);
                }
              }
              while (const auto *arrayRecord =
                         valueType->getAs<clang::RecordType>()) {
                const auto *arraySpecialization =
                    clang::dyn_cast<clang::ClassTemplateSpecializationDecl>(
                        arrayRecord->getDecl());
                if (!arraySpecialization ||
                    arraySpecialization->getQualifiedNameAsString() !=
                        "cpphdl::array") {
                  break;
                }
                const auto &arrayArguments =
                    arraySpecialization->getTemplateArgs();
                if (arrayArguments.size() < 2 ||
                    arrayArguments[0].getKind() !=
                        clang::TemplateArgument::Integral ||
                    arrayArguments[1].getKind() !=
                        clang::TemplateArgument::Type) {
                  item.portModuleDimensions.clear();
                  break;
                }
                item.portModuleDimensions.push_back(
                    arrayArguments[0].getAsIntegral().getLimitedValue());
                valueType = arrayArguments[1].getAsType();
                ++valueTypeDepth;
                // Array-valued ports still expose type template parameters.
                // Record each declared element layer while unwrapping it so
                // optimized expressions need not print the concrete argument.
                for (const auto &[parameter, argumentType] :
                     templateTypeArguments) {
                  if (context.hasSameType(valueType, argumentType)) {
                    info.templateTypeAccess.try_emplace(
                        parameter, "." + item.name + "()");
                    info.templateTypeAccessDepth.try_emplace(parameter,
                                                             valueTypeDepth);
                  }
                }
              }
              const auto *value = valueType->getAsCXXRecordDecl();
              if (value && derivesModule(value)) {
                collectClass(value);
                item.portModuleClass = recordTypeName(value, context);
              }
            }
          }
        }
      } else if (item.type.find("cpphdl::reg<") != std::string::npos) {
        item.kind = FieldKind::Reg;
        if (const auto *registerRecord =
                field->getType()->getAsCXXRecordDecl()) {
          if (const auto *specialization =
                  clang::dyn_cast<clang::ClassTemplateSpecializationDecl>(
                      registerRecord)) {
            const auto &arguments = specialization->getTemplateArgs();
            if (arguments.size() != 0 &&
                arguments[0].getKind() == clang::TemplateArgument::Type) {
              const clang::QualType valueType = arguments[0].getAsType();
              for (const auto &[parameter, argumentType] :
                   templateTypeArguments) {
                if (context.hasSameType(valueType, argumentType)) {
                  info.templateTypeAccess.try_emplace(
                      parameter, "." + item.name + "._next");
                }
              }
            }
          }
        }
      } else {
        for (const auto &[parameter, argumentType] : templateTypeArguments) {
          if (context.hasSameType(field->getType(), argumentType)) {
            info.templateTypeAccess.try_emplace(parameter,
                                                "." + item.name);
          }
        }
        clang::QualType childType = field->getType();
        if (childType->isPointerType()) {
          item.childPointer = true;
          childType = childType->getPointeeType();
        }
        if (const auto *child = childType->getAsCXXRecordDecl();
            child && derivesModule(child)) {
          collectClass(child);
          item.kind = FieldKind::Child;
          item.childClass = recordTypeName(child, context);
        } else {
          std::vector<size_t> dimensions;
          clang::QualType elementType = childType;
          while (const auto *arrayRecord =
                     elementType->getAs<clang::RecordType>()) {
            const auto *specialization =
                clang::dyn_cast<clang::ClassTemplateSpecializationDecl>(
                    arrayRecord->getDecl());
            if (!specialization ||
                specialization->getQualifiedNameAsString() != "cpphdl::array") {
              break;
            }
            const auto &arguments = specialization->getTemplateArgs();
            if (arguments.size() < 2 ||
                arguments[0].getKind() != clang::TemplateArgument::Integral ||
                arguments[1].getKind() != clang::TemplateArgument::Type) {
              dimensions.clear();
              break;
            }
            dimensions.push_back(
                arguments[0].getAsIntegral().getLimitedValue());
            elementType = arguments[1].getAsType();
          }
          const auto *element = elementType->getAsCXXRecordDecl();
          if (!dimensions.empty() && element && derivesModule(element)) {
            collectClass(element);
            item.kind = FieldKind::ChildArray;
            item.childClass = recordTypeName(element, context);
            item.childDimensions = std::move(dimensions);
            item.childCount = 1;
            for (size_t dimension : item.childDimensions) {
              item.childCount *= dimension;
            }
          }
        }
      }
      info.fields[item.name] = std::move(item);
    }
    for (const auto *child : declaration->decls()) {
      if (const auto *type = clang::dyn_cast<clang::TypeDecl>(child);
          type && !type->getNameAsString().empty()) {
        info.nestedTypes.insert(type->getNameAsString());
        continue;
      }
      const auto *variable = clang::dyn_cast<clang::VarDecl>(child);
      if (!variable || !variable->isStaticDataMember()) {
        continue;
      }
      FieldInfo item;
      item.name = variable->getNameAsString();
      item.type = variable->getType().getAsString();
      item.kind = FieldKind::Other;
      info.fields[item.name] = std::move(item);
      clang::Expr::EvalResult result;
      if (variable->getInit() && !variable->getInit()->isValueDependent() &&
          !variable->getInit()->isInstantiationDependent() &&
          variable->getInit()->EvaluateAsInt(result, context)) {
        info.constantValues[variable->getNameAsString()] =
            result.Val.getInt().getLimitedValue();
      }
    }
    collecting.erase(name);
  }

  bool VisitCXXRecordDecl(clang::CXXRecordDecl *declaration) {
    if (declaration->isCompleteDefinition() && derivesModule(declaration) &&
        (!reachableOnly ||
         classes.contains(recordTypeName(declaration, context)))) {
      collectClass(declaration);
    }
    return true;
  }

  bool VisitTypedefNameDecl(clang::TypedefNameDecl *declaration) {
    const auto *record =
        declaration->getUnderlyingType()->getAsCXXRecordDecl();
    const std::string className = recordTypeName(record, context);
    if (record && derivesModule(record) &&
        (!reachableOnly || classes.contains(className))) {
      // A specialization named only through a generated alias is not always
      // visited as a standalone record declaration. Collecting it from the
      // semantic underlying type preserves its concrete fields and arguments.
      collectClass(record);
      ClassInfo &info = classes[className];

      // Clang's semantic printing of structural non-type template arguments
      // is not necessarily valid source. Prefer the spelling from a concrete
      // alias, which also preserves references to previously emitted aliases.
      const auto *specialization =
          clang::dyn_cast<clang::ClassTemplateSpecializationDecl>(record);
      const auto *sourceInfo = declaration->getTypeSourceInfo();
      const auto typeLoc =
          sourceInfo
              ? sourceInfo->getTypeLoc()
                    .getAsAdjusted<clang::TemplateSpecializationTypeLoc>()
              : clang::TemplateSpecializationTypeLoc();
      if (specialization && typeLoc) {
        const auto *parameters =
            specialization->getSpecializedTemplate()->getTemplateParameters();
        const size_t count =
            std::min<size_t>(parameters->size(), typeLoc.getNumArgs());
        for (size_t index = 0; index < count; ++index) {
          const std::string parameter =
              parameters->getParam(index)->getNameAsString();
          const std::string value =
              trim(sourceText(typeLoc.getArgLoc(index).getSourceRange(),
                              context));
          if (!parameter.empty() && !value.empty()) {
            info.templateSubstitutions[parameter] = value;
          }
        }
      }
    }
    return true;
  }

  bool VisitEnumConstantDecl(clang::EnumConstantDecl *declaration) {
    constantValues[declaration->getQualifiedNameAsString()] =
        declaration->getInitVal().getLimitedValue();
    return true;
  }

  bool VisitVarDecl(clang::VarDecl *declaration) {
    if (!declaration->isConstexpr() || !declaration->getInit() ||
        declaration->getInit()->isValueDependent() ||
        declaration->getInit()->isInstantiationDependent()) {
      return true;
    }
    clang::Expr::EvalResult result;
    if (declaration->getInit()->EvaluateAsInt(result, context)) {
      constantValues[declaration->getQualifiedNameAsString()] =
          result.Val.getInt().getLimitedValue();
    }
    return true;
  }

  bool VisitCXXMethodDecl(clang::CXXMethodDecl *declaration) {
    if (!declaration->doesThisDeclarationHaveABody()) {
      return true;
    }
    const auto &sourceManager = context.getSourceManager();
    const auto methodLocation =
        sourceManager.getSpellingLoc(declaration->getLocation());
    // Header-only and template models keep their module methods in included
    // headers. derivesModule() is the ownership boundary; standard-library
    // implementation records cannot enter the model collection.
    if (!methodLocation.isValid()) {
      return true;
    }
    const auto *parent = declaration->getParent();
    if (!derivesModule(parent)) {
      return true;
    }
    const std::string className = recordTypeName(parent, context);
    bool relevant = !reachableOnly || classes.contains(className);
    if (!relevant) {
      const auto *classTemplate = parent->getDescribedClassTemplate();
      const std::string templateBase =
          classTemplate ? classTemplate->getQualifiedNameAsString()
                        : std::string{};
      for (const auto &[unused, info] : classes) {
        if ((!templateBase.empty() && info.templateBase == templateBase) ||
            info.templateBase == className) {
          relevant = true;
          break;
        }
      }
    }
    if (!relevant) {
      return true;
    }
    collectClass(parent);
    const std::string methodName = declaration->getNameAsString();
    const auto bodyRange = declaration->getBody()->getSourceRange();
    const bool crossFileBody =
        bodyRange.isValid() &&
        !sourceManager.isWrittenInSameFile(
            sourceManager.getSpellingLoc(bodyRange.getBegin()),
            sourceManager.getSpellingLoc(bodyRange.getEnd()));
    const bool instantiatedTemplate =
        classes[className].useTemplatePatternMethods;
    // Concrete template ASTs identify discarded constexpr branches. Render
    // those bodies semantically so invalid members in an inactive branch do
    // not become non-dependent errors after flattening into a free function.
    std::string body = instantiatedTemplate
                           ? astCombBody(declaration->getBody(), context)
                           : (crossFileBody
                                  ? std::string{}
                                  : sourceText(declaration->getBody(), context));
    if (body.empty()) {
      body = astCombBody(declaration->getBody(), context);
    }
    if (body.empty()) {
      return true;
    }
    auto &info = classes[className];
    info.name = className;
    const bool inMainFile =
        sourceManager.isWrittenInMainFile(methodLocation);
    if (clang::isa<clang::CXXConstructorDecl>(declaration)) {
      if (!inMainFile) {
        return true;
      }
      info.constructorBody =
          std::make_shared<const std::string>(std::move(body));
      releaseCollectedMethodBuffers();
      return true;
    }
    if (clang::isa<clang::CXXDestructorDecl>(declaration)) {
      if (!inMainFile) {
        return true;
      }
      info.destructorBody =
          std::make_shared<const std::string>(std::move(body));
      releaseCollectedMethodBuffers();
      return true;
    }
    const auto methodBody =
        std::make_shared<const std::string>(std::move(body));
    info.methods[methodName] = methodBody;
    info.concreteMethods.insert(methodName);
    if (methodName == "_assign") {
      info.assignBody = methodBody;
    } else if (methodName == "_work") {
      info.workBody = methodBody;
      // Calls in _work may update ordinary storage through output references.
      // Record those writes from the semantic AST so graph scheduling can
      // retain the first post-call lazy demand instead of moving it earlier.
      WorkMutationCollector mutationCollector(info, info.workMutatedFields);
      mutationCollector.TraverseStmt(declaration->getBody());
    } else if (methodName == "_strobe") {
      info.strobeBody = methodBody;
    }
    releaseCollectedMethodBuffers();
    return true;
  }

  void releaseCollectedMethodBuffers() {
    // Source extraction and AST pretty-printing allocate temporary strings
    // while Clang's full AST is still live. Release completed batches before
    // collection itself becomes the process's peak-memory phase.
    if ((++collectedMethodCount % 32) == 0) {
      releaseTemporaryAllocatorPages();
    }
  }

  void applyTemplateMethods() {
    size_t reconciledClasses = 0;
    for (auto &[name, info] : classes) {
      if (info.templateBase.empty()) {
        continue;
      }
      const ClassInfo *pattern = nullptr;
      const auto exact = classes.find(info.templateBase);
      if (exact != classes.end() && !exact->second.methods.empty()) {
        pattern = &exact->second;
      }
      if (!pattern) {
        // Primary template records retain parameter names in source methods.
        // Prefer them over concrete specializations, whose semantic bodies may
        // already contain non-round-trippable structural argument spellings.
        for (const auto &[candidateName, candidate] : classes) {
          if (candidate.templateBase.empty() &&
              candidateName.starts_with(info.templateBase + "<") &&
              !candidate.methods.empty()) {
            pattern = &candidate;
            break;
          }
        }
      }
      if (!pattern) {
        for (const auto &[candidateName, candidate] : classes) {
          if ((candidate.templateBase == info.templateBase ||
               candidateName.starts_with(info.templateBase + "<")) &&
              !candidate.methods.empty()) {
            pattern = &candidate;
            break;
          }
        }
      }
      if (!pattern) {
        continue;
      }
      if (info.methods.empty()) {
        info.methods = pattern->methods;
      }
      if (info.useTemplatePatternMethods) {
        for (const auto &[methodName, methodBody] : pattern->methods) {
          const bool lifecycleMethod =
              methodName == "_assign" || methodName == "_work" ||
              methodName == "_strobe";
          bool usesTemplateParameter = false;
          for (const auto &[parameter, unused] :
               info.templateSubstitutions) {
            // Lifecycle flattening needs dependency only for projected type
            // members. Keeping unrelated structural values here duplicates
            // large concrete configurations throughout nested work output.
            if (lifecycleMethod &&
                !info.typeTemplateParameters.contains(parameter)) {
              continue;
            }
            if (containsIdentifier(bodyText(methodBody), parameter)) {
              info.methodTemplateParameters[methodName].insert(parameter);
              usesTemplateParameter = true;
            }
          }

          // A concrete AST can substitute only selected occurrences of a type
          // parameter, indirectly serializing structural values in nested
          // types. Source-pattern combs keep every occurrence deducible.
          if (!lifecycleMethod && usesTemplateParameter) {
            info.methods[methodName] = methodBody;
            continue;
          }
          if (lifecycleMethod &&
              bodyText(methodBody).find("if constexpr") != std::string::npos &&
              !info.concreteMethods.contains(methodName)) {
            info.methods[methodName] = methodBody;
          }
        }
      }
      const auto assign = info.methods.find("_assign");
      info.assignBody = assign == info.methods.end() ? nullptr : assign->second;
      const auto work = info.methods.find("_work");
      info.workBody = work == info.methods.end() ? nullptr : work->second;
      // Concrete template classes can inherit lifecycle bodies from their
      // primary pattern. Their mutation metadata belongs to that same body
      // and must follow it into each elaborated specialization.
      if (info.workBody == pattern->workBody) {
        info.workMutatedFields = pattern->workMutatedFields;
      }
      const auto strobe = info.methods.find("_strobe");
      info.strobeBody = strobe == info.methods.end() ? nullptr : strobe->second;
      for (auto &[fieldName, field] : info.fields) {
        if (!field.initializer.empty()) {
          continue;
        }
        const auto patternField = pattern->fields.find(fieldName);
        if (patternField != pattern->fields.end()) {
          field.initializer = patternField->second.initializer;
        }
      }
      info.nestedTypes.insert(pattern->nestedTypes.begin(),
                              pattern->nestedTypes.end());
      if ((++reconciledClasses % 32) == 0) {
        releaseTemporaryAllocatorPages();
      }
    }
  }

  std::map<std::string, ClassInfo> &classes;
  std::set<std::string> &sourceIncludes;
  std::unordered_map<std::string, uint64_t> &constantValues;
  std::string rootName;
  clang::ASTContext &context;
  std::unordered_set<std::string> collecting;
  size_t collectedMethodCount = 0;
  bool reachableOnly = false;
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

std::optional<std::string> combStorage(const std::string &body) {
  static const std::regex returnPattern(
      R"(\breturn\s+(?:this\s*->\s*)?([A-Za-z_][A-Za-z0-9_]*)\s*;)");
  std::string storage;
  for (auto iterator =
           std::sregex_iterator(body.begin(), body.end(), returnPattern);
       iterator != std::sregex_iterator(); ++iterator) {
    storage = (*iterator)[1].str();
  }
  if (storage.empty()) {
    return std::nullopt;
  }
  return storage;
}

struct Assignment {
  std::string targetInstance;
  std::string targetPort;
  std::string expression;
  std::string macro;
  std::vector<std::string> loopVariables;
  std::vector<std::string> constexprGuards;
};

std::string conjunction(const std::vector<std::string> &expressions) {
  std::string result;
  for (const std::string &expression : expressions) {
    if (expression.empty()) {
      continue;
    }
    if (!result.empty()) {
      result += " && ";
    }
    result += "(" + expression + ")";
  }
  return result;
}

std::vector<Assignment> parseAssignments(const std::string &body,
                                         std::string &error) {
  std::vector<Assignment> result;
  std::istringstream input(bodyInterior(body));
  std::string line;
  std::vector<std::string> blockLoopVariables;
  std::vector<std::string> blockConstexprGuards;
  while (std::getline(input, line)) {
    line = trim(line);
    if (line == "}") {
      if (!blockLoopVariables.empty()) {
        blockLoopVariables.pop_back();
      }
      if (!blockConstexprGuards.empty()) {
        blockConstexprGuards.pop_back();
      }
      continue;
    }
    if (line.starts_with("for (") || line.starts_with("for(")) {
      static const std::regex loopVariable(
          R"(for\s*\(\s*(?:unsigned|size_t|std::size_t|u?int(?:8|16|32|64)_t)\s+([A-Za-z_]\w*))");
      std::smatch match;
      blockLoopVariables.push_back(
          std::regex_search(line, match, loopVariable) ? match[1].str() : "");
      blockConstexprGuards.emplace_back();
      continue;
    }
    if (line.starts_with("if constexpr")) {
      if (line.find('{') != std::string::npos) {
        blockLoopVariables.emplace_back();
        const size_t opening = line.find('(');
        const auto closing = matchingDelimiter(line, opening, '(', ')');
        if (!closing) {
          error = "malformed if constexpr in _assign(): " + line;
          return {};
        }
        blockConstexprGuards.push_back(
            trim(line.substr(opening + 1, *closing - opening - 1)));
      }
      continue;
    }
    if (line.starts_with("if (")) {
      if (line.find('{') != std::string::npos) {
        blockLoopVariables.emplace_back();
        blockConstexprGuards.emplace_back();
      }
      continue;
    }
    if (line.empty() || line == "{" ||
        line.starts_with("using ") || line.starts_with("//") ||
        line.starts_with("(void)") ||
        line.find("->_assign()") != std::string::npos ||
        line.find("._assign()") != std::string::npos) {
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
    const auto arrow = target.rfind("->");
    const auto dot = target.rfind('.');
    if (arrow == std::string::npos && dot == std::string::npos) {
      assignment.targetPort = target;
    } else {
      const bool useArrow =
          arrow != std::string::npos &&
          (dot == std::string::npos || arrow > dot);
      const size_t separator = useArrow ? arrow : dot;
      assignment.targetInstance = trim(target.substr(0, separator));
      assignment.targetPort =
          trim(target.substr(separator + (useArrow ? 2 : 1)));
    }
    assignment.expression =
        trim(line.substr(opening + 1, *closing - opening - 1));
    assignment.macro = trim(line.substr(macro, opening - macro));
    for (const std::string &variable : blockLoopVariables) {
      if (!variable.empty()) {
        assignment.loopVariables.push_back(variable);
      }
    }
    for (const std::string &guard : blockConstexprGuards) {
      if (!guard.empty()) {
        assignment.constexprGuards.push_back(guard);
      }
    }
    result.push_back(std::move(assignment));
  }
  return result;
}

bool containsIdentifier(const std::string &text, const std::string &identifier) {
  return std::regex_search(
      text, std::regex("\\b" + identifier + "\\b"));
}

std::string resolvedTemplateSubstitutionImpl(
    const ClassInfo &info,
    const std::string &parameter, std::set<std::string> &active) {
  const auto found = info.templateSubstitutions.find(parameter);
  if (found == info.templateSubstitutions.end() ||
      active.contains(parameter)) {
    return parameter;
  }

  active.insert(parameter);
  const std::string &value = found->second;
  std::string output;
  for (size_t index = 0; index < value.size();) {
    if (value[index] == '"' || value[index] == '\'') {
      const char quote = value[index];
      const size_t begin = index++;
      bool escaped = false;
      while (index < value.size()) {
        const char character = value[index++];
        if (escaped) {
          escaped = false;
        } else if (character == '\\') {
          escaped = true;
        } else if (character == quote) {
          break;
        }
      }
      output.append(value, begin, index - begin);
      continue;
    }
    if (!identifierStart(value[index])) {
      output.push_back(value[index++]);
      continue;
    }
    const size_t begin = index++;
    while (index < value.size() && identifierPart(value[index])) {
      ++index;
    }
    const std::string identifier = value.substr(begin, index - begin);
    output += resolvedTemplateSubstitutionImpl(info, identifier, active);
  }
  active.erase(parameter);
  if (info.integralTemplateParameters.contains(parameter)) {
    output = "static_cast<" + info.templateParameterTypes.at(parameter) +
             ">(" + output + ")";
  }
  return output;
}

std::string resolvedTemplateSubstitution(
    const ClassInfo &info,
    const std::string &parameter) {
  // Resolve dependent defaults only when emitted while tracking recursion.
  // A replacement may legitimately end in a static member with the same name;
  // cycle detection preserves that member instead of qualifying it forever.
  std::set<std::string> active;
  return resolvedTemplateSubstitutionImpl(info, parameter, active);
}

std::string templateTypeFromAccess(const ClassInfo &info,
                                   const std::string &parameter,
                                   const std::string &alias) {
  const auto access = info.templateTypeAccess.find(parameter);
  if (access == info.templateTypeAccess.end()) {
    return {};
  }
  std::string type =
      "std::remove_cvref_t<decltype(" + alias + access->second + ")>";
  const auto depth = info.templateTypeAccessDepth.find(parameter);
  const size_t elementDepth =
      depth == info.templateTypeAccessDepth.end() ? 0 : depth->second;
  // Recover nested array parameters from the container's declared value_type.
  // Packed array indexing yields a proxy, whereas value_type remains exactly
  // the source template argument at every array nesting level.
  for (size_t index = 0; index < elementDepth; ++index) {
    type = "typename std::remove_cvref_t<" + type + ">::value_type";
  }
  return type;
}

std::string replaceIdentifier(std::string text, const std::string &identifier,
                              size_t value) {
  return std::regex_replace(text, std::regex("\\b" + identifier + "\\b"),
                            std::to_string(value));
}

void renameIdentifier(std::string &text, const std::string &identifier,
                      const std::string &replacement) {
  std::string output;
  output.reserve(text.size());
  for (size_t index = 0; index < text.size();) {
    if (text[index] == '"' || text[index] == '\'') {
      const char quote = text[index];
      const size_t begin = index++;
      bool escaped = false;
      while (index < text.size()) {
        const char value = text[index++];
        if (escaped) {
          escaped = false;
        } else if (value == '\\') {
          escaped = true;
        } else if (value == quote) {
          break;
        }
      }
      output.append(text, begin, index - begin);
      continue;
    }
    if (!identifierStart(text[index])) {
      output.push_back(text[index++]);
      continue;
    }
    const size_t begin = index++;
    while (index < text.size() && identifierPart(text[index])) {
      ++index;
    }
    const std::string_view token(text.data() + begin, index - begin);
    size_t before = begin;
    while (before != 0 &&
           std::isspace(static_cast<unsigned char>(text[before - 1]))) {
      --before;
    }
    const bool qualified =
        (before >= 2 &&
         (text.substr(before - 2, 2) == "::" ||
          text.substr(before - 2, 2) == "->")) ||
        (before != 0 && text[before - 1] == '.');
    output += token == identifier && !qualified ? replacement
                                                : std::string(token);
  }
  text = std::move(output);
}

std::vector<size_t> childElementIndices(const std::string &name) {
  std::vector<size_t> result;
  static const std::regex indexPattern(R"(\[([0-9]+)\])");
  for (auto iterator =
           std::sregex_iterator(name.begin(), name.end(), indexPattern);
       iterator != std::sregex_iterator(); ++iterator) {
    result.push_back(std::stoull((*iterator)[1].str()));
  }
  return result;
}

std::vector<std::string> bracketExpressions(const std::string &text) {
  std::vector<std::string> result;
  size_t position = text.find('[');
  while (position != std::string::npos) {
    const auto closing = matchingDelimiter(text, position, '[', ']');
    if (!closing) {
      return {};
    }
    result.push_back(trim(text.substr(position + 1,
                                      *closing - position - 1)));
    position = text.find('[', *closing + 1);
  }
  return result;
}

std::string maskNonCode(const std::string &text) {
  std::string result = text;
  enum class State { Code, String, Character, LineComment, BlockComment };
  State state = State::Code;
  bool escaped = false;
  for (size_t index = 0; index < result.size(); ++index) {
    const char value = result[index];
    const char next = index + 1 < result.size() ? result[index + 1] : '\0';
    if (state == State::Code) {
      if (value == '"') {
        state = State::String;
      } else if (value == '\'') {
        state = State::Character;
      } else if (value == '/' && next == '/') {
        result[index++] = ' ';
        result[index] = ' ';
        state = State::LineComment;
      } else if (value == '/' && next == '*') {
        result[index++] = ' ';
        result[index] = ' ';
        state = State::BlockComment;
      }
      continue;
    }
    if (state == State::LineComment) {
      if (value == '\n') {
        state = State::Code;
      } else {
        result[index] = ' ';
      }
      continue;
    }
    if (state == State::BlockComment) {
      if (value == '*' && next == '/') {
        result[index++] = ' ';
        result[index] = ' ';
        state = State::Code;
      } else if (value != '\n') {
        result[index] = ' ';
      }
      continue;
    }
    result[index] = value == '\n' ? '\n' : ' ';
    if (escaped) {
      escaped = false;
    } else if (value == '\\') {
      escaped = true;
    } else if ((state == State::String && value == '"') ||
               (state == State::Character && value == '\'')) {
      state = State::Code;
    }
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
  std::vector<size_t> allDependencies;
  std::optional<size_t> aliasOf;
  std::optional<std::string> inlineExpression;
  bool expressionReady = false;
  bool referenced = false;
  bool conditionallyEvaluated = false;
  int visitState = 0;
};

std::string nodeKey(size_t instance, NodeKind kind, const std::string &name) {
  return std::to_string(instance) + (kind == NodeKind::Port ? ":p:" : ":c:") +
         name;
}

} // namespace

struct CombsOptimizer::Impl {
  explicit Impl(std::string rootName) : rootName(std::move(rootName)) {}

  std::string rootName;
  std::map<std::string, ClassInfo> classes;
  std::set<std::string> sourceIncludes;
  std::unordered_map<std::string, uint64_t> constantValues;
  std::vector<std::unique_ptr<Instance>> instanceStorage;
  std::vector<Instance *> instances;
  std::vector<Node> nodes;
  std::unordered_map<std::string, size_t> nodeIds;
  std::unordered_map<std::string, std::pair<Instance *, std::string>> bindings;
  std::unordered_map<std::string, std::string> bindingGuards;
  std::unordered_map<std::string, Instance *> modulePortBindings;
  std::vector<size_t> schedule;
  mutable std::vector<CombDeps> trees;
  mutable bool treesBuilt = false;
  size_t lazyCycleBackEdges = 0;
  size_t preparedNodeCount = 0;
  std::vector<size_t> *activeDemandOrder = nullptr;
  std::unordered_set<size_t> dynamicNodes;
  std::string error;

  void referenceNode(size_t id, std::vector<size_t> *dependencies) {
    nodes[id].referenced = true;
    if (dependencies) {
      dependencies->push_back(id);
    } else if (activeDemandOrder) {
      // Flattened _work is the original lazy-evaluation entry sequence.
      // Preserve that sequence so cycle backedges retain the same value as
      // the unoptimized function_ref call graph for each system clock.
      activeDemandOrder->push_back(id);
    }
  }

  void tracePhase(const char *phase) const {
    if (!std::getenv("CPPHDL_TRACE_PHASES")) {
      return;
    }
    std::ifstream status("/proc/self/status");
    std::string line;
    std::string resident = "unknown";
    while (std::getline(status, line)) {
      if (line.starts_with("VmRSS:")) {
        resident = trim(line.substr(6));
        break;
      }
    }
    llvm::errs() << "cpphdl comb phase: " << phase << ", RSS " << resident
                 << "\n";
  }

  const std::vector<CombDeps> &dependencyTrees() const {
    if (treesBuilt) {
      return trees;
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
    treesBuilt = true;
    return trees;
  }

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
      if (field.kind != FieldKind::Child &&
          field.kind != FieldKind::ChildArray) {
        continue;
      }
      const auto found = classes.find(field.childClass);
      if (found == classes.end()) {
        error = "missing class definition for child " + instance->path + "." +
                fieldName + " (" + field.childClass + ")";
        const std::string base =
            field.childClass.substr(0, field.childClass.find('<'));
        for (const auto &[candidate, unused] : classes) {
          if (candidate.starts_with(base + "<")) {
            error += "; available specialization: " + candidate;
            break;
          }
        }
        return nullptr;
      }
      for (const std::string &concreteField :
           childElementNames(fieldName, field)) {
        Instance *child = addInstance(found->second, instance, concreteField,
                                      activeClasses);
        if (!child) {
          return nullptr;
        }
        instance->children[concreteField] = child;
      }
    }
    activeClasses.erase(type.name);
    return instance;
  }

  void discardUnusedClasses() {
    std::unordered_set<const ClassInfo *> used;
    for (const Instance *instance : instances) {
      used.insert(instance->type);
    }

    // Concrete elaboration has resolved every child type needed by the root.
    // Drop declarations outside that hierarchy before graph construction so
    // large input translation units do not retain irrelevant method bodies.
    for (auto iterator = classes.begin(); iterator != classes.end();) {
      if (!used.contains(&iterator->second)) {
        iterator = classes.erase(iterator);
      } else {
        ++iterator;
      }
    }
    releaseTemporaryAllocatorPages();
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
    const auto storage = node.instance->type->combStorage.find(node.name);
    return node.instance->alias + "." +
           (storage == node.instance->type->combStorage.end()
                ? node.name + "_cache"
                : storage->second);
  }

  bool cachesForSystemClock(const Node &node) const {
    // Ports cache their returned address, and _LAZY_COMB adds an equivalent
    // macro-owned clock field beside its storage. Preserve both boundaries
    // while leaving ordinary reference-returning comb methods repeatable.
    if (node.kind == NodeKind::Port) {
      return true;
    }
    const auto storage = node.instance->type->combStorage.find(node.name);
    return storage != node.instance->type->combStorage.end() &&
           node.instance->type->fields.contains("__prev__system_clock_" +
                                                storage->second);
  }

  // Graph expressions use concrete n<ID>.field aliases after elaboration.
  // Detect reads of semantically mutable work storage without product names
  // or rescanning every class field for every node in the large graph.
  bool readsWorkMutatedField(const std::string &expression) const {
    for (size_t start = 0; start < expression.size(); ++start) {
      if (expression[start] != 'n' ||
          (start != 0 && identifierPart(expression[start - 1]))) {
        continue;
      }
      size_t cursor = start + 1;
      const size_t digits = cursor;
      while (cursor < expression.size() &&
             std::isdigit(static_cast<unsigned char>(expression[cursor]))) {
        ++cursor;
      }
      if (cursor == digits || cursor >= expression.size() ||
          expression[cursor] != '.') {
        continue;
      }
      const size_t instance =
          std::stoull(expression.substr(digits, cursor - digits));
      ++cursor;
      const size_t fieldStart = cursor;
      while (cursor < expression.size() && identifierPart(expression[cursor])) {
        ++cursor;
      }
      if (instance < instances.size() && cursor != fieldStart &&
          instances[instance]->type->workMutatedFields.contains(
              expression.substr(fieldStart, cursor - fieldStart))) {
        return true;
      }
    }
    return false;
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
      const auto fieldName =
          instance.parentField.substr(0, instance.parentField.find('['));
      const auto field = instance.parent->type->fields.find(fieldName);
      const bool pointer =
          field != instance.parent->type->fields.end() &&
          field->second.childPointer;
      output << "  auto& " << instance.alias << " = "
             << (pointer ? "*" : "") << instance.parent->alias << "."
             << instance.parentField << ";\n";
    }
  }

  std::string instanceExpression(const Instance &instance,
                                 const std::string &root) const {
    if (!instance.parent) {
      return root;
    }
    const std::string parent =
        instanceExpression(*instance.parent, root);
    const auto fieldName =
        instance.parentField.substr(0, instance.parentField.find('['));
    const auto field = instance.parent->type->fields.find(fieldName);
    const bool pointer =
        field != instance.parent->type->fields.end() &&
        field->second.childPointer;
    const std::string member =
        "(" + parent + ")." + instance.parentField;
    return pointer ? "(*(" + member + "))" : member;
  }

  std::string rewrite(const std::string &input, Instance &context,
                      std::vector<size_t> *dependencies = nullptr,
                      const std::unordered_set<std::string> &locals = {},
                      const std::string &unavailableValue = {},
                      bool preserveDependentPorts = false) {
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
      const bool forcedField =
          locals.contains(identifier) &&
          (input.find(identifier + "._next") != std::string::npos ||
           input.find(identifier + ".strobe(") != std::string::npos ||
           input.find(identifier + ".apply(") != std::string::npos);

      if (!qualifiedBefore && !memberBefore && identifier == "decltype" &&
          afterIdentifier < input.size() &&
          input[afterIdentifier] == '(') {
        const auto closing =
            matchingDelimiter(input, afterIdentifier, '(', ')');
        if (closing) {
          const std::string operand =
              input.substr(afterIdentifier + 1,
                           *closing - afterIdentifier - 1);
          output +=
              "decltype(" + rewriteBinding(operand, context, locals) + ")";
          index = *closing + 1;
          continue;
        }
      }

      // Resolve a direct child port/comb call as one concrete graph node.
      const auto child = context.children.find(identifier);
      const bool childArrow =
          afterIdentifier + 2 <= input.size() &&
          input.substr(afterIdentifier, 2) == "->";
      const bool childDot =
          afterIdentifier < input.size() && input[afterIdentifier] == '.';
      if (!qualifiedBefore && !memberBefore &&
          child != context.children.end() && (childArrow || childDot)) {
        size_t methodStart =
            skipSpace(input, afterIdentifier + (childArrow ? 2 : 1));
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
                referenceNode(*node, dependencies);
                output += nodeValue(*node);
                index = *closing + 1;
                continue;
              }
              if (!unavailableValue.empty()) {
                output += unavailableValue;
                index = *closing + 1;
                continue;
              }
            }
          }
        }
        output += child->second->alias + ".";
        index = afterIdentifier + (childArrow ? 2 : 1);
        continue;
      }

      if (!qualifiedBefore && !memberBefore && identifier == "this") {
        output += "std::addressof(" + context.alias + ")";
        continue;
      }

      const auto typeAccess =
          context.type->templateTypeAccess.find(identifier);
      if (!qualifiedBefore && !memberBefore &&
          (!locals.contains(identifier) || forcedField) &&
          typeAccess != context.type->templateTypeAccess.end()) {
        output += templateTypeFromAccess(*context.type, identifier,
                                         context.alias);
        continue;
      }

      const auto substitution =
          context.type->templateSubstitutions.find(identifier);
      if (!qualifiedBefore && !memberBefore &&
          (!locals.contains(identifier) || forcedField) &&
          substitution != context.type->templateSubstitutions.end()) {
        output += resolvedTemplateSubstitution(*context.type, identifier);
        continue;
      }

      if (!qualifiedBefore && !memberBefore &&
          (!locals.contains(identifier) || forcedField) &&
          context.type->nestedTypes.contains(identifier)) {
        output += "typename std::remove_reference_t<decltype(" +
                  context.alias + ")>::" + identifier;
        continue;
      }

      if (!qualifiedBefore && !memberBefore &&
          (!locals.contains(identifier) || forcedField) &&
          afterIdentifier < input.size() && input[afterIdentifier] == '(') {
        const auto closing =
            matchingDelimiter(input, afterIdentifier, '(', ')');
        if (closing && trim(input.substr(afterIdentifier + 1,
                                         *closing - afterIdentifier - 1))
                           .empty()) {
          const auto field = context.type->fields.find(identifier);
          // A requires-dependent lifecycle body must keep its own port access
          // dependent on the concrete module type. Replacing it with a parent
          // cache would make projected field accesses fail before discard.
          if (preserveDependentPorts && field != context.type->fields.end() &&
              field->second.kind == FieldKind::Port) {
            output += context.alias + "." + identifier + "()";
            index = *closing + 1;
            continue;
          }
          if (field != context.type->fields.end() &&
              !field->second.portModuleClass.empty()) {
            if (!field->second.portModuleDimensions.empty()) {
              size_t cursor = *closing + 1;
              std::vector<std::string> indices;
              for (size_t dimension = 0;
                   dimension < field->second.portModuleDimensions.size();
                   ++dimension) {
                cursor = skipSpace(input, cursor);
                const auto indexClosing =
                    matchingDelimiter(input, cursor, '[', ']');
                if (!indexClosing) {
                  error = "missing index for module-array port " +
                          context.path + "." + identifier;
                  return {};
                }
                indices.push_back(
                    input.substr(cursor + 1, *indexClosing - cursor - 1));
                cursor = *indexClosing + 1;
              }
              cursor = skipSpace(input, cursor);
              if (cursor >= input.size() || input[cursor] != '.') {
                error = "module-array port is not followed by a method call: " +
                        context.path + "." + identifier;
                return {};
              }
              const size_t methodStart = skipSpace(input, cursor + 1);
              size_t methodEnd = methodStart;
              while (methodEnd < input.size() &&
                     identifierPart(input[methodEnd])) {
                ++methodEnd;
              }
              const std::string method =
                  input.substr(methodStart, methodEnd - methodStart);
              const size_t methodOpening = skipSpace(input, methodEnd);
              const auto methodClosing =
                  matchingDelimiter(input, methodOpening, '(', ')');
              if (!methodClosing ||
                  !trim(input.substr(methodOpening + 1,
                                     *methodClosing - methodOpening - 1))
                       .empty()) {
                error = "unsupported module-array port method call " +
                        context.path + "." + identifier;
                return {};
              }

              std::vector<std::string> rewrittenIndices;
              rewrittenIndices.reserve(indices.size());
              for (const std::string &arrayIndex : indices) {
                rewrittenIndices.push_back(
                    rewrite(arrayIndex, context, dependencies, locals,
                            unavailableValue));
                if (!error.empty()) {
                  return {};
                }
              }

              std::vector<std::string> elements;
              appendChildElementNames(
                  identifier, field->second.portModuleDimensions, 0, elements);
              std::vector<size_t> selectedNodes;
              selectedNodes.reserve(elements.size());
              for (const std::string &element : elements) {
                const auto binding = modulePortBindings.find(
                    nodeKey(context.id, NodeKind::Port, element));
                if (binding == modulePortBindings.end()) {
                  error = "missing module-array port binding " + context.path +
                          "." + element;
                  return {};
                }
                const auto node = callableNode(*binding->second, method);
                if (!node) {
                  error = "missing method " + method +
                          " on module-array port element " + context.path +
                          "." + element;
                  return {};
                }
                referenceNode(*node, dependencies);
                selectedNodes.push_back(*node);
              }
              if (selectedNodes.empty()) {
                error = "empty module-array port " + context.path + "." +
                        identifier;
                return {};
              }

              output += "([&]() -> std::remove_cvref_t<decltype(" +
                        nodeValue(selectedNodes.front()) + ")> { ";
              for (size_t elementIndex = 0;
                   elementIndex < elements.size(); ++elementIndex) {
                const auto coordinates = bracketExpressions(
                    elements[elementIndex].substr(identifier.size()));
                output += "if (";
                for (size_t dimension = 0;
                     dimension < rewrittenIndices.size(); ++dimension) {
                  if (dimension != 0) {
                    output += " && ";
                  }
                  output += "(uint64_t)(" + rewrittenIndices[dimension] +
                            ") == " + coordinates[dimension];
                }
                output += ") return " + nodeValue(selectedNodes[elementIndex]) +
                          "; ";
              }
              output += "return {}; }())";
              index = *methodClosing + 1;
              continue;
            }
            const auto binding = modulePortBindings.find(
                nodeKey(context.id, NodeKind::Port, identifier));
            if (binding == modulePortBindings.end()) {
              error = "missing module-valued port binding " + context.path +
                      "." + identifier;
              return {};
            }
            const size_t dot = skipSpace(input, *closing + 1);
            if (dot < input.size() && input[dot] == '.') {
              const size_t methodStart = skipSpace(input, dot + 1);
              size_t methodEnd = methodStart;
              while (methodEnd < input.size() &&
                     identifierPart(input[methodEnd])) {
                ++methodEnd;
              }
              const std::string method =
                  input.substr(methodStart, methodEnd - methodStart);
              const size_t methodOpening = skipSpace(input, methodEnd);
              const auto methodClosing =
                  matchingDelimiter(input, methodOpening, '(', ')');
              if (methodClosing &&
                  trim(input.substr(methodOpening + 1,
                                    *methodClosing - methodOpening - 1))
                      .empty()) {
                if (auto node = callableNode(*binding->second, method)) {
                  referenceNode(*node, dependencies);
                  output += nodeValue(*node);
                  index = *methodClosing + 1;
                  continue;
                }
              }
            }
            output += binding->second->alias;
            index = *closing + 1;
            continue;
          }
          if (auto node = callableNode(context, identifier)) {
            referenceNode(*node, dependencies);
            output += nodeValue(*node);
            index = *closing + 1;
            continue;
          }
        }
        if (context.type->methods.contains(identifier)) {
          output += context.alias + "." + identifier;
          continue;
        }
      }

      const auto field = context.type->fields.find(identifier);
      if (!qualifiedBefore && !memberBefore &&
          (!locals.contains(identifier) || forcedField) &&
          field != context.type->fields.end()) {
        if (field->second.kind == FieldKind::Port) {
          const size_t node = getNode(context, NodeKind::Port, identifier);
          referenceNode(node, dependencies);
          output += nodeValue(node);
        } else if (field->second.kind == FieldKind::Child) {
          const auto instance = context.children.find(identifier);
          if (instance == context.children.end()) {
            error = "missing concrete child " + context.path + "." + identifier;
            return {};
          }
          output += instance->second->alias;
        } else if (field->second.kind == FieldKind::ChildArray) {
          // A runtime child-array index still selects from a fully elaborated
          // set of instances. Emit a typed dispatcher whose branches reference
          // graph nodes, preserving lazy evaluation without legacy port calls.
          size_t cursor = afterIdentifier;
          std::vector<std::string> indices;
          bool indexedCall = true;
          for (size_t dimension = 0;
               dimension < field->second.childDimensions.size();
               ++dimension) {
            cursor = skipSpace(input, cursor);
            const auto indexClosing =
                matchingDelimiter(input, cursor, '[', ']');
            if (!indexClosing) {
              indexedCall = false;
              break;
            }
            indices.push_back(
                input.substr(cursor + 1, *indexClosing - cursor - 1));
            cursor = *indexClosing + 1;
          }
          cursor = skipSpace(input, cursor);
          const bool arrow = cursor + 1 < input.size() &&
                             input[cursor] == '-' && input[cursor + 1] == '>';
          if (indexedCall &&
              (arrow || (cursor < input.size() && input[cursor] == '.'))) {
            const size_t methodStart =
                skipSpace(input, cursor + (arrow ? 2 : 1));
            size_t methodEnd = methodStart;
            while (methodEnd < input.size() &&
                   identifierPart(input[methodEnd])) {
              ++methodEnd;
            }
            const std::string method =
                input.substr(methodStart, methodEnd - methodStart);
            const size_t methodOpening = skipSpace(input, methodEnd);
            const auto methodClosing =
                matchingDelimiter(input, methodOpening, '(', ')');
            if (methodClosing &&
                trim(input.substr(methodOpening + 1,
                                  *methodClosing - methodOpening - 1))
                    .empty()) {
              std::vector<std::string> elements =
                  childElementNames(identifier, field->second);
              std::vector<size_t> selectedNodes;
              selectedNodes.reserve(elements.size());
              for (const std::string &element : elements) {
                const auto childElement = context.children.find(element);
                if (childElement == context.children.end()) {
                  error = "missing child array element " + context.path +
                          "." + element;
                  return {};
                }
                const auto node = callableNode(*childElement->second, method);
                if (!node) {
                  selectedNodes.clear();
                  break;
                }
                referenceNode(*node, dependencies);
                selectedNodes.push_back(*node);
              }
              if (!selectedNodes.empty()) {
                std::vector<std::string> rewrittenIndices;
                rewrittenIndices.reserve(indices.size());
                for (const std::string &arrayIndex : indices) {
                  rewrittenIndices.push_back(rewrite(
                      arrayIndex, context, dependencies, locals,
                      unavailableValue, preserveDependentPorts));
                  if (!error.empty()) {
                    return {};
                  }
                }

                output += "([&]() -> std::remove_cvref_t<decltype(" +
                          nodeValue(selectedNodes.front()) + ")> { ";
                for (size_t elementIndex = 0;
                     elementIndex < elements.size(); ++elementIndex) {
                  const auto coordinates = bracketExpressions(
                      elements[elementIndex].substr(identifier.size()));
                  output += "if (";
                  for (size_t dimension = 0;
                       dimension < rewrittenIndices.size(); ++dimension) {
                    if (dimension != 0) {
                      output += " && ";
                    }
                    output += "(uint64_t)(" + rewrittenIndices[dimension] +
                              ") == " + coordinates[dimension];
                  }
                  output += ") return " + nodeValue(selectedNodes[elementIndex]) +
                            "; ";
                }
                output += "return {}; }())";
                index = *methodClosing + 1;
                continue;
              }
            }
          }
          output += context.alias + "." + identifier;
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

  std::string rewriteBinding(
      const std::string &input, Instance &context,
      const std::unordered_set<std::string> &preservedIdentifiers = {}) {
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
      const bool childArrow =
          afterIdentifier + 2 <= input.size() &&
          input.substr(afterIdentifier, 2) == "->";
      const bool childDot =
          afterIdentifier < input.size() && input[afterIdentifier] == '.';
      if (!qualifiedBefore && !memberBefore && child != context.children.end() &&
          (childArrow || childDot)) {
        output += child->second->alias + ".";
        index = afterIdentifier + (childArrow ? 2 : 1);
        continue;
      }

      if (!qualifiedBefore && !memberBefore && identifier == "this") {
        output += "std::addressof(" + context.alias + ")";
        continue;
      }

      const auto typeAccess =
          context.type->templateTypeAccess.find(identifier);
      if (!qualifiedBefore && !memberBefore &&
          !preservedIdentifiers.contains(identifier) &&
          typeAccess != context.type->templateTypeAccess.end()) {
        output += templateTypeFromAccess(*context.type, identifier,
                                         context.alias);
        continue;
      }

      const auto substitution =
          context.type->templateSubstitutions.find(identifier);
      if (!qualifiedBefore && !memberBefore &&
          !preservedIdentifiers.contains(identifier) &&
          substitution != context.type->templateSubstitutions.end()) {
        output += resolvedTemplateSubstitution(*context.type, identifier);
        continue;
      }

      if (!qualifiedBefore && !memberBefore &&
          !preservedIdentifiers.contains(identifier) &&
          context.type->nestedTypes.contains(identifier)) {
        output += "typename std::remove_reference_t<decltype(" +
                  context.alias + ")>::" + identifier;
        continue;
      }

      const auto field = context.type->fields.find(identifier);
      if (!qualifiedBefore && !memberBefore &&
          !preservedIdentifiers.contains(identifier) &&
          field != context.type->fields.end()) {
        if (field->second.kind == FieldKind::Child) {
          if (child == context.children.end()) {
            error = "missing concrete child " + context.path + "." + identifier;
            return {};
          }
          output += child->second->alias;
        } else if (field->second.kind == FieldKind::ChildArray) {
          output += context.alias + "." + identifier;
        } else {
          output += context.alias + "." + identifier;
        }
        continue;
      }

      if (!qualifiedBefore && !memberBefore &&
          !preservedIdentifiers.contains(identifier) &&
          context.type->methods.contains(identifier) &&
          afterIdentifier < input.size() && input[afterIdentifier] == '(') {
        // Unevaluated operands stay as C++ calls instead of graph nodes, but
        // moving their enclosing expression out of class scope still requires
        // every member method, including helpers with arguments, to be bound.
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
    std::unordered_set<std::string> locals;
    std::set<std::string> dependentTemplateParameters;
    if (kind == NodeKind::Comb) {
      const auto body = instance->type->combExpressions.find(name);
      const auto storage = instance->type->combStorage.find(name);
      if (body == instance->type->combExpressions.end() ||
          storage == instance->type->combStorage.end()) {
        error = "missing comb body for " + instance->path + "." + name;
        return false;
      }
      expressionText = bodyText(body->second);
      locals = localVariables(expressionText);
      // The final `return storage;` resembles a declaration to the
      // lightweight local-variable scanner. The storage is a module field,
      // so it must still be qualified when the body moves out of class scope.
      locals.erase(storage->second);
      const auto parameters =
          instance->type->methodTemplateParameters.find(name);
      if (parameters != instance->type->methodTemplateParameters.end()) {
        dependentTemplateParameters = parameters->second;
        locals.insert(dependentTemplateParameters.begin(),
                      dependentTemplateParameters.end());
      }
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
        // Header-template models retain text from discarded if-constexpr
        // branches. Those branches may mention intentionally unbound ports
        // which the original C++ specialization never evaluates. Materialize
        // a typed zero instead of calling an unassigned function_ref; an
        // active use will still be exposed by behavioral comparison.
        nodes[id].expression =
            "std::remove_reference_t<decltype(" + instance->alias + "." +
            name + "())>{}";
        nodes[id].expressionContext = instance;
        nodes[id].expressionReady = true;
        return true;
      }
    }
    std::vector<size_t> dependencies;
    std::string unavailableValue;
    if (kind == NodeKind::Comb) {
      unavailableValue =
          "std::remove_cvref_t<decltype(" + instance->alias + "." +
          instance->type->combStorage.at(name) + ")>{}";
    }
    const auto bindingGuard =
        kind == NodeKind::Port
            ? bindingGuards.find(nodeKey(instance->id, NodeKind::Port, name))
            : bindingGuards.end();
    if (bindingGuard != bindingGuards.end() && !bindingGuard->second.empty()) {
      // A port in a discarded C++ constexpr branch does not exist in the
      // elaborated RTL. Keep the call behind the original condition so the
      // global schedule cannot eagerly evaluate its intentionally invalid
      // widths or unbound inputs.
      const std::string guard =
          rewriteBinding(bindingGuard->second, *expressionContext);
      // The active branch still needs normal graph rewriting; name-only
      // rewriting leaves legacy function_ref calls that can mutate graph
      // storage after an optimized consumer has cached the earlier value.
      const std::string guardedExpression = rewrite(
          expressionText, *expressionContext, &dependencies, locals,
          unavailableValue);
      if (!error.empty()) {
        return false;
      }
      nodes[id].expression =
          "([&]() -> std::remove_cvref_t<decltype(" + instance->alias + "." +
          name + "())> { if constexpr (" + guard + ") return (" +
          guardedExpression + "); else return {}; }())";
      nodes[id].expressionContext = expressionContext;
      nodes[id].dependencies = dependencies;
      nodes[id].allDependencies = std::move(dependencies);
      nodes[id].conditionallyEvaluated = true;
      nodes[id].expressionReady = true;
      ++preparedNodeCount;
      return true;
    }
    expressionText = rewrite(expressionText, *expressionContext,
                             &dependencies, locals, unavailableValue);
    if (!error.empty()) {
      return false;
    }
    if (kind == NodeKind::Comb) {
      const std::string &storage =
          instance->type->combStorage.at(name);
      std::string lambdaTemplate;
      std::string lambdaModuleType;
      const bool deduceTemplateParameters =
          !dependentTemplateParameters.empty() &&
          instance->type->supportsTemplateArgumentDeduction &&
          !instance->type->templateBase.empty() &&
          !instance->type->templateParameterOrder.empty();
      if (deduceTemplateParameters) {
        std::string templateArguments;
        for (const std::string &parameter :
             instance->type->templateParameterOrder) {
          const auto declaration =
              instance->type->templateParameterDeclarations.find(parameter);
          if (declaration ==
              instance->type->templateParameterDeclarations.end()) {
            error = "cannot deduce template parameter " + parameter +
                    " for optimized comb " + instance->path + "." + name;
            return false;
          }
          if (!lambdaTemplate.empty()) {
            lambdaTemplate += ", ";
            templateArguments += ", ";
          }
          lambdaTemplate += declaration->second;
          templateArguments += parameter;
        }
        // Deduction from the concrete module type keeps structural NTTPs in
        // C++'s type system. Serializing Clang's semantic aggregate spelling
        // can produce source that is descriptive but cannot be recompiled.
        lambdaModuleType = instance->type->templateBase + "<" +
                           templateArguments + ">";
      } else if (!dependentTemplateParameters.empty()) {
        error = "cannot preserve dependent template parameters for optimized "
                "comb " + instance->path + "." + name;
        return false;
      }
      const std::string invocation =
          dependentTemplateParameters.empty()
              ? "()"
              : "(" + instance->alias + ")";
      expressionText = "([&]" +
                       (dependentTemplateParameters.empty()
                            ? std::string{}
                            : "<" + lambdaTemplate + ">") +
                       (dependentTemplateParameters.empty()
                            ? "()"
                            : "(" + lambdaModuleType + "& " +
                                  instance->alias + ")") +
                       " -> std::remove_cvref_t<decltype(" +
                       instance->alias + "." + storage + ")> " +
                       expressionText + ")" + invocation;
    }
    nodes[id].expression = std::move(expressionText);
    nodes[id].expressionContext = expressionContext;
    nodes[id].dependencies = dependencies;
    nodes[id].allDependencies = std::move(dependencies);
    nodes[id].expressionReady = true;
    ++preparedNodeCount;
#if defined(__GLIBC__)
    // Rewriting large generated methods creates short-lived regex and string
    // buffers. Release completed batches so graph construction does not retain
    // several gigabytes of allocator arenas before code emission starts.
    if ((preparedNodeCount % 4) == 0) {
      releaseTemporaryAllocatorPages();
    }
#endif
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
    std::vector<size_t> scheduledDependencies;
    for (const size_t dependency : dependencies) {
      if (nodes[dependency].visitState == 1) {
        // Keep the source expression intact for SCC analysis after discovery.
        // Generated on-demand evaluators will reproduce retained-storage
        // recursion; choosing a fixed backedge here loses conditional behavior.
        ++lazyCycleBackEdges;
        continue;
      }
      if (!visitNode(dependency, stack)) {
        return false;
      }
      scheduledDependencies.push_back(dependency);
    }
    nodes[id].dependencies = std::move(scheduledDependencies);
    stack.pop_back();
    nodes[id].visitState = 2;
    schedule.push_back(id);
    return true;
  }

  bool hasUnresolvedInstanceCall(const std::string &input) const {
    // Graph values have already replaced understood comb and port calls.
    // Parse any remaining n<ID> member/index chain so module arrays receive
    // the same lazy treatment as direct child calls without product patterns.
    for (size_t start = 0; start < input.size(); ++start) {
      if (input[start] != 'n' ||
          (start != 0 && identifierPart(input[start - 1]))) {
        continue;
      }
      size_t cursor = start + 1;
      const size_t digits = cursor;
      while (cursor < input.size() &&
             std::isdigit(static_cast<unsigned char>(input[cursor]))) {
        ++cursor;
      }
      if (cursor == digits ||
          (cursor < input.size() && identifierPart(input[cursor]))) {
        continue;
      }

      bool sawMember = false;
      while (cursor < input.size()) {
        cursor = skipSpace(input, cursor);
        if (cursor < input.size() && input[cursor] == '[') {
          const auto closing = matchingDelimiter(input, cursor, '[', ']');
          if (!closing) {
            break;
          }
          cursor = *closing + 1;
          continue;
        }

        size_t separatorWidth = 0;
        if (cursor < input.size() && input[cursor] == '.') {
          separatorWidth = 1;
        } else if (cursor + 1 < input.size() && input[cursor] == '-' &&
                   input[cursor + 1] == '>') {
          separatorWidth = 2;
        } else {
          break;
        }
        cursor = skipSpace(input, cursor + separatorWidth);
        if (cursor >= input.size() || !identifierStart(input[cursor])) {
          break;
        }
        sawMember = true;
        ++cursor;
        while (cursor < input.size() && identifierPart(input[cursor])) {
          ++cursor;
        }
        cursor = skipSpace(input, cursor);
        if (cursor < input.size() && input[cursor] == '(') {
          const auto closing = matchingDelimiter(input, cursor, '(', ')');
          if (sawMember && closing &&
              trim(input.substr(cursor + 1, *closing - cursor - 1)).empty()) {
            return true;
          }
          break;
        }
      }
    }
    return false;
  }

  void identifyDynamicNodes() {
    const size_t count = nodes.size();
    const std::unordered_set<size_t> activeNodes(schedule.begin(),
                                                 schedule.end());
    std::vector<int> index(count, -1);
    std::vector<int> low(count, -1);
    std::vector<size_t> stack;
    std::vector<bool> onStack(count, false);
    std::vector<size_t> cyclicSeeds;
    int nextIndex = 0;

    std::function<void(size_t)> strongConnect = [&](size_t id) {
      index[id] = low[id] = nextIndex++;
      stack.push_back(id);
      onStack[id] = true;
      for (const size_t dependency : nodes[id].allDependencies) {
        if (dependency >= count || !activeNodes.contains(dependency)) {
          continue;
        }
        if (index[dependency] == -1) {
          strongConnect(dependency);
          low[id] = std::min(low[id], low[dependency]);
        } else if (onStack[dependency]) {
          low[id] = std::min(low[id], index[dependency]);
        }
      }
      if (low[id] != index[id]) {
        return;
      }

      std::vector<size_t> component;
      while (!stack.empty()) {
        const size_t member = stack.back();
        stack.pop_back();
        onStack[member] = false;
        component.push_back(member);
        if (member == id) {
          break;
        }
      }
      bool cyclic = component.size() > 1;
      if (!cyclic && !component.empty()) {
        const auto &dependencies = nodes[component.front()].allDependencies;
        cyclic = std::find(dependencies.begin(), dependencies.end(),
                           component.front()) != dependencies.end();
      }
      if (cyclic) {
        cyclicSeeds.insert(cyclicSeeds.end(), component.begin(),
                           component.end());
      }
    };

    for (const size_t id : schedule) {
      if (index[id] == -1) {
        strongConnect(id);
      }
    }

    for (const size_t id : schedule) {
      // Conditional nodes and their consumers must stay on demand. Scheduling
      // their dependencies eagerly would execute calls from discarded
      // if-constexpr branches, unlike the original function_ref chain.
      if (nodes[id].conditionallyEvaluated) {
        cyclicSeeds.push_back(id);
      }
      if (hasUnresolvedInstanceCall(nodes[id].expression)) {
        cyclicSeeds.push_back(id);
      }
      // A field written by flattened _work is not a clock-start graph input.
      // Keep readers and their consumers on demand so their first evaluation
      // remains at the source call site after the imperative write occurs.
      if (readsWorkMutatedField(nodes[id].expression)) {
        cyclicSeeds.push_back(id);
      }
    }

    // Dependencies reached only through a conditional expression are lazy as
    // well. Include that downward cone before propagating back to consumers,
    // so no discarded branch is evaluated by the remaining eager schedule.
    std::vector<size_t> conditionalDependencies;
    std::unordered_set<size_t> conditionalDependencySet;
    for (const size_t id : cyclicSeeds) {
      if (nodes[id].conditionallyEvaluated) {
        conditionalDependencies.push_back(id);
        conditionalDependencySet.insert(id);
      }
    }
    for (size_t index = 0; index < conditionalDependencies.size(); ++index) {
      for (const size_t dependency :
           nodes[conditionalDependencies[index]].allDependencies) {
        if (conditionalDependencySet.insert(dependency).second) {
          conditionalDependencies.push_back(dependency);
          cyclicSeeds.push_back(dependency);
        }
      }
    }

    std::vector<std::vector<size_t>> consumers(count);
    for (const size_t id : schedule) {
      for (const size_t dependency : nodes[id].allDependencies) {
        if (dependency < count && activeNodes.contains(dependency)) {
          consumers[dependency].push_back(id);
        }
      }
    }

    // Any eager consumer of an SCC or unresolved module call could request it
    // through a branch inactive in the original lazy call chain. Move the
    // complete reverse dependency cone to generated on-demand evaluation.
    dynamicNodes.clear();
    std::vector<size_t> pending;
    for (const size_t id : cyclicSeeds) {
      if (dynamicNodes.insert(id).second) {
        pending.push_back(id);
      }
    }
    while (!pending.empty()) {
      const size_t dependency = pending.back();
      pending.pop_back();
      for (const size_t consumer : consumers[dependency]) {
        if (dynamicNodes.insert(consumer).second) {
          pending.push_back(consumer);
        }
      }
    }
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
    for (const auto &[method, storage] :
         instances[instance]->type->combStorage) {
      if (storage != match[3].str()) {
        continue;
      }
      const auto found =
          nodeIds.find(nodeKey(instance, NodeKind::Comb, method));
      if (found != nodeIds.end()) {
        return found->second;
      }
    }
    return std::nullopt;
  }

  std::string replaceAliases(const std::string &input) const {
    static const std::regex valuePattern(
        R"(s\.p([0-9]+)|n([0-9]+)\.([A-Za-z_][A-Za-z0-9_]*))");
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

  std::string replaceDynamicCalls(const std::string &input,
                                  const std::string &shortRoot,
                                  std::optional<size_t> self = {}) const {
    static const std::regex valuePattern(
        R"(s\.p([0-9]+)|n([0-9]+)\.([A-Za-z_][A-Za-z0-9_]*))");
    std::string output;
    size_t position = 0;
    for (auto iterator =
             std::sregex_iterator(input.begin(), input.end(), valuePattern);
         iterator != std::sregex_iterator(); ++iterator) {
      const auto &match = *iterator;
      output.append(input, position,
                    static_cast<size_t>(match.position()) - position);
      const auto id = valueNode(match);
      if (id && dynamicNodes.contains(*id) && (!self || *id != *self)) {
        output += "(" + shortRoot + "_optimized_comb_eval_" +
                  std::to_string(*id) + "(obj, s), " + nodeValue(*id) + ")";
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
        R"(s\.p([0-9]+)|n([0-9]+)\.([A-Za-z_][A-Za-z0-9_]*))");
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
      if (dynamicNodes.contains(id) || node.kind != NodeKind::Port ||
          node.dependencies.size() != 1 ||
          dynamicNodes.contains(node.dependencies.front())) {
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
      if (dynamicNodes.contains(id) || node.kind != NodeKind::Port ||
          !node.instance->parent) {
        continue;
      }
      constexpr size_t maxInlineExpressionBytes = 1024;
      if ((uses[id] == 1 &&
           node.expression.size() <= maxInlineExpressionBytes) ||
          std::regex_match(node.expression, directFieldPattern)) {
        // Small single-use values remove harmless temporaries, while a size
        // limit prevents recursively embedding generated type-heavy bodies.
        // Direct field aliases remain compact regardless of their use count.
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
        R"((?:const\s+)?(?:auto|bool|size_t|u?int(?:8|16|32|64)_t|[A-Za-z_]\w*(?:::\w+)*(?:<[^;=]+>)?)\s+([A-Za-z_]\w*)\s*(?:[=;{]))");
    // Format strings and comments often contain declaration-like text such
    // as "state=%u". Exclude non-code spans before the lightweight scanner so
    // a module field cannot be misclassified as a shadowing local variable.
    const std::string code = maskNonCode(body);
    for (auto iterator =
             std::sregex_iterator(code.begin(), code.end(), declaration);
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

    // A continuation clause is grammatically part of the statement before it.
    // Keep that relationship before byte-based packing can place a chunk edge
    // between a closing brace and its `else`, `catch`, or do-while tail.
    std::vector<std::string> joinedUnits;
    joinedUnits.reserve(units.size());
    for (std::string &item : units) {
      const std::string stripped = trim(item);
      const bool continuation =
          stripped.starts_with("else") || stripped.starts_with("catch") ||
          (stripped.starts_with("while") && !joinedUnits.empty() &&
           trim(joinedUnits.back()).starts_with("do"));
      if (continuation) {
        if (joinedUnits.empty()) {
          return {work};
        }
        joinedUnits.back() += item;
      } else {
        joinedUnits.push_back(std::move(item));
      }
    }
    units = std::move(joinedUnits);

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
    const std::string interior =
        bodyInterior(bodyText(instance.type->workBody));
    auto locals = localVariables(interior);
    std::set<std::string> dependentTemplateParameters;
    if (const auto parameters =
            instance.type->methodTemplateParameters.find("_work");
        parameters != instance.type->methodTemplateParameters.end()) {
      dependentTemplateParameters = parameters->second;
      locals.insert(dependentTemplateParameters.begin(),
                    dependentTemplateParameters.end());
    }
    std::ostringstream output;
    std::istringstream input(interior);
    std::string line;
    std::vector<std::string> pendingLoops;
    size_t skipLoopCloses = 0;
    while (std::getline(input, line)) {
      const std::string stripped = trim(line);
      if (stripped.empty()) {
        continue;
      }
      if (skipLoopCloses != 0 && stripped == "}") {
        --skipLoopCloses;
        continue;
      }
      if (stripped.starts_with("for (") || stripped.starts_with("for(")) {
        pendingLoops.push_back(stripped);
        continue;
      }
      const auto arrowWorkCall = stripped.find("->_work(");
      const auto dotWorkCall = stripped.find("._work(");
      const auto workCall =
          arrowWorkCall != std::string::npos ? arrowWorkCall : dotWorkCall;
      if (workCall != std::string::npos) {
        const std::string object = trim(stripped.substr(0, workCall));
        const auto bracket = object.find('[');
        if (bracket != std::string::npos) {
          const std::string fieldName = trim(object.substr(0, bracket));
          const auto field = instance.type->fields.find(fieldName);
          if (field == instance.type->fields.end() ||
              field->second.kind != FieldKind::ChildArray) {
            error = "cannot resolve child array _work call in " +
                    instance.path + ": " + stripped;
            active.erase(instance.id);
            return {};
          }
          for (const std::string &element :
               childElementNames(fieldName, field->second)) {
            const auto child = instance.children.find(element);
            if (child == instance.children.end()) {
              error = "missing child array element " + instance.path + "." +
                      element;
              active.erase(instance.id);
              return {};
            }
            output << flattenWork(*child->second, indent, active);
            if (!error.empty()) {
              active.erase(instance.id);
              return {};
            }
          }
          skipLoopCloses = pendingLoops.size();
          pendingLoops.clear();
          continue;
        }
        if (!pendingLoops.empty()) {
          for (const std::string &loop : pendingLoops) {
            output << indent
                   << rewrite(loop, instance, nullptr, locals, {},
                              !dependentTemplateParameters.empty())
                   << '\n';
          }
          pendingLoops.clear();
        }
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
          condition =
              rewrite(condition, instance, nullptr, locals, {},
                      !dependentTemplateParameters.empty());
          for (const std::string &nestedChunk : splitWork(nested, 400)) {
            output << indent << "if (" << condition << ") {\n"
                   << nestedChunk << indent << "}\n";
          }
        } else {
          output << nested;
        }
        continue;
      }
      if (!pendingLoops.empty()) {
        for (const std::string &loop : pendingLoops) {
          output << indent
                 << rewrite(loop, instance, nullptr, locals, {},
                            !dependentTemplateParameters.empty())
                 << '\n';
        }
        pendingLoops.clear();
      }
      std::string rewritten =
          rewrite(stripped, instance, nullptr, locals, {},
                  !dependentTemplateParameters.empty());
      if (!error.empty()) {
        active.erase(instance.id);
        return {};
      }
      // The bool passed to _work is a live simulation input, including during
      // the initial reset cycles. Preserve it in flattened work instead of
      // specializing reset-sensitive state updates to the normal-run value.
      rewritten =
          std::regex_replace(rewritten, std::regex(R"(\bedge\b)"),
                             "__cpphdl_reset");
      rewritten =
          std::regex_replace(rewritten, std::regex(R"(\breset\b)"),
                             "__cpphdl_reset");
      output << indent << rewritten << '\n';
    }
    for (const std::string &loop : pendingLoops) {
      output << indent
             << rewrite(loop, instance, nullptr, locals, {},
                        !dependentTemplateParameters.empty())
             << '\n';
    }
    active.erase(instance.id);
    if (dependentTemplateParameters.empty()) {
      return output.str();
    }

    std::string lambdaTemplate;
    std::string lambdaArguments;
    std::vector<std::pair<std::string, std::string>> renamedParameters;
    for (const std::string &parameter : dependentTemplateParameters) {
      if (!lambdaTemplate.empty()) {
        lambdaTemplate += ", ";
        lambdaArguments += ", ";
      }
      const std::string emittedParameter =
          parameter + "__cpphdl_" + std::to_string(instance.id);
      renamedParameters.emplace_back(parameter, emittedParameter);
      if (instance.type->typeTemplateParameters.contains(parameter)) {
        lambdaTemplate += "typename " + emittedParameter;
      } else if (const auto declaredType =
                     instance.type->templateParameterTypes.find(parameter);
                 declaredType !=
                 instance.type->templateParameterTypes.end()) {
        lambdaTemplate += declaredType->second + " " + emittedParameter;
      } else {
        lambdaTemplate += "auto " + emittedParameter;
      }
      if (const auto access =
              instance.type->templateTypeAccess.find(parameter);
          instance.type->typeTemplateParameters.contains(parameter) &&
          access != instance.type->templateTypeAccess.end()) {
        lambdaArguments += templateTypeFromAccess(
            *instance.type, parameter, instance.alias);
      } else {
        lambdaArguments +=
            resolvedTemplateSubstitution(*instance.type, parameter);
      }
    }

    // A primary-template _work body may contain a requires-based constexpr
    // branch even when Clang did not instantiate that method. Keep its class
    // parameters and module object dependent while flattening that one body.
    std::ostringstream wrapped;
    std::string flattenedBody = std::move(output).str();
    for (const auto &[source, emitted] : renamedParameters) {
      // Child lifecycle lambdas have already received their own suffixed
      // names, so token replacement here only renames this wrapper's source
      // parameters and cannot capture a nested template declaration.
      renameIdentifier(flattenedBody, source, emitted);
    }
    const std::string moduleType =
        "__cpphdl_module_t_" + std::to_string(instance.id);
    wrapped << indent << "([&]<" << lambdaTemplate
            << ", typename " << moduleType << ">(" << moduleType << "& "
            << instance.alias << ") {\n"
            << flattenedBody << indent << "}).template operator()<"
            << lambdaArguments << ">(" << instance.alias << ");\n";
    return wrapped.str();
  }

  std::string flattenStrobe(Instance &instance, const std::string &indent,
                            std::unordered_set<size_t> &active) {
    if (active.contains(instance.id)) {
      error = "recursive _strobe call at " + instance.path;
      return {};
    }
    active.insert(instance.id);
    const std::string interior =
        bodyInterior(bodyText(instance.type->strobeBody));
    const auto locals = localVariables(interior);
    std::ostringstream output;
    std::istringstream input(interior);
    std::string line;
    std::vector<std::string> pendingLoops;
    size_t skipLoopCloses = 0;
    while (std::getline(input, line)) {
      const std::string stripped = trim(line);
      if (stripped.empty()) {
        continue;
      }
      if (skipLoopCloses != 0 && stripped == "}") {
        --skipLoopCloses;
        continue;
      }
      if (stripped.starts_with("for (") || stripped.starts_with("for(")) {
        pendingLoops.push_back(stripped);
        continue;
      }
      const auto arrowStrobeCall = stripped.find("->_strobe(");
      const auto dotStrobeCall = stripped.find("._strobe(");
      const auto strobeCall = arrowStrobeCall != std::string::npos
                                  ? arrowStrobeCall
                                  : dotStrobeCall;
      if (strobeCall != std::string::npos) {
        const std::string object = trim(stripped.substr(0, strobeCall));
        const auto bracket = object.find('[');
        if (bracket != std::string::npos) {
          const std::string fieldName = trim(object.substr(0, bracket));
          const auto field = instance.type->fields.find(fieldName);
          if (field == instance.type->fields.end() ||
              field->second.kind != FieldKind::ChildArray) {
            error = "cannot resolve child array _strobe call in " +
                    instance.path + ": " + stripped;
            active.erase(instance.id);
            return {};
          }
          for (const std::string &element :
               childElementNames(fieldName, field->second)) {
            const auto child = instance.children.find(element);
            if (child == instance.children.end()) {
              error = "missing child array element " + instance.path + "." +
                      element;
              active.erase(instance.id);
              return {};
            }
            output << flattenStrobe(*child->second, indent, active);
            if (!error.empty()) {
              active.erase(instance.id);
              return {};
            }
          }
          skipLoopCloses = pendingLoops.size();
          pendingLoops.clear();
          continue;
        }
        if (!pendingLoops.empty()) {
          for (const std::string &loop : pendingLoops) {
            output << indent << rewrite(loop, instance, nullptr, locals)
                   << '\n';
          }
          pendingLoops.clear();
        }
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
      if (!pendingLoops.empty()) {
        for (const std::string &loop : pendingLoops) {
          output << indent << rewrite(loop, instance, nullptr, locals) << '\n';
        }
        pendingLoops.clear();
      }
      const std::string rewritten =
          rewrite(stripped, instance, nullptr, locals);
      if (!error.empty()) {
        active.erase(instance.id);
        return {};
      }
      output << indent << rewritten << '\n';
    }
    for (const std::string &loop : pendingLoops) {
      output << indent << rewrite(loop, instance, nullptr, locals) << '\n';
    }
    active.erase(instance.id);
    return output.str();
  }

  std::optional<size_t> constantIndex(Instance &instance,
                                      const std::string &expression) const {
    const std::string stripped = trim(expression);
    if (!stripped.empty() &&
        std::all_of(stripped.begin(), stripped.end(),
                    [](unsigned char value) { return std::isdigit(value); })) {
      return std::stoull(stripped);
    }
    if (const auto value = instance.type->constantValues.find(stripped);
        value != instance.type->constantValues.end()) {
      return value->second;
    }
    if (const auto value = constantValues.find(stripped);
        value != constantValues.end()) {
      return value->second;
    }
    static const std::regex qualified(
        R"(([A-Za-z_]\w*(?:::[A-Za-z_]\w*)+))");
    for (auto iterator =
             std::sregex_iterator(stripped.begin(), stripped.end(), qualified);
         iterator != std::sregex_iterator(); ++iterator) {
      if (const auto value = constantValues.find((*iterator)[1].str());
          value != constantValues.end()) {
        return value->second;
      }
    }
    return std::nullopt;
  }

  std::vector<Assignment> concreteAssignments(
      Instance &instance, const Assignment &assignment) {
    if (assignment.targetInstance.empty() ||
        instance.children.contains(assignment.targetInstance)) {
      return {assignment};
    }
    const auto bracket = assignment.targetInstance.find('[');
    if (bracket == std::string::npos) {
      return {assignment};
    }
    const std::string targetBase =
        trim(assignment.targetInstance.substr(0, bracket));
    const bool portArray = targetBase.ends_with("()");
    const std::string fieldName =
        portArray ? trim(targetBase.substr(0, targetBase.size() - 2))
                  : targetBase;
    const auto field = instance.type->fields.find(fieldName);
    if (field == instance.type->fields.end()) {
      return {assignment};
    }
    std::vector<size_t> dimensions;
    if (field->second.kind == FieldKind::ChildArray) {
      dimensions = field->second.childDimensions;
    } else if (portArray && field->second.kind == FieldKind::Port &&
               !field->second.portModuleDimensions.empty()) {
      dimensions = field->second.portModuleDimensions;
    } else {
      return {assignment};
    }
    const auto expressions = bracketExpressions(assignment.targetInstance);
    if (expressions.size() != dimensions.size()) {
      error = "wrong number of indices for _assign target " + instance.path +
              "." + assignment.targetInstance;
      return {};
    }
    std::vector<Assignment> result;
    std::vector<std::string> elements;
    appendChildElementNames(targetBase, dimensions, 0, elements);
    for (const std::string &element : elements) {
      const auto indices = childElementIndices(element);
      bool selected = true;
      std::map<std::string, size_t> substitutions;
      for (size_t dimension = 0; dimension < expressions.size();
           ++dimension) {
        bool loopIndex = false;
        for (const std::string &variable : assignment.loopVariables) {
          if (containsIdentifier(expressions[dimension], variable)) {
            substitutions[variable] = indices[dimension];
            loopIndex = true;
            break;
          }
        }
        if (!loopIndex) {
          const auto expected =
              constantIndex(instance, expressions[dimension]);
          if (!expected || *expected != indices[dimension]) {
            selected = false;
            break;
          }
        }
      }
      if (!selected) {
        continue;
      }
      Assignment concrete = assignment;
      concrete.targetInstance = element;
      concrete.loopVariables.clear();
      for (const auto &[variable, value] : substitutions) {
        concrete.expression =
            replaceIdentifier(concrete.expression, variable, value);
      }
      result.push_back(std::move(concrete));
    }
    if (result.empty()) {
      error = "cannot resolve indexed _assign target " + instance.path + "." +
              assignment.targetInstance;
    }
    return result;
  }

  Instance *resolveModuleExpression(Instance &context,
                                    const std::string &expression) {
    const std::string stripped = trim(expression);
    if (const auto direct = context.children.find(stripped);
        direct != context.children.end()) {
      return direct->second;
    }
    Assignment source;
    source.targetInstance = stripped;
    const auto concrete = concreteAssignments(context, source);
    if (!error.empty() || concrete.size() != 1) {
      return nullptr;
    }
    const auto child = context.children.find(concrete.front().targetInstance);
    return child == context.children.end() ? nullptr : child->second;
  }

  std::vector<Instance *> resolveModuleArrayExpression(
      Instance &context, const std::string &expression,
      const std::vector<size_t> &dimensions) {
    const std::string stripped = trim(expression);
    std::vector<std::string> names;
    appendChildElementNames(stripped, dimensions, 0, names);
    std::vector<Instance *> result;
    result.reserve(names.size());
    for (const std::string &name : names) {
      if (const auto child = context.children.find(name);
          child != context.children.end()) {
        result.push_back(child->second);
        continue;
      }
      if (stripped.ends_with("()")) {
        const std::string port =
            trim(stripped.substr(0, stripped.size() - 2));
        const std::string suffix = name.substr(stripped.size());
        const auto binding = modulePortBindings.find(
            nodeKey(context.id, NodeKind::Port, port + suffix));
        if (binding != modulePortBindings.end()) {
          result.push_back(binding->second);
          continue;
        }
      }
      return {};
    }
    return result;
  }

  Instance *resolveAssignmentTarget(Instance &context,
                                    const std::string &targetExpression) {
    const auto call = targetExpression.find("()");
    if (call != std::string::npos) {
      const std::string port = trim(targetExpression.substr(0, call));
      std::string suffix = targetExpression.substr(call + 2);
      if (!suffix.empty()) {
        std::string normalized;
        for (const std::string &index : bracketExpressions(suffix)) {
          const auto value = constantIndex(context, index);
          if (!value) {
            return nullptr;
          }
          normalized += "[" + std::to_string(*value) + "]";
        }
        suffix = normalized;
      }
      const auto binding =
          modulePortBindings.find(
              nodeKey(context.id, NodeKind::Port, port + suffix));
      return binding == modulePortBindings.end() ? nullptr : binding->second;
    }
    const auto child = context.children.find(targetExpression);
    return child == context.children.end() ? nullptr : child->second;
  }

  bool buildBindings() {
    for (Instance *instance : instances) {
      std::string parseError;
      std::vector<Assignment> assignments;
      for (const auto &[name, field] : instance->type->fields) {
        if (field.kind != FieldKind::Port || field.initializer.empty()) {
          continue;
        }
        const auto initialized = parseAssignments(
            "{\n" + name + " = " + field.initializer + ";\n}", parseError);
        if (!parseError.empty()) {
          error = instance->path + "." + name + ": " + parseError;
          return false;
        }
        assignments.insert(assignments.end(), initialized.begin(),
                           initialized.end());
      }
      const auto assigned =
          parseAssignments(bodyText(instance->type->assignBody), parseError);
      if (!parseError.empty()) {
        error = instance->path + ": " + parseError;
        return false;
      }
      assignments.insert(assignments.end(), assigned.begin(), assigned.end());
      for (const Assignment &abstractAssignment : assignments) {
        const auto expanded =
            concreteAssignments(*instance, abstractAssignment);
        if (!error.empty()) {
          return false;
        }
        for (const Assignment &assignment : expanded) {
        Instance *target = instance;
        if (!assignment.targetInstance.empty()) {
          target =
              resolveAssignmentTarget(*instance, assignment.targetInstance);
          if (!target) {
            error = "unknown _assign target " + instance->path + "." +
                    assignment.targetInstance;
            return false;
          }
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
        if (!field->second.portModuleClass.empty()) {
          if (field->second.portModuleDimensions.empty()) {
            Instance *source =
                resolveModuleExpression(*instance, assignment.expression);
            if (!source) {
              error = "cannot resolve module-valued port binding " +
                      target->path + "." + assignment.targetPort + " = " +
                      assignment.expression;
              return false;
            }
            modulePortBindings[key] = source;
          } else {
            const auto sources = resolveModuleArrayExpression(
                *instance, assignment.expression,
                field->second.portModuleDimensions);
            std::vector<std::string> targetElements;
            appendChildElementNames(assignment.targetPort,
                                    field->second.portModuleDimensions, 0,
                                    targetElements);
            if (sources.size() != targetElements.size()) {
              error = "cannot resolve module-array port binding " +
                      target->path + "." + assignment.targetPort + " = " +
                      assignment.expression;
              return false;
            }
            for (size_t index = 0; index < sources.size(); ++index) {
              modulePortBindings[nodeKey(target->id, NodeKind::Port,
                                         targetElements[index])] =
                  sources[index];
            }
          }
        }
        // _assign() is imperative binding setup. A later assignment to the
        // same port replaces the earlier function_ref, exactly as in C++.
        bindings[key] = {instance, assignment.expression};
        bindingGuards[key] = conjunction(assignment.constexprGuards);
        }
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
        if (auto storage = combStorage(bodyText(body));
            storage && info.fields.contains(*storage)) {
          info.combExpressions[methodName] = body;
          info.combStorage[methodName] = *storage;
        }
      }
    }
  }

  bool emit(const std::string &rootName, const std::string &directory) {
    lazyCycleBackEdges = 0;
    preparedNodeCount = 0;
    findCombExpressions();
    tracePhase("comb discovery");
    const auto rootType = classes.find(rootName);
    if (rootType == classes.end()) {
      error = "root module class not found: " + rootName;
      return false;
    }
    std::unordered_set<std::string> activeClasses;
    Instance *root = addInstance(rootType->second, nullptr, {}, activeClasses);
    if (!root) {
      return false;
    }
    discardUnusedClasses();
    tracePhase("hierarchy elaboration");
    if (!buildBindings()) {
      return false;
    }
    // Binding expansion creates short-lived copies of generated template
    // expressions across the concrete hierarchy. Return those pages before
    // lifecycle flattening starts its independent string-heavy phase.
    releaseTemporaryAllocatorPages();
    tracePhase("port binding expansion");

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
    std::vector<size_t> workDemandOrder;
    // rewrite() sees calls in the same order as the original flattened work.
    // Record only this phase so declarations and graph preparation cannot
    // perturb the lazy cycle entry points selected by runtime evaluation.
    activeDemandOrder = &workDemandOrder;
    std::string work = flattenWork(*root, "  ", activeWork);
    activeDemandOrder = nullptr;
    if (!error.empty()) {
      return false;
    }
    if (instances.size() > 1 && trim(work).empty() &&
        bodyText(root->type->workBody).find("_work(") != std::string::npos) {
      error = "flattened root work is empty; concrete child method bodies "
              "were not collected; missing:";
      size_t reported = 0;
      for (const Instance *instance : instances) {
        if (!instance->type->workBody && reported < 8) {
          error += " " + instance->type->name;
          ++reported;
        }
      }
      return false;
    }
    releaseTemporaryAllocatorPages();
    tracePhase("work flattening");
    std::unordered_set<size_t> activeStrobe;
    std::string strobe = flattenStrobe(*root, "  ", activeStrobe);
    if (!error.empty()) {
      return false;
    }
    releaseTemporaryAllocatorPages();
    tracePhase("strobe flattening");
    if (instances.size() > 1 && trim(strobe).empty() &&
        bodyText(root->type->strobeBody).find("_strobe(") !=
            std::string::npos) {
      error = "flattened root strobe is empty; concrete child method bodies "
              "were not collected";
      return false;
    }

    // External models still consume their ports through the generated
    // function_ref API.  Make those bindings graph roots so their lambdas can
    // read already-scheduled caches instead of calling legacy comb methods.
    for (Instance *instance : instances) {
      if (bodyText(instance->type->workBody)
              .find("firtool_cpphdl_external::work") ==
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

    // Lazy cycles retain a value at the recursive edge selected by the first
    // runtime demand. Visit flattened work roots in that same order before
    // falling back to graph-discovery order for outputs and external models.
    std::vector<size_t> stack;
    std::unordered_set<size_t> demanded;
    for (const size_t id : workDemandOrder) {
      if (demanded.insert(id).second && !visitNode(id, stack)) {
        return false;
      }
    }
    // getNode may grow nodes while expressions are rewritten. Iterate by
    // index after ordered work roots so every other referenced node is also
    // scheduled once the complete dependency set has stabilized.
    for (size_t id = 0; id < nodes.size(); ++id) {
      if (nodes[id].referenced && !visitNode(id, stack)) {
        return false;
      }
    }
    releaseTemporaryAllocatorPages();
    tracePhase("dependency scheduling");
    // Classify lazy boundaries before alias simplification can erase ports.
    // Dynamic function_ref nodes carry observable per-clock cache semantics.
    // Recompute after simplification below to classify the final graph too.
    identifyDynamicNodes();
    optimizeGraph(work);
    // Alias and inline simplification removes thousands of transparent port
    // nodes. Rebuild the concrete graph from those simplified expressions so
    // SCC fallback contains behavior, not conversion scaffolding.
    for (const size_t id : schedule) {
      std::unordered_map<size_t, size_t> uses;
      collectNodeUses(nodes[id].expression, uses);
      nodes[id].allDependencies.clear();
      for (const auto &[dependency, count] : uses) {
        // A comb body writes and may reread its own persistent result while
        // accumulating a value. That is local method state, not a recursive
        // graph call, so it must not create a singleton SCC.
        if (count != 0 && dependency != id) {
          nodes[id].allDependencies.push_back(dependency);
        }
      }
    }
    identifyDynamicNodes();
    const std::string shortRoot = rootName.substr(
        rootName.rfind("::") == std::string::npos ? 0
                                                  : rootName.rfind("::") + 2);
    // The class name does not require its defining header to have the same name.
    // Including a same-named but unrelated specialization header redefines the root.
    // Prefer Clang's declaration location and retain the old spelling only as fallback.
    const std::string rootHeader = root->type->header.empty()
                                       ? shortRoot + ".h"
                                       : root->type->header;
    const std::string rootHeaderName =
        std::filesystem::path(rootHeader).filename().string();
    // SCC consumers execute through generated per-clock evaluators so source
    // conditionals select the recursive path at runtime. Remove them from the
    // eager schedule and redirect their exact use sites to those evaluators.
    for (const size_t id : dynamicNodes) {
      if (id < nodes.size() && nodes[id].expressionReady) {
        nodes[id].expression =
            replaceDynamicCalls(nodes[id].expression, shortRoot, id);
      }
    }
    work = replaceDynamicCalls(work, shortRoot);
    schedule.erase(
        std::remove_if(schedule.begin(), schedule.end(),
                       [this](size_t id) { return dynamicNodes.contains(id); }),
        schedule.end());
    releaseTemporaryAllocatorPages();
    tracePhase("graph simplification");

    struct BindingStatement {
      std::string text;
      std::set<size_t> usedInstances;
    };
    std::vector<BindingStatement> bindingStatements;
    for (Instance *instance : instances) {
      std::string parseError;
      const auto assignments =
          parseAssignments(bodyText(instance->type->assignBody), parseError);
      if (!parseError.empty()) {
        error = instance->path + ": " + parseError;
        return false;
      }
      for (const Assignment &abstractAssignment : assignments) {
        const auto expanded =
            concreteAssignments(*instance, abstractAssignment);
        if (!error.empty()) {
          return false;
        }
        for (const Assignment &assignment : expanded) {
        Instance *target = instance;
        if (!assignment.targetInstance.empty()) {
          target =
              resolveAssignmentTarget(*instance, assignment.targetInstance);
          if (!target) {
            error = "unknown flattened binding target " + instance->path +
                    "." + assignment.targetInstance;
            return false;
          }
        }
        if (bodyText(target->type->workBody)
                .find("firtool_cpphdl_external::work") ==
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
            (assignment.constexprGuards.empty()
                 ? "  "
                 : "  if constexpr (" +
                       conjunction(assignment.constexprGuards) + ") ") +
            target->alias + "." + assignment.targetPort + " = " +
            assignment.macro + "(" + expression + ");\n";
        bindingStatements.push_back(BindingStatement{
            statement,
            referencedInstances(statement, {target->id, instance->id})});
        }
      }
    }

    // Dependency diagnostics duplicate long specialized type names. Build
    // that optional public view only if dependencyTrees() is requested; the
    // normal generator needs the compact node graph but not this debug copy.
    trees.clear();
    treesBuilt = false;
    tracePhase("dependency metadata deferred");

    std::filesystem::create_directories(directory);
    const std::regex staleChunkPattern(
        "^" + shortRoot +
        R"(_optimized_combs_(?:(?:work|bind|strobe|model|dynamic)_)?[0-9]+\.cpp$)");
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
           << "void calc_all(" << shortRoot << "& obj, bool reset);\n"
           << "void commit_optimized_regs(" << shortRoot << "& obj);\n";
    if (!finishOutput(header, headerPath)) {
      return false;
    }

    std::set<std::string> headers;
    std::set<std::string> headerNames;
    for (const auto &[name, info] : classes) {
      if (!info.header.empty() && info.header.ends_with(".h")) {
        headers.insert(info.header);
        headerNames.insert(
            std::filesystem::path(info.header).filename().string());
      }
    }

    constexpr size_t valuesPerChunk = 400;
    constexpr size_t dynamicValuesPerChunk = 200;
    constexpr size_t bindingsPerChunk = 400;
    constexpr size_t modelTypesPerChunk = 50;
    std::vector<size_t> dynamicSchedule(dynamicNodes.begin(),
                                        dynamicNodes.end());
    std::sort(dynamicSchedule.begin(), dynamicSchedule.end());
    const size_t chunkCount =
        (schedule.size() + valuesPerChunk - 1) / valuesPerChunk;
    const size_t dynamicChunkCount =
        (dynamicSchedule.size() + dynamicValuesPerChunk - 1) /
        dynamicValuesPerChunk;
    const size_t bindChunkCount =
        (bindingStatements.size() + bindingsPerChunk - 1) / bindingsPerChunk;
    const std::vector<std::string> workChunks = splitWork(work, 400);
    const std::vector<std::string> strobeChunks = splitWork(strobe, 400);
    std::map<std::string, const ClassInfo *> concreteTypes;
    for (const Instance *instance : instances) {
      if (!bodyText(instance->type->constructorBody).empty() ||
          !bodyText(instance->type->destructorBody).empty()) {
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
    internal << "#pragma once\n#include <limits>\n#include <memory>\n#include "
                "<type_traits>\n#include <unordered_map>\n#include <utility>\n";
    for (const std::string &include : sourceIncludes) {
      const std::string includeName =
          std::filesystem::path(include).filename().string();
      if (!include.ends_with(".h") || headerNames.contains(includeName) ||
          include == rootHeader || includeName == rootHeaderName ||
          includeName == "cpphdl_support.h") {
        continue;
      }
      internal << "#include \"" << include << "\"\n";
    }
    // Load CppHDL and its standard-library dependencies before changing
    // access control while reading generated module declarations.
    internal << "#include \"cpphdl.h\"\n";
    internal << "#define private public\n";
    // Root first makes the declaration in the generated API obvious.
    internal << "#include \"" << rootHeader << "\"\n";
    for (auto iterator = headers.begin(); iterator != headers.end();) {
      if (*iterator == rootHeader ||
          std::filesystem::path(*iterator).filename() == rootHeaderName) {
        iterator = headers.erase(iterator);
      } else {
        ++iterator;
      }
    }
    for (const std::string &include : headers) {
      internal << "#include \"" << include << "\"\n";
    }
    internal << "#undef private\n\n"
             << "struct " << shortRoot << "_optimized_combs_state {\n";
    internal << "  long evaluated_system_clock = "
                "std::numeric_limits<long>::min();\n";
    std::unordered_set<size_t> stateNodes(schedule.begin(), schedule.end());
    stateNodes.insert(dynamicSchedule.begin(), dynamicSchedule.end());
    for (size_t id = 0; id < nodes.size(); ++id) {
      if (!stateNodes.contains(id)) {
        continue;
      }
      const Node &node = nodes[id];
      if (node.kind != NodeKind::Port) {
        continue;
      }
      internal << "  std::remove_reference_t<decltype("
               << instanceExpression(
                      *node.instance,
                      "std::declval<" + shortRoot + "&>()")
               << "." << node.name << "())> p" << id << ";\n";
    }
    // Ports cache for a clock; combs only guard recursive active calls.
    // A comb executes again after returning, while an SCC backedge observes
    // the storage accumulated by the currently active invocation.
    for (const size_t id : dynamicSchedule) {
      if (cachesForSystemClock(nodes[id])) {
        internal << "  bool evaluated" << id << " = false;\n";
      } else {
        internal << "  bool evaluating" << id << " = false;\n";
      }
    }
    internal << "};\n\n";
    for (const size_t id : dynamicSchedule) {
      internal << "void " << shortRoot << "_optimized_comb_eval_" << id
               << "(" << shortRoot << "&, "
               << shortRoot << "_optimized_combs_state&);\n";
    }
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
               << "_optimized_combs_state&, bool);\n";
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
      // Instance-reference scans allocate temporary match and string buffers.
      // Each emitted chunk is independent, so release its completed buffers
      // before scanning the next chunk of a large specialized hierarchy.
      releaseTemporaryAllocatorPages();
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
        if (!bodyText(type.constructorBody).empty()) {
          chunkSource << type.name << "::" << baseName << "() "
                      << bodyText(type.constructorBody) << "\n\n";
        }
        if (!bodyText(type.destructorBody).empty()) {
          chunkSource << type.name << "::~" << baseName << "() "
                      << bodyText(type.destructorBody) << "\n\n";
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
      // Dependency scans and source assembly allocate temporary buffers for
      // each scheduled-value group. Return them before building the next group
      // so emission memory stays proportional to one generated source chunk.
      releaseTemporaryAllocatorPages();
    }

    for (size_t chunk = 0; chunk < dynamicChunkCount; ++chunk) {
      const size_t begin = chunk * dynamicValuesPerChunk;
      const size_t end =
          std::min(dynamicSchedule.size(), begin + dynamicValuesPerChunk);
      const std::filesystem::path chunkPath =
          std::filesystem::path(directory) /
          (shortRoot + "_optimized_combs_dynamic_" +
           std::to_string(chunk) + ".cpp");
      std::ofstream chunkSource(chunkPath);
      if (!chunkSource) {
        error = "cannot write " + chunkPath.string();
        return false;
      }
      chunkSource << "#include \"" << shortRoot
                  << "_optimized_combs_internal.h\"\n\n";
      for (size_t position = begin; position < end; ++position) {
        const size_t id = dynamicSchedule[position];
        const Node &node = nodes[id];
        const std::set<size_t> usedInstances =
            referencedInstances(node.expression, {0, node.instance->id});
        chunkSource << "void " << shortRoot << "_optimized_comb_eval_" << id
                    << "(" << shortRoot
                    << "& obj, " << shortRoot
                    << "_optimized_combs_state& s) {\n";
        emitAliases(chunkSource, usedInstances);
        // Ports retain function_ref's clock cache; combs guard only recursion.
        // Clear a comb guard after its body so later call sites execute again.
        // This preserves both SCC backedges and stateful repeated comb calls.
        if (cachesForSystemClock(node)) {
          chunkSource << "  if (s.evaluated" << id << ") return;\n"
                      << "  s.evaluated" << id << " = true;\n";
        } else {
          chunkSource << "  if (s.evaluating" << id << ") return;\n"
                      << "  s.evaluating" << id << " = true;\n";
        }
        chunkSource << "  " << nodeValue(id) << " = " << node.expression
                    << ";\n";
        if (!cachesForSystemClock(node)) {
          chunkSource << "  s.evaluating" << id << " = false;\n";
        }
        chunkSource << "}\n\n";
      }
      if (!finishOutput(chunkSource, chunkPath)) {
        return false;
      }
      // Dynamic evaluator chunks preserve source branch order inside SCC
      // consumers. Emit them independently so compile memory remains bounded
      // by one specialized source group rather than the full feedback cone.
      releaseTemporaryAllocatorPages();
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
                  << "& obj, " << shortRoot
                  << "_optimized_combs_state& s, bool __cpphdl_reset) {\n";
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
    // Flattening replaces calls to many module _work methods, but their reset
    // argument remains part of the model's cycle semantics. Thread that one
    // runtime value through the dispatcher and every emitted work chunk.
    source << "}\n\n"
           << "void calc_all(" << shortRoot << "& obj, bool reset) {\n"
           << "  static thread_local std::unordered_map<" << shortRoot
           << "*, " << shortRoot << "_optimized_combs_state> states;\n"
           << "  auto& s = states[&obj];\n";
    // calc_all may be reached by more than one observer in a host cycle.
    // Match function_ref and _LAZY_COMB by evaluating the specialized graph
    // only once for each root object and _system_clock value.
    source << "  if (s.evaluated_system_clock == _system_clock) return;\n"
           << "  s.evaluated_system_clock = _system_clock;\n";
    // Port temporaries retain their preceding-clock value just like a lazy
    // function_ref cache, while only evaluation flags reset at clock start.
    // This also keeps independent optimized roots isolated by object address.
    // Reset only function_ref-equivalent port cache flags each clock.
    // Internal comb methods carry storage but are never call-result caches.
    // Their generated evaluators therefore have no state flag to clear.
    for (const size_t id : dynamicSchedule) {
      if (cachesForSystemClock(nodes[id])) {
        source << "  s.evaluated" << id << " = false;\n";
      }
    }
    for (size_t chunk = 0; chunk < chunkCount; ++chunk) {
      source << "  " << shortRoot << "_optimized_combs_chunk_" << chunk
             << "(obj, s);\n";
    }
    for (size_t chunk = 0; chunk < workChunks.size(); ++chunk) {
      source << "  " << shortRoot << "_optimized_combs_work_chunk_" << chunk
             << "(obj, s, reset);\n";
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
                 << modelChunkCount << " model chunks, "
                 << dynamicSchedule.size() << " dynamic values, "
                 << lazyCycleBackEdges << " lazy cycle back-edges\n"
                 << "  " << headerPath.string() << "\n"
                 << "  " << sourcePath.string() << "\n"
                 << "  " << internalPath.string() << "\n";
    return true;
  }
};

CombsOptimizer::CombsOptimizer(std::string rootModule)
    : impl(std::make_unique<Impl>(std::move(rootModule))) {}
CombsOptimizer::~CombsOptimizer() = default;
CombsOptimizer::CombsOptimizer(CombsOptimizer &&) noexcept = default;
CombsOptimizer &CombsOptimizer::operator=(CombsOptimizer &&) noexcept = default;

void CombsOptimizer::collect(clang::ASTContext &context) {
  Collector collector(impl->classes, impl->sourceIncludes,
                      impl->constantValues, impl->rootName, context);
  collector.collectMainFileIncludes();
  collector.collectRootHierarchy();
  collector.TraverseDecl(context.getTranslationUnitDecl());
  collector.applyTemplateMethods();
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
  return impl->dependencyTrees();
}

} // namespace cpphdl
