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
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <map>
#include <memory>
#include <numeric>
#include <optional>
#include <queue>
#include <regex>
#include <set>
#include <sstream>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <zlib.h>

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

std::optional<unsigned> firstIntegerLiteral(std::string_view text) {
  for (size_t index = 0; index < text.size(); ++index) {
    if (!std::isdigit(static_cast<unsigned char>(text[index]))) {
      continue;
    }
    if (index != 0 && identifierPart(text[index - 1])) {
      continue;
    }
    size_t end = index + 1;
    while (end < text.size() &&
           std::isdigit(static_cast<unsigned char>(text[end]))) {
      ++end;
    }
    if (end < text.size() && identifierPart(text[end])) {
      index = end;
      continue;
    }
    try {
      return static_cast<unsigned>(
          std::stoul(std::string(text.substr(index, end - index))));
    } catch (const std::exception &) {
      return std::nullopt;
    }
  }
  return std::nullopt;
}

std::optional<size_t> topLevelAssignment(const std::string &text) {
  int parentheses = 0;
  int brackets = 0;
  int braces = 0;
  for (size_t index = 0; index < text.size(); ++index) {
    switch (text[index]) {
    case '(':
      ++parentheses;
      break;
    case ')':
      --parentheses;
      break;
    case '[':
      ++brackets;
      break;
    case ']':
      --brackets;
      break;
    case '{':
      ++braces;
      break;
    case '}':
      --braces;
      break;
    case '=': {
      if (parentheses != 0 || brackets != 0 || braces != 0) {
        break;
      }
      const char before = index == 0 ? '\0' : text[index - 1];
      const char after = index + 1 == text.size() ? '\0' : text[index + 1];
      if (before != '=' && before != '!' && before != '<' && before != '>' &&
          before != '+' && before != '-' && before != '*' && before != '/' &&
          before != '%' && before != '&' && before != '|' && before != '^' &&
          after != '=') {
        return index;
      }
      break;
    }
    default:
      break;
    }
  }
  return std::nullopt;
}

std::vector<std::string> splitTopLevel(const std::string &text, char delimiter) {
  std::vector<std::string> result;
  size_t start = 0;
  int parentheses = 0;
  int brackets = 0;
  int braces = 0;
  for (size_t index = 0; index < text.size(); ++index) {
    switch (text[index]) {
    case '(':
      ++parentheses;
      break;
    case ')':
      --parentheses;
      break;
    case '[':
      ++brackets;
      break;
    case ']':
      --brackets;
      break;
    case '{':
      ++braces;
      break;
    case '}':
      --braces;
      break;
    default:
      if (text[index] == delimiter && parentheses == 0 && brackets == 0 &&
          braces == 0) {
        result.push_back(trim(text.substr(start, index - start)));
        start = index + 1;
      }
      break;
    }
  }
  result.push_back(trim(text.substr(start)));
  return result;
}

std::string hexMask(unsigned low, unsigned high) {
  const uint64_t highMask =
      high == 63 ? ~uint64_t{0} : ((uint64_t{1} << (high + 1)) - 1);
  const uint64_t lowMask = low == 0 ? 0 : ((uint64_t{1} << low) - 1);
  std::ostringstream output;
  output << "0x" << std::hex << (highMask & ~lowMask) << "ull";
  return output.str();
}

std::optional<unsigned> packedLogicWidth(const std::string &type) {
  static const std::regex pattern(
      R"((cpphdl::)?logic<\s*([0-9]+)\s*>)");
  std::smatch match;
  if (!std::regex_search(type, match, pattern)) {
    return std::nullopt;
  }
  return static_cast<unsigned>(std::stoul(match[2].str()));
}

struct MathReplacement {
  std::string expression;
  const char *kind = nullptr;
};

std::string bodyInterior(const std::string &body);

class MathCombRewriter {
public:
  static std::optional<MathReplacement>
  rewrite(const std::string &method, const std::string &body,
          unsigned targetWidth) {
    const std::string target = method + "_cache";
    std::vector<BitAssignment> bits;
    std::optional<SliceAssignment> slice;

    std::istringstream input(bodyInterior(body));
    std::string line;
    while (std::getline(input, line)) {
      line = trim(line);
      if (line.empty() || line == "return " + target + ";") {
        continue;
      }
      if (!line.empty() && line.back() == ';') {
        line.pop_back();
        line = trim(line);
      }
      const auto equals = topLevelAssignment(line);
      if (!equals) {
        return std::nullopt;
      }
      const std::string lhs = trim(line.substr(0, *equals));
      const std::string rhs = trim(line.substr(*equals + 1));
      if (rhs.empty()) {
        return std::nullopt;
      }
      if (lhs.starts_with(target + "[") && lhs.ends_with(']')) {
        const size_t opening = target.size();
        const auto closing = matchingDelimiter(lhs, opening, '[', ']');
        if (!closing || *closing + 1 != lhs.size()) {
          return std::nullopt;
        }
        const auto index = firstIntegerLiteral(
            std::string_view(lhs).substr(opening + 1, *closing - opening - 1));
        if (!index) {
          return std::nullopt;
        }
        bits.push_back(BitAssignment{*index, rhs});
        continue;
      }
      const std::string bitsPrefix = target + ".bits(";
      if (lhs.starts_with(bitsPrefix) && lhs.ends_with(')') && !slice) {
        const size_t opening = bitsPrefix.size() - 1;
        const auto closing = matchingDelimiter(lhs, opening, '(', ')');
        if (!closing || *closing + 1 != lhs.size()) {
          return std::nullopt;
        }
        const auto arguments = splitTopLevel(
            lhs.substr(opening + 1, *closing - opening - 1), ',');
        if (arguments.size() != 2) {
          return std::nullopt;
        }
        const auto high = firstIntegerLiteral(arguments[0]);
        const auto low = firstIntegerLiteral(arguments[1]);
        if (!high || !low || *high < *low) {
          return std::nullopt;
        }
        slice = SliceAssignment{*high, *low, rhs};
        continue;
      }
      return std::nullopt;
    }

    if (bits.size() < 2) {
      return std::nullopt;
    }
    if (auto replacement = replicatedBits(bits, slice, targetWidth)) {
      return replacement;
    }
    if (!slice) {
      return reversedBits(bits, targetWidth);
    }
    return std::nullopt;
  }

private:
  struct BitAssignment {
    unsigned index;
    std::string value;
  };
  struct SliceAssignment {
    unsigned high;
    unsigned low;
    std::string value;
  };

  static bool uniqueContiguous(const std::vector<BitAssignment> &bits,
                               unsigned low, unsigned high) {
    if (high < low || bits.size() != high - low + 1) {
      return false;
    }
    std::vector<bool> seen(high - low + 1, false);
    for (const auto &bit : bits) {
      if (bit.index < low || bit.index > high || seen[bit.index - low]) {
        return false;
      }
      seen[bit.index - low] = true;
    }
    return true;
  }

  static std::optional<MathReplacement>
  replicatedBits(const std::vector<BitAssignment> &bits,
                 const std::optional<SliceAssignment> &slice,
                 unsigned targetWidth) {
    const std::string value = bits.front().value;
    if (std::any_of(bits.begin(), bits.end(), [&](const BitAssignment &bit) {
          return bit.value != value;
        })) {
      return std::nullopt;
    }
    const unsigned low = slice ? slice->high + 1 : 0;
    const unsigned high = std::max_element(
                              bits.begin(), bits.end(),
                              [](const auto &left, const auto &right) {
                                return left.index < right.index;
                              })
                              ->index;
    if (targetWidth == 0 || targetWidth > 64 || high + 1 != targetWidth ||
        !uniqueContiguous(bits, low, high) ||
        (slice && slice->low != 0)) {
      return std::nullopt;
    }
    const unsigned width = targetWidth;
    std::ostringstream expression;
    expression << "cpphdl::logic<" << width << ">((((uint64_t)(" << value
               << ") & 1ull) ? " << hexMask(low, high) << " : 0ull)";
    if (slice) {
      expression << " | ((uint64_t)(" << slice->value << ") & "
                 << hexMask(0, slice->high) << ")";
    }
    expression << ")";
    return MathReplacement{expression.str(),
                           slice ? "sign-extension" : "bit-replication"};
  }

  static std::optional<MathReplacement>
  reversedBits(const std::vector<BitAssignment> &bits, unsigned targetWidth) {
    const unsigned width = targetWidth;
    if ((width != 8 && width != 16 && width != 32 && width != 64) ||
        !uniqueContiguous(bits, 0, width - 1)) {
      return std::nullopt;
    }

    std::optional<std::string> source;
    static const std::regex castPattern(
        R"(static_cast<(?:cpphdl::)?logic<([0-9]+)>>\s*\()"
    );
    for (const auto &bit : bits) {
      std::smatch match;
      if (!std::regex_search(bit.value, match, castPattern) ||
          std::stoul(match[1].str()) != width) {
        return std::nullopt;
      }
      const size_t opening = match.position() + match.length() - 1;
      const auto closing = matchingDelimiter(bit.value, opening, '(', ')');
      if (!closing) {
        return std::nullopt;
      }
      const std::string candidate = trim(
          bit.value.substr(opening + 1, *closing - opening - 1));
      const size_t shift = bit.value.find(">>", *closing + 1);
      if (shift == std::string::npos) {
        return std::nullopt;
      }
      const auto sourceIndex =
          firstIntegerLiteral(std::string_view(bit.value).substr(shift + 2));
      if (!sourceIndex || bit.index + *sourceIndex != width - 1 ||
          (source && *source != candidate)) {
        return std::nullopt;
      }
      source = candidate;
    }
    if (!source) {
      return std::nullopt;
    }

    const unsigned hostWidth = width <= 32 ? 32 : 64;
    std::ostringstream expression;
    expression << "cpphdl::logic<" << width
               << ">(cpphdl_optimized_math::bit_reverse" << hostWidth
               << "((std::uint" << hostWidth << "_t)(uint64_t)(" << *source
               << "))";
    if (width < hostWidth) {
      expression << " >> " << (hostWidth - width);
    }
    expression << ")";
    return MathReplacement{expression.str(), "bit-reversal"};
  }
};

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
  std::optional<unsigned> packedWidth;
  std::string childClass;
  std::string portModuleClass;
  std::string initializer;
  std::vector<size_t> portModuleDimensions;
  size_t childCount = 0;
  std::vector<size_t> childDimensions;
  bool childPointer = false;
  bool indexedStorage = false;
  FieldKind kind = FieldKind::Other;
};

struct FreeHelper {
  std::optional<std::string> receiverParameter;
  std::string method;
};

struct ClassInfo {
  struct BodyData {
    std::shared_ptr<const std::string> text;
    std::string collectionPath;
    uint64_t collectionIndex = 0;
  };
  using Body = std::shared_ptr<BodyData>;

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
  // Derived from collected method bodies after deferred bodies are loaded.
  // It need not be serialized in collection shards.
  std::map<std::string, std::string> inlineMethods;
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
  bool typeAlias = false;
  // Oversized concrete specializations can remain normal CppHDL subtrees.
  // Their lifecycle calls are preserved while the surrounding hierarchy is
  // flattened, avoiding semantic instantiation solely for optimization.
  bool opaque = false;
};

struct CollectionWriter {
  explicit CollectionWriter(const std::string &path)
      : output(gzopen(path.c_str(), "wb1")), valid(output != nullptr) {}
  ~CollectionWriter() {
    if (output) {
      valid = gzclose(output) == Z_OK && valid;
    }
  }

  void number(uint64_t value) {
    bytes(reinterpret_cast<const char *>(&value), sizeof(value));
  }
  void flag(bool value) { number(value ? 1 : 0); }
  void text(const std::string &value) {
    number(value.size());
    bytes(value.data(), value.size());
  }
  void bytes(const char *data, size_t size) {
    while (valid && size != 0) {
      const unsigned int chunk = static_cast<unsigned int>(
          std::min<size_t>(size, std::numeric_limits<unsigned int>::max()));
      const int written = gzwrite(output, data, chunk);
      if (written <= 0 || static_cast<unsigned int>(written) != chunk) {
        valid = false;
        return;
      }
      data += chunk;
      size -= chunk;
    }
  }
  bool close() {
    if (!output) {
      return false;
    }
    valid = gzclose(output) == Z_OK && valid;
    output = nullptr;
    return valid;
  }
  explicit operator bool() const { return output && valid; }

  gzFile output = nullptr;
  bool valid = true;
};

struct CollectionReader {
  explicit CollectionReader(const std::string &path)
      : input(gzopen(path.c_str(), "rb")), valid(input != nullptr) {}
  ~CollectionReader() {
    if (input) {
      gzclose(input);
    }
  }

  uint64_t number() {
    uint64_t value = 0;
    bytes(reinterpret_cast<char *>(&value), sizeof(value));
    return value;
  }
  bool flag() { return number() != 0; }
  std::string text() {
    const uint64_t size = number();
    if (!valid || size > (uint64_t{1} << 34)) {
      valid = false;
      return {};
    }
    std::string value(static_cast<size_t>(size), '\0');
    bytes(value.data(), value.size());
    return value;
  }
  void bytes(char *data, size_t size) {
    while (valid && size != 0) {
      const unsigned int chunk = static_cast<unsigned int>(
          std::min<size_t>(size, std::numeric_limits<unsigned int>::max()));
      const int read = gzread(input, data, chunk);
      if (read <= 0 || static_cast<unsigned int>(read) != chunk) {
        valid = false;
        return;
      }
      data += chunk;
      size -= chunk;
    }
  }
  void skip(size_t size) {
    char buffer[64 * 1024];
    while (valid && size != 0) {
      const size_t chunk = std::min(size, sizeof(buffer));
      bytes(buffer, chunk);
      size -= chunk;
    }
  }
  void fail() { valid = false; }
  explicit operator bool() const { return input && valid; }

  gzFile input = nullptr;
  bool valid = true;
};

void writeStrings(CollectionWriter &writer,
                  const std::vector<std::string> &values) {
  writer.number(values.size());
  for (const auto &value : values) {
    writer.text(value);
  }
}

void readStrings(CollectionReader &reader, std::vector<std::string> &values) {
  const uint64_t count = reader.number();
  values.clear();
  values.reserve(static_cast<size_t>(count));
  for (uint64_t index = 0; index < count && reader; ++index) {
    values.push_back(reader.text());
  }
}

void writeStringSet(CollectionWriter &writer,
                    const std::set<std::string> &values) {
  writer.number(values.size());
  for (const auto &value : values) {
    writer.text(value);
  }
}

void readStringSet(CollectionReader &reader, std::set<std::string> &values) {
  const uint64_t count = reader.number();
  values.clear();
  for (uint64_t index = 0; index < count && reader; ++index) {
    values.insert(reader.text());
  }
}

void writeStringMap(CollectionWriter &writer,
                    const std::map<std::string, std::string> &values) {
  writer.number(values.size());
  for (const auto &[key, value] : values) {
    writer.text(key);
    writer.text(value);
  }
}

void readStringMap(CollectionReader &reader,
                   std::map<std::string, std::string> &values) {
  const uint64_t count = reader.number();
  values.clear();
  for (uint64_t index = 0; index < count && reader; ++index) {
    auto key = reader.text();
    auto value = reader.text();
    values.insert_or_assign(std::move(key), std::move(value));
  }
}

void writeStringSizeMap(CollectionWriter &writer,
                        const std::map<std::string, size_t> &values) {
  writer.number(values.size());
  for (const auto &[key, value] : values) {
    writer.text(key);
    writer.number(value);
  }
}

void readStringSizeMap(CollectionReader &reader,
                       std::map<std::string, size_t> &values) {
  const uint64_t count = reader.number();
  values.clear();
  for (uint64_t index = 0; index < count && reader; ++index) {
    auto key = reader.text();
    values.insert_or_assign(std::move(key),
                            static_cast<size_t>(reader.number()));
  }
}

void writeStringUintMap(
    CollectionWriter &writer,
    const std::unordered_map<std::string, uint64_t> &values) {
  std::map<std::string, uint64_t> ordered(values.begin(), values.end());
  writer.number(ordered.size());
  for (const auto &[key, value] : ordered) {
    writer.text(key);
    writer.number(value);
  }
}

void readStringUintMap(CollectionReader &reader,
                       std::unordered_map<std::string, uint64_t> &values) {
  const uint64_t count = reader.number();
  values.clear();
  for (uint64_t index = 0; index < count && reader; ++index) {
    auto key = reader.text();
    values.insert_or_assign(std::move(key), reader.number());
  }
}

void writeOrderedStringUintMap(
    CollectionWriter &writer,
    const std::map<std::string, uint64_t> &values) {
  writer.number(values.size());
  for (const auto &[key, value] : values) {
    writer.text(key);
    writer.number(value);
  }
}

void readOrderedStringUintMap(CollectionReader &reader,
                              std::map<std::string, uint64_t> &values) {
  const uint64_t count = reader.number();
  values.clear();
  for (uint64_t index = 0; index < count && reader; ++index) {
    auto key = reader.text();
    values.insert_or_assign(std::move(key), reader.number());
  }
}

void writeStringSetMap(
    CollectionWriter &writer,
    const std::map<std::string, std::set<std::string>> &values) {
  writer.number(values.size());
  for (const auto &[key, value] : values) {
    writer.text(key);
    writeStringSet(writer, value);
  }
}

void readStringSetMap(
    CollectionReader &reader,
    std::map<std::string, std::set<std::string>> &values) {
  const uint64_t count = reader.number();
  values.clear();
  for (uint64_t index = 0; index < count && reader; ++index) {
    auto key = reader.text();
    std::set<std::string> value;
    readStringSet(reader, value);
    values.insert_or_assign(std::move(key), std::move(value));
  }
}

struct CollectionBodyTable {
  void add(const ClassInfo::Body &body) {
    if (!body || !body->text || indexes.contains(*body->text)) {
      return;
    }
    indexes.emplace(*body->text, bodies.size());
    bodies.push_back(body->text.get());
  }

  std::unordered_map<std::string_view, uint64_t> indexes;
  std::vector<const std::string *> bodies;
};

void writeBody(CollectionWriter &writer, const ClassInfo::Body &body,
               const CollectionBodyTable &table) {
  if (!body) {
    writer.number(0);
    return;
  }
  writer.number(table.indexes.at(*body->text) + 1);
}

ClassInfo::Body readBody(
    CollectionReader &reader,
    const std::vector<ClassInfo::Body> &bodies) {
  const uint64_t encoded = reader.number();
  if (encoded == 0) {
    return nullptr;
  }
  const uint64_t index = encoded - 1;
  if (index >= bodies.size()) {
    reader.fail();
    return nullptr;
  }
  return bodies[static_cast<size_t>(index)];
}

void writeClassInfo(CollectionWriter &writer, const ClassInfo &info,
                    const CollectionBodyTable &bodies) {
  writer.text(info.name);
  writer.text(info.templateBase);
  writer.text(info.header);
  writeStrings(writer, info.templateParameterOrder);
  writeStringMap(writer, info.templateParameterDeclarations);
  writeStringMap(writer, info.templateSubstitutions);
  writeStringMap(writer, info.templateTypeAccess);
  writeStringSizeMap(writer, info.templateTypeAccessDepth);
  writeStringMap(writer, info.templateParameterTypes);
  writeStringSet(writer, info.typeTemplateParameters);
  writeStringSet(writer, info.integralTemplateParameters);
  writeStringSet(writer, info.structuralTemplateParameters);
  writeStringSetMap(writer, info.methodTemplateParameters);
  writeOrderedStringUintMap(writer, info.constantValues);
  writeStringSet(writer, info.nestedTypes);
  writer.number(info.fields.size());
  for (const auto &[name, field] : info.fields) {
    writer.text(name);
    writer.text(field.name);
    writer.text(field.type);
    writer.number(field.packedWidth ? static_cast<uint64_t>(*field.packedWidth) + 1
                                    : 0);
    writer.text(field.childClass);
    writer.text(field.portModuleClass);
    writer.text(field.initializer);
    writer.number(field.portModuleDimensions.size());
    for (size_t value : field.portModuleDimensions) writer.number(value);
    writer.number(field.childCount);
    writer.number(field.childDimensions.size());
    for (size_t value : field.childDimensions) writer.number(value);
    writer.flag(field.childPointer);
    writer.flag(field.indexedStorage);
    writer.number(static_cast<uint64_t>(field.kind));
  }
  writer.number(info.methods.size());
  for (const auto &[name, body] : info.methods) {
    writer.text(name);
    writeBody(writer, body, bodies);
  }
  writeStringSet(writer, info.concreteMethods);
  writeStringSet(writer, info.workMutatedFields);
  writeBody(writer, info.constructorBody, bodies);
  writeBody(writer, info.destructorBody, bodies);
  writeBody(writer, info.assignBody, bodies);
  writeBody(writer, info.workBody, bodies);
  writeBody(writer, info.strobeBody, bodies);
  writer.flag(info.useTemplatePatternMethods);
  writer.flag(info.supportsTemplateArgumentDeduction);
  writer.flag(info.typeAlias);
  writer.flag(info.opaque);
}

ClassInfo readClassInfo(CollectionReader &reader,
                        const std::vector<ClassInfo::Body> &bodies) {
  ClassInfo info;
  info.name = reader.text();
  info.templateBase = reader.text();
  info.header = reader.text();
  readStrings(reader, info.templateParameterOrder);
  readStringMap(reader, info.templateParameterDeclarations);
  readStringMap(reader, info.templateSubstitutions);
  readStringMap(reader, info.templateTypeAccess);
  readStringSizeMap(reader, info.templateTypeAccessDepth);
  readStringMap(reader, info.templateParameterTypes);
  readStringSet(reader, info.typeTemplateParameters);
  readStringSet(reader, info.integralTemplateParameters);
  readStringSet(reader, info.structuralTemplateParameters);
  readStringSetMap(reader, info.methodTemplateParameters);
  readOrderedStringUintMap(reader, info.constantValues);
  readStringSet(reader, info.nestedTypes);
  const uint64_t fieldCount = reader.number();
  for (uint64_t index = 0; index < fieldCount && reader; ++index) {
    auto name = reader.text();
    FieldInfo field;
    field.name = reader.text();
    field.type = reader.text();
    const uint64_t packedWidth = reader.number();
    if (packedWidth != 0) {
      field.packedWidth = static_cast<unsigned>(packedWidth - 1);
    }
    field.childClass = reader.text();
    field.portModuleClass = reader.text();
    field.initializer = reader.text();
    const uint64_t portDimensions = reader.number();
    for (uint64_t dimension = 0; dimension < portDimensions && reader;
         ++dimension) {
      field.portModuleDimensions.push_back(
          static_cast<size_t>(reader.number()));
    }
    field.childCount = static_cast<size_t>(reader.number());
    const uint64_t childDimensions = reader.number();
    for (uint64_t dimension = 0; dimension < childDimensions && reader;
         ++dimension) {
      field.childDimensions.push_back(static_cast<size_t>(reader.number()));
    }
    field.childPointer = reader.flag();
    field.indexedStorage = reader.flag();
    field.kind = static_cast<FieldKind>(reader.number());
    info.fields.insert_or_assign(std::move(name), std::move(field));
  }
  const uint64_t methodCount = reader.number();
  for (uint64_t index = 0; index < methodCount && reader; ++index) {
    auto name = reader.text();
    info.methods.insert_or_assign(std::move(name), readBody(reader, bodies));
  }
  readStringSet(reader, info.concreteMethods);
  readStringSet(reader, info.workMutatedFields);
  info.constructorBody = readBody(reader, bodies);
  info.destructorBody = readBody(reader, bodies);
  info.assignBody = readBody(reader, bodies);
  info.workBody = readBody(reader, bodies);
  info.strobeBody = readBody(reader, bodies);
  info.useTemplatePatternMethods = reader.flag();
  info.supportsTemplateArgumentDeduction = reader.flag();
  info.typeAlias = reader.flag();
  info.opaque = reader.flag();
  return info;
}

// Collection shards can mention the same concrete class with different amounts
// of instantiated metadata. Preserve declarations from every shard while giving
// the latest shard precedence when both contain the same concrete definition.
void mergeClassInfo(ClassInfo &target, ClassInfo incoming) {
  const auto keepString = [](std::string &value, const std::string &fallback) {
    if (value.empty()) value = fallback;
  };
  const auto mergeMap = [](auto &values, const auto &fallback) {
    for (const auto &[name, value] : fallback) {
      values.try_emplace(name, value);
    }
  };
  const auto mergeSet = [](auto &values, const auto &fallback) {
    values.insert(fallback.begin(), fallback.end());
  };

  keepString(incoming.name, target.name);
  keepString(incoming.templateBase, target.templateBase);
  keepString(incoming.header, target.header);
  if (incoming.templateParameterOrder.empty()) {
    incoming.templateParameterOrder = target.templateParameterOrder;
  }
  mergeMap(incoming.templateParameterDeclarations,
           target.templateParameterDeclarations);
  mergeMap(incoming.templateSubstitutions, target.templateSubstitutions);
  mergeMap(incoming.templateTypeAccess, target.templateTypeAccess);
  mergeMap(incoming.templateTypeAccessDepth, target.templateTypeAccessDepth);
  mergeMap(incoming.templateParameterTypes, target.templateParameterTypes);
  mergeSet(incoming.typeTemplateParameters, target.typeTemplateParameters);
  mergeSet(incoming.integralTemplateParameters,
           target.integralTemplateParameters);
  mergeSet(incoming.structuralTemplateParameters,
           target.structuralTemplateParameters);
  for (const auto &[method, parameters] : target.methodTemplateParameters) {
    mergeSet(incoming.methodTemplateParameters[method], parameters);
  }
  mergeMap(incoming.constantValues, target.constantValues);
  mergeSet(incoming.nestedTypes, target.nestedTypes);
  for (const auto &[name, oldField] : target.fields) {
    const auto [position, inserted] = incoming.fields.try_emplace(name, oldField);
    if (!inserted && position->second.initializer.empty()) {
      position->second.initializer = oldField.initializer;
    }
    if (!inserted && !position->second.packedWidth) {
      position->second.packedWidth = oldField.packedWidth;
    }
  }
  mergeMap(incoming.methods, target.methods);
  mergeSet(incoming.concreteMethods, target.concreteMethods);
  mergeSet(incoming.workMutatedFields, target.workMutatedFields);
  if (!incoming.constructorBody)
    incoming.constructorBody = target.constructorBody;
  if (!incoming.destructorBody)
    incoming.destructorBody = target.destructorBody;
  if (!incoming.assignBody) incoming.assignBody = target.assignBody;
  if (!incoming.workBody) incoming.workBody = target.workBody;
  if (!incoming.strobeBody) incoming.strobeBody = target.strobeBody;
  incoming.useTemplatePatternMethods =
      incoming.useTemplatePatternMethods || target.useTemplatePatternMethods;
  incoming.supportsTemplateArgumentDeduction =
      incoming.supportsTemplateArgumentDeduction &&
      target.supportsTemplateArgumentDeduction;
  incoming.typeAlias = incoming.typeAlias || target.typeAlias;
  incoming.opaque = incoming.opaque || target.opaque;
  target = std::move(incoming);
}

// Flattened work can pass ordinary module fields to output-reference helpers.
// Capture that semantic write boundary without treating reg::_next updates as
// mutations of the current registered value visible to same-clock combs.
struct FieldReferenceCollector
    : clang::RecursiveASTVisitor<FieldReferenceCollector> {
  FieldReferenceCollector(const ClassInfo &info,
                          std::set<std::string> &fields)
      : info(info), fields(fields) {}

  bool VisitMemberExpr(clang::MemberExpr *expression) {
    // Only the outermost field rooted directly at this belongs to the class
    // whose _work body is being summarized. Traversing child.field must not
    // mark an unrelated parent field that happens to share field's name.
    const clang::Expr *base = expression->getBase()->IgnoreParenImpCasts();
    if (!clang::isa<clang::CXXThisExpr>(base)) {
      return true;
    }
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
  return body && body->text ? *body->text : empty;
}

ClassInfo::Body makeBody(std::string text) {
  auto body = std::make_shared<ClassInfo::BodyData>();
  body->text = std::make_shared<const std::string>(std::move(text));
  return body;
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
                     std::map<std::string, FreeHelper> &freeHelpers,
                     std::set<std::string> &sourceIncludes,
                     std::unordered_map<std::string, uint64_t> &constantValues,
                     std::string rootName,
                     clang::ASTContext &context,
                     bool collectionOnly)
      : classes(classes), freeHelpers(freeHelpers),
        sourceIncludes(sourceIncludes),
        constantValues(constantValues), rootName(std::move(rootName)),
        context(context), collectionOnly(collectionOnly) {}

  void discoverTypeAliases() {
    for (const clang::Decl *declaration :
         context.getTranslationUnitDecl()->decls()) {
      const auto *alias = clang::dyn_cast<clang::TypedefNameDecl>(declaration);
      if (!alias) {
        continue;
      }
      const auto *record =
          alias->getUnderlyingType()->getAsCXXRecordDecl();
      if (!record || !derivesModule(record)) {
        continue;
      }
      const auto *canonical = record->getCanonicalDecl();
      const std::string name = alias->getQualifiedNameAsString();
      const auto known = typeAliases.find(canonical);
      if (known == typeAliases.end() || name.size() < known->second.size()) {
        typeAliases.insert_or_assign(canonical, name);
      }
    }
  }

  std::string typeName(const clang::CXXRecordDecl *record) const {
    if (!record) {
      return {};
    }
    const auto alias = typeAliases.find(record->getCanonicalDecl());
    return alias == typeAliases.end() ? recordTypeName(record, context)
                                      : alias->second;
  }

  bool TraverseStmt(clang::Stmt *) {
    // Relevant method bodies are captured once by VisitCXXMethodDecl as
    // source or semantic AST text. Walking every expression beneath those
    // methods duplicates that work and cannot discover module declarations.
    return true;
  }

  bool matchesRootName(const clang::NamedDecl &declaration) const {
    return declaration.getQualifiedNameAsString() == rootName ||
           (rootName.find("::") == std::string::npos &&
            declaration.getNameAsString() == rootName);
  }

  const clang::CXXRecordDecl *findRoot(const clang::DeclContext *scope) {
    for (const clang::Decl *declaration : scope->decls()) {
      if (const auto *record = clang::dyn_cast<clang::CXXRecordDecl>(declaration);
          record && record->isCompleteDefinition() &&
          (typeName(record) == rootName ||
           matchesRootName(*record))) {
        return record;
      }
      // Specialized hdlcpp models are exposed through compact aliases. Accept
      // an alias as the optimizer root while retaining the underlying record's
      // methods and fields for hierarchy collection.
      if (const auto *alias =
              clang::dyn_cast<clang::TypedefNameDecl>(declaration);
          alias && matchesRootName(*alias)) {
        if (const auto *record =
                alias->getUnderlyingType()->getAsCXXRecordDecl()) {
          rootAlias = alias;
          return record->getDefinition() ? record->getDefinition() : record;
        }
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
    // Preserve compact source aliases for concrete specializations. Clang's
    // canonical spelling can recursively expand structural template values.
    discoverTypeAliases();
    // Bounded collection translation units can precede the final root TU.
    // Restrict AST walking to explicitly reached classes so declarations
    // imported from a PCH remain lazy instead of recreating one giant AST.
    reachableOnly = true;
    const auto *root = findRoot(context.getTranslationUnitDecl());
    bool sawCollectionMarker = false;
    bool sawRootMarker = false;
    for (const clang::Decl *declaration :
         context.getTranslationUnitDecl()->decls()) {
      const auto *variable = clang::dyn_cast<clang::VarDecl>(declaration);
      if (!variable ||
          (!variable->getNameAsString().starts_with("cpphdlCombsCollect") &&
           !variable->getNameAsString().starts_with("cpphdlCombsOpaque"))) {
        continue;
      }
      sawCollectionMarker = true;
      clang::QualType type = variable->getType();
      if (type->isPointerType()) {
        type = type->getPointeeType();
      }
      const auto *record = type->getAsCXXRecordDecl();
      if (!record) {
        continue;
      }
      const bool isRoot = root &&
          record->getCanonicalDecl() == root->getCanonicalDecl();
      if (isRoot) {
        // The final root marker joins classes collected by earlier bounded
        // translation units. A collection-only process captures just the root
        // class; the final loaded process connects its already owned children.
        sawRootMarker = true;
        if (collectionOnly) {
          collectClass(record, false);
        }
        continue;
      }
      if (variable->getNameAsString().starts_with("cpphdlCombsOpaque")) {
        const std::string name = typeName(record);
        if (!name.empty()) {
          ClassInfo &info = classes[name];
          info.name = typeName(record);
          info.opaque = true;
          const auto location = context.getSourceManager().getSpellingLoc(
              record->getLocation());
          if (location.isValid()) {
            info.header =
                context.getSourceManager().getFilename(location).str();
          }
        }
      } else if (derivesModule(record)) {
        collectClass(record, false);
      }
    }
    if (!root) {
      return;
    }

    // Marker-free inputs retain the direct one-TU optimizer behavior. Bounded
    // collection inputs defer root expansion until their explicit root marker,
    // preventing every shard from instantiating the entire concrete hierarchy.
    if (!sawCollectionMarker || sawRootMarker) {
      collectClass(root);
    }
  }

  void materializeRootAlias() {
    if (!rootAlias) {
      return;
    }
    const auto *record = rootAlias->getUnderlyingType()->getAsCXXRecordDecl();
    if (!record) {
      return;
    }
    const std::string concreteRoot = typeName(record);
    if (concreteRoot != rootName) {
      const auto concrete = classes.find(concreteRoot);
      if (concrete != classes.end()) {
        ClassInfo alias = concrete->second;
        alias.name = rootName;
        if (rootAlias) {
          alias.typeAlias = true;
          const auto location = context.getSourceManager().getSpellingLoc(
              rootAlias->getLocation());
          if (location.isValid()) {
            alias.header =
                context.getSourceManager().getFilename(location).str();
          }
        }
        classes.insert_or_assign(rootName, std::move(alias));
      }
    } else if (auto concrete = classes.find(concreteRoot);
               concrete != classes.end()) {
      concrete->second.typeAlias = true;
      const auto location = context.getSourceManager().getSpellingLoc(
          rootAlias->getLocation());
      if (location.isValid()) {
        concrete->second.header =
            context.getSourceManager().getFilename(location).str();
      }
    }
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

  void collectClass(const clang::CXXRecordDecl *declaration,
                    bool collectChildren = true) {
    const std::string name = typeName(declaration);
    if (name.empty() || collecting.contains(name)) {
      return;
    }
    auto &info = classes[name];
    if (info.opaque || !info.fields.empty()) {
      return;
    }
    declaration = declaration->getDefinition() ? declaration->getDefinition()
                                               : declaration;
    collecting.insert(name);
    info.name = typeName(declaration);
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
        if (value.empty() &&
            arguments[index].getKind() == clang::TemplateArgument::Type) {
          value = typeName(
              arguments[index].getAsType()->getAsCXXRecordDecl());
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
      item.packedWidth = packedLogicWidth(item.type);
      item.indexedStorage = field->getType()->isArrayType() ||
                            item.type.find("cpphdl::array<") !=
                                std::string::npos ||
                            item.type.find("std::array<") != std::string::npos;
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
              const std::string valueName = typeName(value);
              const auto knownValue = classes.find(valueName);
              if (value && ((knownValue != classes.end() &&
                             knownValue->second.opaque) ||
                            derivesModule(value))) {
                if (collectChildren) {
                  collectClass(value);
                }
                item.portModuleClass = valueName;
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
        const auto *child = childType->getAsCXXRecordDecl();
        const std::string childName = typeName(child);
        const auto knownChild = classes.find(childName);
        const bool childIsOpaque =
            knownChild != classes.end() && knownChild->second.opaque;
        if (child && (childIsOpaque || derivesModule(child))) {
          if (collectChildren) {
            collectClass(child);
          }
          item.kind = FieldKind::Child;
          item.childClass = childName;
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
          const std::string elementName = typeName(element);
          const auto knownElement = classes.find(elementName);
          if (!dimensions.empty() && element &&
              ((knownElement != classes.end() &&
                knownElement->second.opaque) ||
               derivesModule(element))) {
            if (collectChildren) {
              collectClass(element);
            }
            item.kind = FieldKind::ChildArray;
            item.childClass = elementName;
            item.childDimensions = std::move(dimensions);
            item.childCount = 1;
            for (size_t dimension : item.childDimensions) {
              item.childCount *= dimension;
            }
          }
        }
      }
      // Classification above is the only consumer of the canonical field
      // spelling. Keeping it can recursively expand structural NTTP values.
      item.type.clear();
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
    const std::string name = typeName(declaration);
    const auto known = classes.find(name);
    if (known != classes.end() && known->second.opaque) {
      return true;
    }
    if (declaration->isCompleteDefinition() && derivesModule(declaration) &&
        (!reachableOnly ||
         classes.contains(name))) {
      collectClass(declaration);
    }
    return true;
  }

  bool VisitTypedefNameDecl(clang::TypedefNameDecl *declaration) {
    const auto *record =
        declaration->getUnderlyingType()->getAsCXXRecordDecl();
    const std::string className = typeName(record);
    const auto known = classes.find(className);
    if (known != classes.end() && known->second.opaque) {
      return true;
    }
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

  bool VisitFunctionDecl(clang::FunctionDecl *declaration) {
    if (clang::isa<clang::CXXMethodDecl>(declaration) ||
        !declaration->doesThisDeclarationHaveABody()) {
      return true;
    }
    std::string body = sourceText(declaration->getBody(), context);
    std::string interior = trim(bodyInterior(body));
    constexpr std::string_view prefix = "return ";
    if (!interior.starts_with(prefix) || !interior.ends_with(';')) {
      return true;
    }
    interior = trim(interior.substr(prefix.size(),
                                    interior.size() - prefix.size() - 1));
    static const std::regex wrapper(
        R"(^\s*([A-Za-z_]\w*)\s*(?:\.|->)\s*([A-Za-z_]\w*)\s*\(\s*\)\s*$)");
    std::smatch match;
    if (!std::regex_match(interior, match, wrapper)) {
      return true;
    }
    FreeHelper helper;
    const std::string receiver = match[1].str();
    bool parameter = false;
    for (const clang::ParmVarDecl *argument : declaration->parameters()) {
      if (argument->getNameAsString() == receiver) {
        parameter = true;
        break;
      }
    }
    if (!parameter) {
      return true;
    }
    helper.receiverParameter = receiver;
    helper.method = match[2].str();
    freeHelpers[declaration->getNameAsString()] = std::move(helper);
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
    const std::string className = typeName(parent);
    const auto known = classes.find(className);
    if (known != classes.end() && known->second.opaque) {
      return true;
    }
    if (!derivesModule(parent)) {
      return true;
    }
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
    info.name = typeName(parent);
    const bool inMainFile =
        sourceManager.isWrittenInMainFile(methodLocation);
    if (clang::isa<clang::CXXConstructorDecl>(declaration)) {
      if (!inMainFile) {
        return true;
      }
      info.constructorBody =
          makeBody(std::move(body));
      releaseCollectedMethodBuffers();
      return true;
    }
    if (clang::isa<clang::CXXDestructorDecl>(declaration)) {
      if (!inMainFile) {
        return true;
      }
      info.destructorBody =
          makeBody(std::move(body));
      releaseCollectedMethodBuffers();
      return true;
    }
    const auto methodBody =
        makeBody(std::move(body));
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

  void applyTemplateMethods(bool fillMissingOnly = false) {
    // Loaded SoC collections contain many specializations of the same primary
    // templates. Index candidates once so post-merge reconciliation is linear
    // in class count instead of scanning every class for every specialization.
    std::unordered_map<std::string, const ClassInfo *> primaryPatterns;
    std::unordered_map<std::string, const ClassInfo *> fallbackPatterns;
    for (const auto &[candidateName, candidate] : classes) {
      if (candidate.methods.empty()) {
        continue;
      }
      const size_t arguments = candidateName.find('<');
      if (arguments != std::string::npos) {
        const std::string base = candidateName.substr(0, arguments);
        if (candidate.templateBase.empty()) {
          primaryPatterns.try_emplace(base, &candidate);
        }
        fallbackPatterns.try_emplace(base, &candidate);
      }
      if (!candidate.templateBase.empty()) {
        fallbackPatterns.try_emplace(candidate.templateBase, &candidate);
      }
    }

    size_t reconciledClasses = 0;
    for (auto &[name, info] : classes) {
      if (info.opaque || info.templateBase.empty()) {
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
        const auto primary = primaryPatterns.find(info.templateBase);
        if (primary != primaryPatterns.end()) {
          pattern = primary->second;
        }
      }
      if (!pattern) {
        const auto fallback = fallbackPatterns.find(info.templateBase);
        if (fallback != fallbackPatterns.end()) {
          pattern = fallback->second;
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
          // Every collection shard performs full source-body reconciliation.
          // After shards are merged, only restore methods absent from a partial
          // specialization; rescanning large deferred bodies is unnecessary.
          if (fillMissingOnly) {
            info.methods.try_emplace(methodName, methodBody);
            continue;
          }
          bool usesTemplateParameter = false;
          for (const auto &[parameter, unused] :
               info.templateSubstitutions) {
            if (containsIdentifier(bodyText(methodBody), parameter)) {
              info.methodTemplateParameters[methodName].insert(parameter);
              usesTemplateParameter = true;
            }
          }

          // Concrete semantic bodies can expand a compact generated template
          // into megabytes for every specialization. Keep the shared source
          // pattern and preserve its parameters through generated wrappers.
          const bool hasConstexprBranch =
              bodyText(methodBody).find("if constexpr") != std::string::npos;
          if (!hasConstexprBranch || usesTemplateParameter ||
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
  std::map<std::string, FreeHelper> &freeHelpers;
  std::set<std::string> &sourceIncludes;
  std::unordered_map<std::string, uint64_t> &constantValues;
  std::string rootName;
  clang::ASTContext &context;
  std::unordered_set<std::string> collecting;
  const clang::TypedefNameDecl *rootAlias = nullptr;
  size_t collectedMethodCount = 0;
  bool reachableOnly = false;
  bool collectionOnly = false;
  std::unordered_map<const clang::CXXRecordDecl *, std::string> typeAliases;
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

std::optional<std::string> singleReturnExpression(const std::string &body) {
  std::string interior = trim(bodyInterior(body));
  constexpr std::string_view prefix = "return ";
  if (!interior.starts_with(prefix) || !interior.ends_with(';')) {
    return std::nullopt;
  }
  interior = trim(
      interior.substr(prefix.size(), interior.size() - prefix.size() - 1));
  if (interior.empty() || interior.find(';') != std::string::npos) {
    return std::nullopt;
  }
  return interior;
}

std::optional<std::string>
cacheAssignmentExpression(const std::string &body,
                          const std::string &storage) {
  const std::string marker = "return " + storage;
  const size_t returned = body.rfind(marker);
  if (returned == std::string::npos) {
    return std::nullopt;
  }
  const size_t equals = body.find('=', returned + marker.size());
  const size_t semicolon = body.find(';', returned + marker.size());
  if (equals == std::string::npos || semicolon == std::string::npos ||
      equals > semicolon) {
    return std::nullopt;
  }
  const std::string expression =
      trim(body.substr(equals + 1, semicolon - equals - 1));
  return expression.empty() ? std::nullopt
                            : std::optional<std::string>(expression);
}

std::string l1CombBody(const std::string &body, const std::string &method,
                       const std::string &storage) {
  const std::string clock = method + "_clock";
  std::vector<std::string> lines;
  std::istringstream input(bodyInterior(body));
  std::string line;
  bool skipGuardReturn = false;
  bool skipGuardClosingBrace = false;
  while (std::getline(input, line)) {
    const std::string statement = trim(line);
    if (statement.find(clock + " == _system_clock") != std::string::npos) {
      skipGuardReturn =
          statement.find("return " + storage) == std::string::npos;
      skipGuardClosingBrace = skipGuardReturn && statement.ends_with('{');
      continue;
    }
    if (skipGuardReturn && statement == "return " + storage + ";") {
      skipGuardReturn = false;
      continue;
    }
    if (skipGuardClosingBrace && statement == "}") {
      skipGuardClosingBrace = false;
      continue;
    }
    if (statement == clock + " = _system_clock;") {
      continue;
    }
    lines.push_back(line);
  }
  std::ostringstream normalized;
  normalized << "{\n";
  for (const std::string &bodyLine : lines) {
    normalized << bodyLine << '\n';
  }
  normalized << "}";
  return normalized.str();
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

std::string maskUnevaluatedOperands(const std::string &text) {
  std::string code = maskNonCode(text);
  for (size_t start = 0; start < code.size();) {
    if (!identifierStart(code[start])) {
      ++start;
      continue;
    }
    size_t end = start + 1;
    while (end < code.size() && identifierPart(code[end])) {
      ++end;
    }
    const std::string_view identifier(code.data() + start, end - start);
    const size_t operand = skipSpace(code, end);
    if ((identifier == "decltype" || identifier == "sizeof" ||
         identifier == "noexcept") &&
        operand < code.size() && code[operand] == '(') {
      if (const auto closing = matchingDelimiter(code, operand, '(', ')')) {
        std::fill(code.begin() + operand + 1, code.begin() + *closing, ' ');
        start = *closing + 1;
        continue;
      }
    }
    start = end;
  }
  return code;
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
  bool directCombExpression = false;
  int visitState = 0;
};

struct DeferredWrite {
  size_t id = 0;
  Instance *instance = nullptr;
  std::string field;
  std::vector<std::string> indexes;
};

struct IndexedAssignment {
  std::string field;
  std::vector<std::string> indexes;
  std::string value;
};

std::string nodeKey(size_t instance, NodeKind kind, const std::string &name) {
  return std::to_string(instance) + (kind == NodeKind::Port ? ":p:" : ":c:") +
         name;
}

} // namespace

struct CombsOptimizer::Impl {
  explicit Impl(std::string rootName) : rootName(std::move(rootName)) {}

  bool saveCollection(const std::string &path) const {
    CollectionWriter writer(path);
    CollectionBodyTable bodies;
    for (const auto &[unused, info] : classes) {
      for (const auto &[unusedMethod, body] : info.methods) bodies.add(body);
      bodies.add(info.constructorBody);
      bodies.add(info.destructorBody);
      bodies.add(info.assignBody);
      bodies.add(info.workBody);
      bodies.add(info.strobeBody);
    }
    writer.text("cpphdl-combs-collection-v4");
    writer.number(bodies.bodies.size());
    for (const std::string *body : bodies.bodies) {
      writer.text(*body);
    }
    writeStringSet(writer, sourceIncludes);
    writeStringUintMap(writer, constantValues);
    writer.number(classes.size());
    for (const auto &[name, info] : classes) {
      writer.text(name);
      writeClassInfo(writer, info, bodies);
    }
    if (!writer.close()) {
      llvm::errs() << "cpphdl --optimize-combs: cannot write collection "
                   << path << "\n";
      return false;
    }
    return true;
  }

  bool loadCollection(const std::string &path) {
    CollectionReader reader(path);
    if (reader.text() != "cpphdl-combs-collection-v4") {
      llvm::errs() << "cpphdl --optimize-combs: invalid collection " << path
                   << "\n";
      return false;
    }
    const uint64_t bodyCount = reader.number();
    std::vector<ClassInfo::Body> bodies;
    bodies.reserve(static_cast<size_t>(bodyCount));
    for (uint64_t index = 0; index < bodyCount && reader; ++index) {
      auto body = std::make_shared<ClassInfo::BodyData>();
      body->collectionPath = path;
      body->collectionIndex = index;
      bodies.push_back(std::move(body));
      const uint64_t size = reader.number();
      reader.skip(static_cast<size_t>(size));
    }
    std::set<std::string> loadedIncludes;
    std::unordered_map<std::string, uint64_t> loadedConstants;
    readStringSet(reader, loadedIncludes);
    readStringUintMap(reader, loadedConstants);
    const uint64_t classCount = reader.number();
    uint64_t loadedFields = 0;
    uint64_t loadedMethods = 0;
    uint64_t metadataTextBytes = 0;
    size_t largestClassBytes = 0;
    std::string largestClass;
    for (uint64_t index = 0; index < classCount && reader; ++index) {
      auto name = reader.text();
      auto info = readClassInfo(reader, bodies);
      size_t classBytes = info.name.size() + info.templateBase.size() +
                          info.header.size();
      const auto addMapBytes = [&classBytes](const auto &values) {
        for (const auto &[key, value] : values) {
          classBytes += key.size();
          if constexpr (requires { value.size(); }) {
            classBytes += value.size();
          }
        }
      };
      addMapBytes(info.templateParameterDeclarations);
      addMapBytes(info.templateSubstitutions);
      addMapBytes(info.templateTypeAccess);
      addMapBytes(info.templateParameterTypes);
      for (const auto &[fieldName, field] : info.fields) {
        classBytes += fieldName.size() + field.name.size() + field.type.size() +
                      field.childClass.size() + field.portModuleClass.size() +
                      field.initializer.size();
      }
      for (const auto &[methodName, unused] : info.methods) {
        classBytes += methodName.size();
      }
      loadedFields += info.fields.size();
      loadedMethods += info.methods.size();
      metadataTextBytes += classBytes;
      if (classBytes > largestClassBytes) {
        largestClassBytes = classBytes;
        largestClass = name;
      }
      const auto existing = classes.find(name);
      if (existing == classes.end()) {
        classes.emplace(std::move(name), std::move(info));
      } else {
        mergeClassInfo(existing->second, std::move(info));
      }
    }
    if (!reader) {
      llvm::errs() << "cpphdl --optimize-combs: cannot read collection "
                   << path << "\n";
      return false;
    }
    sourceIncludes.insert(loadedIncludes.begin(), loadedIncludes.end());
    for (auto &[name, value] : loadedConstants) {
      constantValues.insert_or_assign(std::move(name), value);
    }
    collectionBodies.insert_or_assign(path, std::move(bodies));
    if (std::getenv("CPPHDL_TRACE_COLLECTIONS")) {
      llvm::errs() << "cpphdl collection: " << path << ", " << classCount
                   << " classes, " << loadedFields << " fields, "
                   << loadedMethods << " methods, " << metadataTextBytes
                   << " metadata text bytes, largest "
                   << largestClass.substr(0, 120)
                   << " (" << largestClassBytes << " bytes)\n";
    }
    return true;
  }

  bool materializeBodies(const std::vector<ClassInfo::Body> &requested) {
    std::map<std::string, std::set<uint64_t>> needed;
    for (const auto &body : requested) {
      if (body && !body->text && !body->collectionPath.empty()) {
        needed[body->collectionPath].insert(body->collectionIndex);
      }
    }
    for (const auto &[path, indexes] : needed) {
      CollectionReader reader(path);
      if (reader.text() != "cpphdl-combs-collection-v4") {
        error = "invalid deferred collection " + path;
        return false;
      }
      const uint64_t count = reader.number();
      const auto table = collectionBodies.find(path);
      if (table == collectionBodies.end() || table->second.size() != count) {
        error = "invalid deferred body table " + path;
        return false;
      }
      for (uint64_t index = 0; index < count && reader; ++index) {
        const uint64_t size = reader.number();
        if (!indexes.contains(index)) {
          reader.skip(static_cast<size_t>(size));
          continue;
        }
        std::string text(static_cast<size_t>(size), '\0');
        reader.bytes(text.data(), text.size());
        table->second[static_cast<size_t>(index)]->text =
            std::make_shared<const std::string>(std::move(text));
      }
      if (!reader) {
        error = "cannot materialize deferred collection " + path;
        return false;
      }
    }
    return true;
  }

  void releaseBodies(const std::vector<ClassInfo::Body> &bodies) {
    for (const auto &body : bodies) {
      if (body && !body->collectionPath.empty()) {
        body->text.reset();
      }
    }
    releaseTemporaryAllocatorPages();
  }

  std::string rootName;
  // The concrete-hierarchy optimizer already schedules complete procedural
  // comb bodies. Retain the explicit L1 mode so the upstream CLI can select
  // and report that behavior without falling back to the legacy optimizer.
  bool l1Scheduling = false;
  bool collectionOnly = false;
  std::map<std::string, ClassInfo> classes;
  std::map<std::string, FreeHelper> freeHelpers;
  std::set<std::string> sourceIncludes;
  std::unordered_map<std::string, uint64_t> constantValues;
  std::map<std::string, std::vector<ClassInfo::Body>> collectionBodies;
  std::vector<std::unique_ptr<Instance>> instanceStorage;
  std::vector<Instance *> instances;
  std::vector<Node> nodes;
  std::unordered_map<std::string, size_t> nodeIds;
  std::unordered_map<std::string, std::pair<Instance *, std::string>> bindings;
  std::unordered_map<std::string, std::string> bindingMacros;
  std::unordered_map<std::string, std::string> bindingGuards;
  std::unordered_map<std::string, Instance *> modulePortBindings;
  std::vector<size_t> schedule;
  std::vector<DeferredWrite> deferredWrites;
  mutable std::vector<CombDeps> trees;
  mutable bool treesBuilt = false;
  size_t lazyCycleBackEdges = 0;
  size_t preparedNodeCount = 0;
  std::vector<size_t> *activeDemandOrder = nullptr;
  std::unordered_set<size_t> dynamicNodes;
  std::string error;
  bool mathOptimization = false;
  size_t threadCount = 1;
  size_t mathBitReversals = 0;
  size_t mathReplications = 0;
  size_t mathSignExtensions = 0;

  std::optional<IndexedAssignment>
  indexedAssignment(const std::string &statement,
                    const Instance &instance) const {
    std::string text = trim(statement);
    if (!text.empty() && text.back() == ';') {
      text.pop_back();
      text = trim(text);
    }

    size_t equals = std::string::npos;
    int parentheses = 0;
    int brackets = 0;
    int braces = 0;
    bool inString = false;
    bool inCharacter = false;
    bool escaped = false;
    for (size_t index = 0; index < text.size(); ++index) {
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
      if (value == '(') {
        ++parentheses;
      } else if (value == ')') {
        --parentheses;
      } else if (value == '[') {
        ++brackets;
      } else if (value == ']') {
        --brackets;
      } else if (value == '{') {
        ++braces;
      } else if (value == '}') {
        --braces;
      } else if (value == '=' && parentheses == 0 && brackets == 0 &&
                 braces == 0) {
        const char before = index == 0 ? '\0' : text[index - 1];
        const char after = index + 1 == text.size() ? '\0' : text[index + 1];
        if (before != '=' && before != '!' && before != '<' && before != '>' &&
            before != '+' && before != '-' && before != '*' && before != '/' &&
            before != '%' && before != '&' && before != '|' && before != '^' &&
            after != '=') {
          equals = index;
          break;
        }
      }
    }
    if (equals == std::string::npos) {
      return std::nullopt;
    }

    const std::string target = trim(text.substr(0, equals));
    size_t position = skipSpace(target, 0);
    if (position >= target.size() || !identifierStart(target[position])) {
      return std::nullopt;
    }
    const size_t fieldStart = position++;
    while (position < target.size() && identifierPart(target[position])) {
      ++position;
    }
    IndexedAssignment result;
    result.field = target.substr(fieldStart, position - fieldStart);
    const auto field = instance.type->fields.find(result.field);
    if (field == instance.type->fields.end() ||
        !field->second.indexedStorage) {
      return std::nullopt;
    }

    position = skipSpace(target, position);
    while (position < target.size() && target[position] == '[') {
      const auto closing = matchingDelimiter(target, position, '[', ']');
      if (!closing) {
        return std::nullopt;
      }
      result.indexes.push_back(
          trim(target.substr(position + 1, *closing - position - 1)));
      position = skipSpace(target, *closing + 1);
    }
    if (result.indexes.empty() || position != target.size()) {
      return std::nullopt;
    }
    result.value = trim(text.substr(equals + 1));
    if (result.value.empty()) {
      return std::nullopt;
    }
    return result;
  }

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
      return "(*s.p" + std::to_string(id) + "_pointer)";
    }
    const auto storage = node.instance->type->combStorage.find(node.name);
    return node.instance->alias + "." +
           (storage == node.instance->type->combStorage.end()
                ? node.name + "_cache"
                : storage->second);
  }

  bool portUsesOwnedStorage(const Node &node) const {
    const std::string key =
        nodeKey(node.instance->id, NodeKind::Port, node.name);
    const auto macro = bindingMacros.find(key);
    const auto guard = bindingGuards.find(key);
    const bool guarded =
        guard != bindingGuards.end() && !guard->second.empty();
    const bool rootInput = !node.instance->parent && macro == bindingMacros.end();
    const bool addressableBinding =
        macro != bindingMacros.end() &&
        (macro->second.starts_with("_ASSIGN_REG") ||
         macro->second.starts_with("_ASSIGN_COMB"));
    return guarded || (!rootInput && !addressableBinding);
  }

  std::string nodeAssignment(size_t id, const std::string &expression) const {
    const Node &node = nodes[id];
    if (node.kind == NodeKind::Port) {
      const std::string suffix = std::to_string(id);
      if (!portUsesOwnedStorage(node)) {
        // Binding expressions can be top-level comma expressions whose final
        // operand is the addressable value returned by _ASSIGN_COMB/_ASSIGN_REG.
        // Parenthesize the whole expression so addressof still gets one lvalue.
        return "s.p" + suffix + "_pointer = std::addressof((" + expression +
               "))";
      }
      return "(s.p" + suffix + "_storage = (" + expression +
             "), s.p" + suffix + "_pointer = std::addressof(s.p" + suffix +
             "_storage))";
    }
    return nodeValue(id) + " = " + expression;
  }

  bool cachesForSystemClock(const Node &node) const {
    // function_ref ports cache their returned address, and _LAZY_COMB owns an
    // equivalent clock field beside its storage. Ordinary comb methods do not:
    // repeated calls in one clock are observable and must remain repeatable.
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
                      const std::string &unavailableValue = {}) {
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
              if (child->second->type->opaque) {
                // Opaque subtrees retain their normal CppHDL ports and combs.
                // Keep the getter call so function_ref evaluates that subtree
                // on demand instead of replacing an observable value with zero.
                output += child->second->alias + "." + method + "()";
                index = *closing + 1;
                continue;
              }
              if (!unavailableValue.empty()) {
                // A dependent if-constexpr branch can legally name a method
                // absent from the concrete child type. Preserve that call in
                // the extracted template body; C++ discards the inactive
                // branch, while an active missing method remains a compile
                // error instead of silently becoming a value.
                output += context.alias + "." + identifier + "." + method +
                          "()";
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

      if (l1Scheduling && !qualifiedBefore && !memberBefore &&
          afterIdentifier < input.size() && input[afterIdentifier] == '(') {
        const auto helper = freeHelpers.find(identifier);
        const auto closing =
            matchingDelimiter(input, afterIdentifier, '(', ')');
        if (helper != freeHelpers.end() && closing) {
          const std::string argument = trim(input.substr(
              afterIdentifier + 1, *closing - afterIdentifier - 1));
          Instance *receiver = nullptr;
          if (argument == "this") {
            receiver = &context;
          } else {
            const auto receiverChild = context.children.find(argument);
            if (receiverChild != context.children.end()) {
              receiver = receiverChild->second;
            }
          }
          if (receiver) {
            const auto method =
                receiver->type->inlineMethods.find(helper->second.method);
            if (method != receiver->type->inlineMethods.end()) {
              std::string expanded =
                  rewrite(method->second, *receiver, dependencies);
              if (!error.empty()) {
                return {};
              }
              output += "(" + expanded + ")";
              index = *closing + 1;
              continue;
            }
          }
        }
      }

      if (!qualifiedBefore && !memberBefore && identifier == "this" &&
          afterIdentifier + 2 <= input.size() &&
          input.substr(afterIdentifier, 2) == "->") {
        const size_t methodStart = skipSpace(input, afterIdentifier + 2);
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
              if (auto node = callableNode(context, method)) {
                referenceNode(*node, dependencies);
                output += nodeValue(*node);
                index = *closing + 1;
                continue;
              }
            }
          }
        }
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

      // Static constexpr members lose constant-expression status when accessed
      // through a runtime instance alias. Their collected specialization value
      // is equivalent and remains valid in if constexpr and template arguments.
      const auto classConstant = context.type->constantValues.find(identifier);
      if (!qualifiedBefore && !memberBefore &&
          (!locals.contains(identifier) || forcedField) &&
          classConstant != context.type->constantValues.end()) {
        output += std::to_string(classConstant->second);
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
              if (const auto element = constantArrayElement(
                      context, elements, indices)) {
                const auto binding = modulePortBindings.find(
                    nodeKey(context.id, NodeKind::Port, *element));
                if (binding == modulePortBindings.end()) {
                  error = "missing module-array port binding " + context.path +
                          "." + *element;
                  return {};
                }
                const auto node = callableNode(*binding->second, method);
                if (!node) {
                  error = "missing method " + method +
                          " on module-array port element " + context.path +
                          "." + *element;
                  return {};
                }
                referenceNode(*node, dependencies);
                output += nodeValue(*node);
                index = *methodClosing + 1;
                continue;
              }
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
          if (context.type->methods.contains(identifier)) {
            output += context.alias + "." + identifier;
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
              if (const auto element = constantArrayElement(
                      context, elements, indices)) {
                const auto childElement = context.children.find(*element);
                if (childElement == context.children.end()) {
                  error = "missing child array element " + context.path +
                          "." + *element;
                  return {};
                }
                const auto node = callableNode(*childElement->second, method);
                if (node) {
                  referenceNode(*node, dependencies);
                  output += nodeValue(*node);
                  index = *methodClosing + 1;
                  continue;
                }
              }
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
                      unavailableValue));
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

      const auto classConstant = context.type->constantValues.find(identifier);
      if (!qualifiedBefore && !memberBefore &&
          !preservedIdentifiers.contains(identifier) &&
          classConstant != context.type->constantValues.end()) {
        output += std::to_string(classConstant->second);
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
    bool directCombExpression = false;
    if (kind == NodeKind::Comb) {
      const auto body = instance->type->combExpressions.find(name);
      const auto storage = instance->type->combStorage.find(name);
      if (body == instance->type->combExpressions.end() ||
          storage == instance->type->combStorage.end()) {
        error = "missing comb body for " + instance->path + "." + name;
        return false;
      }
      expressionText = bodyText(body->second);
      if (auto direct = singleReturnExpression(expressionText)) {
        expressionText = std::move(*direct);
        directCombExpression = true;
      }
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
    if (kind == NodeKind::Comb && !directCombExpression) {
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
    if (kind == NodeKind::Comb && !directCombExpression) {
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
    nodes[id].directCombExpression = directCombExpression;
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
    const std::string code = maskUnevaluatedOperands(input);

    for (size_t start = 0; start < code.size(); ++start) {
      if (code[start] != 'n' ||
          (start != 0 && identifierPart(code[start - 1]))) {
        continue;
      }
      size_t cursor = start + 1;
      const size_t digits = cursor;
      while (cursor < code.size() &&
             std::isdigit(static_cast<unsigned char>(code[cursor]))) {
        ++cursor;
      }
      if (cursor == digits ||
          (cursor < code.size() && identifierPart(code[cursor]))) {
        continue;
      }

      bool sawMember = false;
      while (cursor < code.size()) {
        cursor = skipSpace(code, cursor);
        if (cursor < code.size() && code[cursor] == '[') {
          const auto closing = matchingDelimiter(code, cursor, '[', ']');
          if (!closing) {
            break;
          }
          cursor = *closing + 1;
          continue;
        }

        size_t separatorWidth = 0;
        if (cursor < code.size() && code[cursor] == '.') {
          separatorWidth = 1;
        } else if (cursor + 1 < code.size() && code[cursor] == '-' &&
                   code[cursor + 1] == '>') {
          separatorWidth = 2;
        } else {
          break;
        }
        cursor = skipSpace(code, cursor + separatorWidth);
        if (cursor >= code.size() || !identifierStart(code[cursor])) {
          break;
        }
        sawMember = true;
        ++cursor;
        while (cursor < code.size() && identifierPart(code[cursor])) {
          ++cursor;
        }
        cursor = skipSpace(code, cursor);
        if (cursor < code.size() && code[cursor] == '(') {
          const auto closing = matchingDelimiter(code, cursor, '(', ')');
          if (sawMember && closing &&
              trim(code.substr(cursor + 1, *closing - cursor - 1)).empty()) {
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
    std::vector<size_t> demandSeeds;
    std::vector<size_t> workMutationSeeds;
    std::unordered_set<size_t> sccSeeds;
    std::unordered_set<size_t> conditionalSeeds;
    std::unordered_set<size_t> unresolvedSeeds;
    std::vector<std::pair<size_t, size_t>> cyclicComponents;
    std::vector<size_t> largestCyclicComponent;
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
        demandSeeds.insert(demandSeeds.end(), component.begin(),
                           component.end());
        sccSeeds.insert(component.begin(), component.end());
        cyclicComponents.emplace_back(component.size(), component.front());
        if (component.size() > largestCyclicComponent.size()) {
          largestCyclicComponent = component;
        }
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
        demandSeeds.push_back(id);
        conditionalSeeds.insert(id);
      }
      if (hasUnresolvedInstanceCall(nodes[id].expression)) {
        demandSeeds.push_back(id);
        unresolvedSeeds.insert(id);
      }
      // A field written by flattened _work is not a clock-start graph input.
      // Keep readers and their consumers on demand so their first evaluation
      // remains at the source call site after the imperative write occurs.
      if (readsWorkMutatedField(nodes[id].expression)) {
        workMutationSeeds.push_back(id);
      }
    }

    // Dependencies reached only through a conditional expression are lazy as
    // well. Include that downward cone before propagating back to consumers,
    // so no discarded branch is evaluated by the remaining eager schedule.
    std::vector<size_t> conditionalDependencies;
    std::unordered_set<size_t> conditionalDependencySet;
    for (const size_t id : demandSeeds) {
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
          demandSeeds.push_back(dependency);
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

    // Any eager consumer could otherwise trigger an on-demand dependency before
    // the call site which first reaches it in the source model. That changes
    // function_ref/_LAZY_COMB cache contents and SCC retained-value selection.
    // Keep the complete reverse cone on demand so first evaluation order stays
    // identical to the flattened source work and output demand order.
    dynamicNodes.clear();
    std::vector<size_t> pending;
    for (const size_t id : demandSeeds) {
      if (dynamicNodes.insert(id).second) {
        pending.push_back(id);
      }
    }
    for (const size_t id : workMutationSeeds) {
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
    // Large specialized designs can have one conservative seed pull a broad
    // reverse cone on demand. Keep reason counts opt-in so graph tuning can
    // distinguish real SCCs from conditional or work-order overclassification.
    if (std::getenv("CPPHDL_TRACE_COMB_GRAPH")) {
      const auto reverseConeSize = [&](const auto &seeds) {
        std::unordered_set<size_t> closure;
        std::vector<size_t> worklist;
        for (const size_t id : seeds) {
          if (closure.insert(id).second) {
            worklist.push_back(id);
          }
        }
        while (!worklist.empty()) {
          const size_t id = worklist.back();
          worklist.pop_back();
          for (const size_t consumer : consumers[id]) {
            if (closure.insert(consumer).second) {
              worklist.push_back(consumer);
            }
          }
        }
        return closure.size();
      };
      std::sort(cyclicComponents.begin(), cyclicComponents.end(),
                std::greater<>());
      llvm::errs() << "cpphdl comb graph: active=" << activeNodes.size()
                   << " scc=" << sccSeeds.size()
                   << " conditional=" << conditionalSeeds.size()
                   << " unresolved=" << unresolvedSeeds.size()
                   << " work_mutated=" << workMutationSeeds.size()
                   << " conditional_deps="
                   << conditionalDependencySet.size()
                   << " dynamic=" << dynamicNodes.size()
                   << " scc_components=" << cyclicComponents.size() << "\n";
      llvm::errs() << "cpphdl comb graph reverse cones: scc="
                   << reverseConeSize(sccSeeds)
                   << " conditional=" << reverseConeSize(conditionalDependencySet)
                   << " unresolved=" << reverseConeSize(unresolvedSeeds)
                   << " work_mutated=" << reverseConeSize(workMutationSeeds)
                   << "\n";
      for (size_t rank = 0;
           rank < std::min<size_t>(cyclicComponents.size(), 8); ++rank) {
        const auto [size, representative] = cyclicComponents[rank];
        llvm::errs() << "cpphdl comb graph scc[" << rank << "] size=" << size
                     << " representative=" << nodes[representative].instance->path
                     << "." << nodes[representative].name << "\n";
      }
      if (!largestCyclicComponent.empty()) {
        std::unordered_set<size_t> members(largestCyclicComponent.begin(),
                                           largestCyclicComponent.end());
        std::unordered_map<std::string, size_t> typeCounts;
        std::vector<std::pair<size_t, size_t>> internalDegrees;
        size_t portCount = 0;
        size_t combCount = 0;
        for (const size_t id : largestCyclicComponent) {
          ++typeCounts[nodes[id].instance->type->name];
          portCount += nodes[id].kind == NodeKind::Port;
          combCount += nodes[id].kind == NodeKind::Comb;
          const size_t degree = std::count_if(
              nodes[id].allDependencies.begin(),
              nodes[id].allDependencies.end(),
              [&](size_t dependency) { return members.contains(dependency); });
          internalDegrees.emplace_back(degree, id);
        }
        std::vector<std::pair<size_t, std::string>> rankedTypes;
        for (const auto &[type, count] : typeCounts) {
          rankedTypes.emplace_back(count, type);
        }
        std::sort(rankedTypes.begin(), rankedTypes.end(), std::greater<>());
        std::sort(internalDegrees.begin(), internalDegrees.end(),
                  std::greater<>());
        llvm::errs() << "cpphdl comb graph largest scc: ports=" << portCount
                     << " combs=" << combCount
                     << " types=" << rankedTypes.size() << "\n";
        for (size_t rank = 0;
             rank < std::min<size_t>(rankedTypes.size(), 16); ++rank) {
          llvm::errs() << "cpphdl comb graph largest type[" << rank
                       << "] count=" << rankedTypes[rank].first
                       << " name=" << rankedTypes[rank].second << "\n";
        }
        for (size_t rank = 0;
             rank < std::min<size_t>(internalDegrees.size(), 24); ++rank) {
          const auto [degree, id] = internalDegrees[rank];
          llvm::errs() << "cpphdl comb graph largest degree[" << rank
                       << "] deps=" << degree << " node="
                       << nodes[id].instance->path << "." << nodes[id].name
                       << "\n";
        }

        // A large SCC count alone does not identify the conservative edge
        // which closes the cycle. Print one shortest concrete cycle from a
        // high-degree member so graph fixes can target its actual expression.
        const size_t cycleStart = internalDegrees.front().second;
        std::vector<size_t> parent(nodes.size(), nodes.size());
        std::queue<size_t> queue;
        parent[cycleStart] = cycleStart;
        queue.push(cycleStart);
        size_t cycleEnd = nodes.size();
        while (!queue.empty() && cycleEnd == nodes.size()) {
          const size_t current = queue.front();
          queue.pop();
          for (const size_t dependency : nodes[current].allDependencies) {
            if (!members.contains(dependency)) {
              continue;
            }
            if (dependency == cycleStart && current != cycleStart) {
              cycleEnd = current;
              break;
            }
            if (parent[dependency] == nodes.size()) {
              parent[dependency] = current;
              queue.push(dependency);
            }
          }
        }
        if (cycleEnd != nodes.size()) {
          std::vector<size_t> cycle;
          for (size_t current = cycleEnd;; current = parent[current]) {
            cycle.push_back(current);
            if (current == cycleStart) {
              break;
            }
          }
          std::reverse(cycle.begin(), cycle.end());
          cycle.push_back(cycleStart);
          llvm::errs() << "cpphdl comb graph largest shortest cycle length="
                       << cycle.size() - 1 << "\n";
          for (size_t position = 0; position < cycle.size(); ++position) {
            const Node &node = nodes[cycle[position]];
            std::string expression = node.expression;
            std::replace(expression.begin(), expression.end(), '\n', ' ');
            if (expression.size() > 180) {
              expression.resize(180);
              expression += "...";
            }
            llvm::errs() << "cpphdl comb graph cycle[" << position << "] "
                         << node.instance->path << "." << node.name
                         << " expr=" << expression << "\n";
          }
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
        R"(\(\*s\.p([0-9]+)_pointer\)|n([0-9]+)\.([A-Za-z_][A-Za-z0-9_]*))");
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
                                  std::optional<size_t> self = {},
                                  const std::vector<std::string> *rawExpressions =
                                      nullptr,
                                  const std::unordered_map<size_t, size_t>
                                      *useCounts = nullptr,
                                  bool allowFusion = true) const {
    static const std::regex valuePattern(
        R"(\(\*s\.p([0-9]+)_pointer\)|n([0-9]+)\.([A-Za-z_][A-Za-z0-9_]*))");
    static const std::regex genericLambdaPattern(R"(\[[^]]*\]\s*<)");
    const std::string evaluatedCode = maskUnevaluatedOperands(input);
    const bool inputHasGenericLambda =
        std::regex_search(evaluatedCode, genericLambdaPattern);
    std::string output;
    size_t position = 0;
    for (auto iterator =
             std::sregex_iterator(input.begin(), input.end(), valuePattern);
         iterator != std::sregex_iterator(); ++iterator) {
      const auto &match = *iterator;
      output.append(input, position,
                    static_cast<size_t>(match.position()) - position);
      const auto id = valueNode(match);
      const size_t matchPosition = static_cast<size_t>(match.position());
      if (evaluatedCode[matchPosition] != ' ' && id &&
          dynamicNodes.contains(*id) && (!self || *id != *self)) {
        const bool clockCached = cachesForSystemClock(nodes[*id]);
        const std::string state = "s.evaluated" + std::to_string(*id) +
                                  " == _system_clock";
        const bool nestedGenericLambda =
            inputHasGenericLambda && rawExpressions &&
            std::regex_search(rawExpressions->at(*id), genericLambdaPattern);
        const bool fuse = allowFusion && !nestedGenericLambda &&
                          rawExpressions && useCounts &&
                          useCounts->contains(*id) && useCounts->at(*id) == 1;
        if (fuse) {
          const std::string body = replaceDynamicCalls(
              rawExpressions->at(*id), shortRoot, *id, rawExpressions,
              useCounts, false);
          const std::string evaluate =
              "([&]() { " +
              (clockCached ? "s.evaluated" + std::to_string(*id) +
                                 " = _system_clock; "
                           : std::string{}) +
              nodeAssignment(*id, body) + "; }())";
          output += clockCached
                        ? "((" + state + " ? void() : " + evaluate + "), " +
                              nodeValue(*id) + ")"
                        : "((" + evaluate + "), " + nodeValue(*id) + ")";
        } else {
          const std::string evaluate =
              shortRoot + "_optimized_comb_eval_" + std::to_string(*id) +
              "(obj, s)";
          output += clockCached
                        ? "((" + state + " ? void() : " + evaluate + "), " +
                              nodeValue(*id) + ")"
                        : "((" + evaluate + "), " + nodeValue(*id) + ")";
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
    static const std::regex portPattern(R"(\(\*s\.p([0-9]+)_pointer\))");
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
        R"(\(\*s\.p([0-9]+)_pointer\)|n([0-9]+)\.([A-Za-z_][A-Za-z0-9_]*))");
    const std::string code = maskUnevaluatedOperands(input);
    for (auto iterator =
             std::sregex_iterator(code.begin(), code.end(), valuePattern);
         iterator != std::sregex_iterator(); ++iterator) {
      if (const auto id = valueNode(*iterator)) {
        ++uses[*id];
      }
    }
  }

  std::vector<size_t>
  clusterDynamicSchedule(const std::vector<size_t> &input,
                         size_t valuesPerChunk) const {
    if (input.size() <= valuesPerChunk) {
      return input;
    }

    std::unordered_set<size_t> active(input.begin(), input.end());
    std::vector<std::unordered_map<size_t, size_t>> affinity(nodes.size());
    std::vector<size_t> rank(nodes.size(), input.size());
    for (size_t position = 0; position < input.size(); ++position) {
      rank[input[position]] = position;
      std::unordered_map<size_t, size_t> uses;
      collectNodeUses(nodes[input[position]].expression, uses);
      for (const auto &[dependency, count] : uses) {
        if (dependency == input[position] || !active.contains(dependency)) {
          continue;
        }
        affinity[input[position]][dependency] += count;
        affinity[dependency][input[position]] += count;
      }
    }

    std::vector<size_t> output;
    output.reserve(input.size());
    std::unordered_set<size_t> assigned;
    size_t nextSeed = 0;
    while (output.size() < input.size()) {
      while (nextSeed < input.size() && assigned.contains(input[nextSeed])) {
        ++nextSeed;
      }
      if (nextSeed == input.size()) {
        break;
      }

      std::unordered_map<size_t, size_t> scores;
      using Candidate = std::tuple<size_t, size_t, size_t>;
      std::priority_queue<Candidate> candidates;
      size_t chunkSize = 0;
      auto add = [&](size_t id) {
        assigned.insert(id);
        output.push_back(id);
        ++chunkSize;
        for (const auto &[neighbor, weight] : affinity[id]) {
          if (assigned.contains(neighbor)) {
            continue;
          }
          const size_t score = (scores[neighbor] += weight);
          candidates.emplace(score, input.size() - rank[neighbor], neighbor);
        }
      };
      add(input[nextSeed]);

      while (chunkSize < valuesPerChunk && output.size() < input.size()) {
        size_t selected = nodes.size();
        while (!candidates.empty()) {
          const auto [score, unusedRank, candidate] = candidates.top();
          candidates.pop();
          if (!assigned.contains(candidate) && scores[candidate] == score) {
            selected = candidate;
            break;
          }
        }
        if (selected == nodes.size()) {
          while (nextSeed < input.size() && assigned.contains(input[nextSeed])) {
            ++nextSeed;
          }
          if (nextSeed == input.size()) {
            break;
          }
          selected = input[nextSeed];
        }
        add(selected);
      }
    }
    return output;
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
      const bool demandOrdered = dynamicNodes.contains(id) ||
                                 dynamicNodes.contains(dependency);
      // An exact cached-to-cached forwarding port adds no observable boundary:
      // if the dependency was demanded earlier, the forwarding port would read
      // that same cached value on its first call. Keep all other dynamic aliases
      // separate, especially a cached port forwarding a repeatable comb method.
      if (demandOrdered &&
          !(cachesForSystemClock(node) &&
            cachesForSystemClock(nodes[dependency]))) {
        continue;
      }
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
    if (l1Scheduling) {
      std::map<std::tuple<size_t, unsigned, std::string>, size_t>
          exactExpressions;
      for (const size_t id : schedule) {
        Node &node = nodes[id];
        if (node.kind != NodeKind::Comb || !node.directCombExpression ||
            node.aliasOf || dynamicNodes.contains(id)) {
          continue;
        }
        const auto storage = node.instance->type->combStorage.find(node.name);
        if (storage == node.instance->type->combStorage.end()) {
          continue;
        }
        const auto field = node.instance->type->fields.find(storage->second);
        if (field == node.instance->type->fields.end() ||
            !field->second.packedWidth) {
          continue;
        }
        const auto key =
            std::make_tuple(node.instance->id, *field->second.packedWidth,
                            node.expression);
        const auto [existing, inserted] = exactExpressions.emplace(key, id);
        if (!inserted) {
          node.aliasOf = canonicalNode(existing->second);
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
    // A concrete class-template _work body is represented as one immediately
    // invoked generic lambda so discarded constexpr branches stay dependent.
    // Look through that generated wrapper before counting top-level statements;
    // otherwise an entire SoC appears to be one indivisible multi-megabyte unit.
    const size_t wrapperStart = skipSpace(work, 0);
    if (work.compare(wrapperStart, 5, "([&]<") == 0) {
      const size_t opening = work.find('{', wrapperStart + 5);
      if (opening != std::string::npos) {
        const auto closing = matchingDelimiter(work, opening, '{', '}');
        if (closing) {
          const std::string suffix = work.substr(*closing);
          if (trim(suffix).starts_with("}).template operator()<")) {
            const std::string interior =
                work.substr(opening + 1, *closing - opening - 1);
            const std::vector<std::string> interiorChunks =
                splitWork(interior, statementsPerChunk, bytesPerChunk);
            if (interiorChunks.size() > 1) {
              const std::string prefix = work.substr(0, opening + 1) + "\n";
              std::vector<std::string> wrappedChunks;
              wrappedChunks.reserve(interiorChunks.size());
              for (const std::string &chunk : interiorChunks) {
                wrappedChunks.push_back(prefix + chunk + suffix);
              }
              return wrappedChunks;
            }
          }
        }
      }
    }

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
    if (instance.type->opaque) {
      return indent + instance.alias + "._work(__cpphdl_reset);\n";
    }
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
                   << rewrite(loop, instance, nullptr, locals)
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
              rewrite(condition, instance, nullptr, locals);
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
                 << rewrite(loop, instance, nullptr, locals)
                 << '\n';
        }
        pendingLoops.clear();
      }
      if (auto assignment = indexedAssignment(stripped, instance)) {
        DeferredWrite write;
        write.id = deferredWrites.size();
        write.instance = &instance;
        write.field = assignment->field;
        for (const std::string &index : assignment->indexes) {
          write.indexes.push_back(
              rewrite(index, instance, nullptr, locals));
          if (!error.empty()) {
            active.erase(instance.id);
            return {};
          }
        }
        const std::string value =
            rewrite(assignment->value, instance, nullptr, locals);
        if (!error.empty()) {
          active.erase(instance.id);
          return {};
        }
        output << indent << "s.w" << write.id << "_pending = true;\n";
        for (size_t dimension = 0; dimension < write.indexes.size();
             ++dimension) {
          output << indent << "s.w" << write.id << "_index_" << dimension
                 << " = " << write.indexes[dimension] << ";\n";
        }
        output << indent << "s.w" << write.id << "_value = " << value
               << ";\n";
        deferredWrites.push_back(std::move(write));
        continue;
      }
      std::string rewritten =
          rewrite(stripped, instance, nullptr, locals);
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
             << rewrite(loop, instance, nullptr, locals)
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
    if (instance.type->opaque) {
      return indent + instance.alias + "._strobe();\n";
    }
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
    std::string stripped = trim(expression);
    for (;;) {
      bool changed = false;
      if (!stripped.empty() && stripped.front() == '(') {
        if (const auto closing = matchingDelimiter(stripped, 0, '(', ')');
            closing && *closing == stripped.size() - 1) {
          stripped = trim(stripped.substr(1, stripped.size() - 2));
          changed = true;
          continue;
        }
        if (const auto closing = matchingDelimiter(stripped, 0, '(', ')');
            closing && *closing + 1 < stripped.size()) {
          const std::string castType =
              trim(stripped.substr(1, *closing - 1));
          static const std::regex typeName(
              R"(^[A-Za-z_][A-Za-z0-9_:]*(?:\s+[A-Za-z_][A-Za-z0-9_:]*)*(?:\s*[*&]+\s*)?$)");
          if (std::regex_match(castType, typeName)) {
            stripped = trim(stripped.substr(*closing + 1));
            changed = true;
            continue;
          }
        }
      }
      static const std::regex staticCast(
          R"(^static_cast\s*<[^<>]+>\s*\((.*)\)$)");
      std::smatch match;
      if (std::regex_match(stripped, match, staticCast)) {
        stripped = trim(match[1].str());
        changed = true;
        continue;
      }
      if (!changed) {
        break;
      }
    }

    static const std::regex integerLiteral(
        R"(^(?:0[xX][0-9A-Fa-f]+|0[bB][01]+|0[0-7]*|[1-9][0-9]*)(?:[uUlL]*)$)");
    if (std::regex_match(stripped, integerLiteral)) {
      std::string literal = stripped;
      while (!literal.empty() &&
             (literal.back() == 'u' || literal.back() == 'U' ||
              literal.back() == 'l' || literal.back() == 'L')) {
        literal.pop_back();
      }
      if (literal.starts_with("0b") || literal.starts_with("0B")) {
        return std::stoull(literal.substr(2), nullptr, 2);
      }
      return std::stoull(literal, nullptr, 0);
    }
    if (const auto value = instance.type->constantValues.find(stripped);
        value != instance.type->constantValues.end()) {
      return value->second;
    }
    if (const auto value = constantValues.find(stripped);
        value != constantValues.end()) {
      return value->second;
    }
    return std::nullopt;
  }

  std::optional<std::string> constantArrayElement(
      Instance &instance, const std::vector<std::string> &elements,
      const std::vector<std::string> &expressions) const {
    std::vector<size_t> indices;
    indices.reserve(expressions.size());
    for (const std::string &expression : expressions) {
      const auto index = constantIndex(instance, expression);
      if (!index) {
        return std::nullopt;
      }
      indices.push_back(*index);
    }
    for (const std::string &element : elements) {
      if (childElementIndices(element) == indices) {
        return element;
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
        // Opaque subtrees retain the bindings established by the root's
        // one-time _assign() call. Their ports are intentionally outside the
        // flattened dependency graph.
        if (target->type->opaque) {
          continue;
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
        bindingMacros[key] = assignment.macro;
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
        if (l1Scheduling && methodName.starts_with("_optimized_")) {
          std::string interior = trim(bodyInterior(bodyText(body)));
          constexpr std::string_view prefix = "return ";
          if (interior.starts_with(prefix) && interior.ends_with(';')) {
            interior = trim(interior.substr(
                prefix.size(), interior.size() - prefix.size() - 1));
            if (!interior.empty() && interior.find(';') == std::string::npos) {
              info.inlineMethods[methodName] = std::move(interior);
            }
          }
        }
        if (auto storage = combStorage(bodyText(body));
            storage && info.fields.contains(*storage)) {
          const std::string &sourceBody = bodyText(body);
          bool replaced = false;
          if (mathOptimization && *storage == methodName + "_cache") {
            const std::string procedural =
                proceduralCombBody(methodName, sourceBody);
            const auto width = info.fields.at(*storage).packedWidth;
            const auto replacement =
                width ? MathCombRewriter::rewrite(methodName, procedural,
                                                   *width)
                      : std::nullopt;
            if (std::getenv("CPPHDL_TRACE_MATH")) {
              llvm::errs() << "cpphdl math: " << className << "::"
                           << methodName << ", width="
                           << (width ? std::to_string(*width) : "unknown")
                           << ", replacement="
                           << (replacement ? replacement->kind : "none")
                           << "\n";
              if (!replacement) {
                llvm::errs() << procedural << "\n";
              }
            }
            if (replacement) {
              info.combExpressions[methodName] =
                  makeBody("{ return (" + replacement->expression + "); }");
              replaced = true;
              if (std::string_view(replacement->kind) == "bit-reversal") {
                ++mathBitReversals;
              } else if (std::string_view(replacement->kind) ==
                         "sign-extension") {
                ++mathSignExtensions;
              } else {
                ++mathReplications;
              }
            }
          }
          if (!replaced && l1Scheduling &&
              *storage == methodName + "_cache" &&
              info.fields.contains(methodName + "_clock")) {
            if (auto expression =
                    cacheAssignmentExpression(sourceBody, *storage)) {
              info.combExpressions[methodName] =
                  makeBody("{ return (" + *expression + "); }");
            } else {
              info.combExpressions[methodName] =
                  makeBody(l1CombBody(sourceBody, methodName, *storage));
            }
          } else if (!replaced) {
            info.combExpressions[methodName] = body;
          }
          info.combStorage[methodName] = *storage;
        }
      }
    }
  }

  std::string proceduralCombBody(const std::string &methodName,
                                 const std::string &body) const {
    std::vector<std::string> lines;
    std::istringstream input(bodyInterior(body));
    std::string line;
    bool skipGuardReturn = false;
    bool skipGuardClosingBrace = false;
    while (std::getline(input, line)) {
      const std::string statement = trim(line);
      const std::string cache = methodName + "_cache";
      const std::string clock = methodName + "_clock";
      if (statement.find(clock + " == _system_clock") != std::string::npos) {
        skipGuardReturn =
            statement.find("return " + cache) == std::string::npos;
        skipGuardClosingBrace = skipGuardReturn && statement.ends_with('{');
        continue;
      }
      if (skipGuardReturn && statement == "return " + cache + ";") {
        skipGuardReturn = false;
        continue;
      }
      if (skipGuardClosingBrace && statement == "}") {
        skipGuardClosingBrace = false;
        continue;
      }
      if (statement == clock + " = _system_clock;") {
        continue;
      }
      lines.push_back(line);
    }
    while (!lines.empty() && trim(lines.back()).empty()) {
      lines.pop_back();
    }
    if (!lines.empty() &&
        trim(lines.back()) == "return " + methodName + "_cache;") {
      lines.pop_back();
    }
    std::ostringstream result;
    for (const std::string &bodyLine : lines) {
      result << bodyLine << '\n';
    }
    return result.str();
  }

  bool emit(const std::string &rootName, const std::string &directory) {
    lazyCycleBackEdges = 0;
    preparedNodeCount = 0;
    deferredWrites.clear();
    mathBitReversals = 0;
    mathReplications = 0;
    mathSignExtensions = 0;
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

    const auto lifecycleBodies = [this](auto selector) {
      std::vector<ClassInfo::Body> bodies;
      std::set<const ClassInfo *> seen;
      for (const Instance *instance : instances) {
        if (seen.insert(instance->type).second) {
          selector(*instance->type, bodies);
        }
      }
      return bodies;
    };
    const auto assignBodies = lifecycleBodies(
        [](const ClassInfo &info, std::vector<ClassInfo::Body> &bodies) {
          if (info.assignBody) bodies.push_back(info.assignBody);
        });
    const auto combBodies = lifecycleBodies(
        [](const ClassInfo &info, std::vector<ClassInfo::Body> &bodies) {
          for (const auto &[name, body] : info.methods) {
            if (name != "_assign" && name != "_work" &&
                name != "_strobe" && body) {
              bodies.push_back(body);
            }
          }
        });
    const auto workBodies = lifecycleBodies(
        [](const ClassInfo &info, std::vector<ClassInfo::Body> &bodies) {
          if (info.workBody) bodies.push_back(info.workBody);
        });
    const auto strobeBodies = lifecycleBodies(
        [](const ClassInfo &info, std::vector<ClassInfo::Body> &bodies) {
          if (info.strobeBody) bodies.push_back(info.strobeBody);
        });

    if (!materializeBodies(assignBodies)) {
      return false;
    }
    if (!buildBindings()) {
      return false;
    }
    // Binding expansion creates short-lived copies of generated template
    // expressions across the concrete hierarchy. Return those pages before
    // lifecycle flattening starts its independent string-heavy phase.
    releaseBodies(assignBodies);
    releaseTemporaryAllocatorPages();
    tracePhase("port binding expansion");

    if (!materializeBodies(combBodies)) {
      return false;
    }
    findCombExpressions();
    tracePhase("comb discovery");

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
    if (!materializeBodies(workBodies)) {
      return false;
    }
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
    std::unordered_set<const ClassInfo *> externalTypes;
    for (const Instance *instance : instances) {
      if (bodyText(instance->type->workBody)
              .find("firtool_cpphdl_external::work") != std::string::npos) {
        externalTypes.insert(instance->type);
      }
    }
    releaseBodies(workBodies);
    releaseTemporaryAllocatorPages();
    tracePhase("work flattening");
    std::unordered_set<size_t> activeStrobe;
    if (!materializeBodies(strobeBodies)) {
      return false;
    }
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
    releaseBodies(strobeBodies);

    // External models still consume their ports through the generated
    // function_ref API.  Make those bindings graph roots so their lambdas can
    // read already-scheduled caches instead of calling legacy comb methods.
    for (Instance *instance : instances) {
      if (!externalTypes.contains(instance->type)) {
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
    // Both dynamic and eager consumers can read an on-demand value. Rewrite
    // every prepared expression so a mixed graph invokes that evaluator at
    // the exact read site instead of observing uninitialized retained storage.
    std::vector<std::string> rawDynamicExpressions(nodes.size());
    std::unordered_map<size_t, size_t> dynamicUseCounts;
    for (size_t id = 0; id < nodes.size(); ++id) {
      if (!nodes[id].expressionReady) {
        continue;
      }
      rawDynamicExpressions[id] = nodes[id].expression;
    }
    for (const size_t id : schedule) {
      std::unordered_map<size_t, size_t> uses;
      collectNodeUses(nodes[id].expression, uses);
      for (const auto &[dependency, count] : uses) {
        if (dependency != id && dynamicNodes.contains(dependency)) {
          dynamicUseCounts[dependency] += count;
        }
      }
    }
    {
      std::unordered_map<size_t, size_t> uses;
      collectNodeUses(work, uses);
      for (const auto &[dependency, count] : uses) {
        if (dynamicNodes.contains(dependency)) {
          dynamicUseCounts[dependency] += count;
        }
      }
    }
    for (size_t id = 0; id < nodes.size(); ++id) {
      if (nodes[id].expressionReady) {
        nodes[id].expression = replaceDynamicCalls(
            rawDynamicExpressions[id], shortRoot, id, &rawDynamicExpressions,
            &dynamicUseCounts);
      }
    }
    work = replaceDynamicCalls(work, shortRoot, {}, &rawDynamicExpressions,
                               &dynamicUseCounts);
    const std::regex evaluatorCallPattern(shortRoot +
                                          R"(_optimized_comb_eval_([0-9]+))");
    std::unordered_set<size_t> reachableEvaluators;
    std::vector<size_t> pendingEvaluators;
    const auto addEvaluatorCalls = [&](const std::string &text) {
      for (auto iterator = std::sregex_iterator(text.begin(), text.end(),
                                                evaluatorCallPattern);
           iterator != std::sregex_iterator(); ++iterator) {
        const size_t id = std::stoull((*iterator)[1].str());
        if (id < nodes.size() && reachableEvaluators.insert(id).second) {
          pendingEvaluators.push_back(id);
        }
      }
    };
    addEvaluatorCalls(work);
    for (const size_t id : schedule) {
      if (!dynamicNodes.contains(id)) {
        addEvaluatorCalls(nodes[id].expression);
      }
    }
    for (const auto &[name, field] : root->type->fields) {
      if (field.kind != FieldKind::Port || !field.portModuleClass.empty() ||
          !bindings.contains(nodeKey(root->id, NodeKind::Port, name))) {
        continue;
      }
      const auto node = nodeIds.find(nodeKey(root->id, NodeKind::Port, name));
      if (node == nodeIds.end()) {
        continue;
      }
      const size_t id = canonicalNode(node->second);
      if (dynamicNodes.contains(id) && reachableEvaluators.insert(id).second) {
        pendingEvaluators.push_back(id);
      }
    }
    for (size_t position = 0; position < pendingEvaluators.size(); ++position) {
      addEvaluatorCalls(nodes[pendingEvaluators[position]].expression);
    }
    std::vector<size_t> dynamicStateSchedule;
    std::vector<size_t> dynamicSchedule;
    dynamicStateSchedule.reserve(dynamicNodes.size());
    dynamicSchedule.reserve(dynamicNodes.size());
    for (const size_t id : schedule) {
      if (dynamicNodes.contains(id)) {
        dynamicStateSchedule.push_back(id);
        // The dependency DFS keeps related evaluators adjacent. Preserve that
        // order when partitioning sources so direct calls are visible to one
        // compiler invocation instead of crossing arbitrary node-id chunks.
        if (reachableEvaluators.contains(id)) {
          dynamicSchedule.push_back(id);
        }
      }
    }
    schedule.erase(
        std::remove_if(schedule.begin(), schedule.end(),
                       [this](size_t id) { return dynamicNodes.contains(id); }),
        schedule.end());
    const size_t clockCachedDynamicStates = std::count_if(
        dynamicStateSchedule.begin(), dynamicStateSchedule.end(),
        [this](size_t id) { return cachesForSystemClock(nodes[id]); });

    // Evaluator definitions stay in source partitions. Duplicating even small
    // definitions in the common internal header makes every partition parse
    // and optimize the same bodies, while CVA6 profiling showed no runtime
    // benefit over hidden direct calls.
    std::unordered_set<size_t> inlineDynamicEvaluators;

    struct RootOutputPort {
      std::string name;
      size_t value = 0;
    };
    std::vector<RootOutputPort> rootOutputPorts;
    for (const auto &[name, field] : root->type->fields) {
      if (field.kind != FieldKind::Port || !field.portModuleClass.empty() ||
          !bindings.contains(nodeKey(root->id, NodeKind::Port, name))) {
        continue;
      }
      const auto node = nodeIds.find(nodeKey(root->id, NodeKind::Port, name));
      if (node == nodeIds.end()) {
        error = "missing optimized root output port node " + root->path +
                "." + name;
        return false;
      }
      rootOutputPorts.push_back({name, canonicalNode(node->second)});
    }
    releaseTemporaryAllocatorPages();
    tracePhase("graph simplification");
    releaseBodies(combBodies);

    struct BindingStatement {
      std::string text;
      std::set<size_t> usedInstances;
    };
    std::vector<BindingStatement> bindingStatements;
    if (!materializeBodies(assignBodies)) {
      return false;
    }
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
        if (!externalTypes.contains(target->type)) {
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
    releaseBodies(assignBodies);

    // Dependency diagnostics duplicate long specialized type names. Build
    // that optional public view only if dependencyTrees() is requested; the
    // normal generator needs the compact node graph but not this debug copy.
    trees.clear();
    treesBuilt = false;
    tracePhase("dependency metadata deferred");

    std::filesystem::create_directories(directory);
    struct PreviousOutput {
      uint64_t hash = 0;
      uintmax_t size = 0;
      std::filesystem::file_time_type modified;
    };
    const auto fingerprint = [](const std::filesystem::path &path) {
      uint64_t hash = 1469598103934665603ull;
      std::ifstream input(path, std::ios::binary);
      std::array<char, 64 * 1024> buffer{};
      while (input) {
        input.read(buffer.data(), buffer.size());
        for (std::streamsize index = 0; index < input.gcount(); ++index) {
          hash ^= static_cast<unsigned char>(buffer[index]);
          hash *= 1099511628211ull;
        }
      }
      return hash;
    };
    std::map<std::string, PreviousOutput> previousOutputs;
    const std::string outputPrefix = shortRoot + "_optimized_combs";
    // Optimizer-only fixes often change one generated partition. Remember the
    // old content identity so byte-identical files retain their timestamps and
    // incremental builds do not re-optimize the entire graph unnecessarily.
    for (const auto &entry : std::filesystem::directory_iterator(directory)) {
      const std::string name = entry.path().filename().string();
      if (entry.is_regular_file() && name.starts_with(outputPrefix) &&
          (entry.path().extension() == ".cpp" ||
           entry.path().extension() == ".h")) {
        previousOutputs.emplace(
            name, PreviousOutput{fingerprint(entry.path()),
                                 std::filesystem::file_size(entry.path()),
                                 std::filesystem::last_write_time(entry.path())});
      }
    }
    const std::regex staleChunkPattern(
        "^" + shortRoot +
        R"(_optimized_combs_(?:(?:work|bind|strobe|memory|model|dynamic|prefix)_[0-9]+|thread_[0-9]+_[0-9]+(?:_[0-9]+)?|[0-9]+)\.cpp$)");
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
    header << "#pragma once\n";
    if (root->type->typeAlias) {
      header << "#include \"" << rootHeader << "\"\n";
    } else {
      header << "\nclass " << shortRoot << ";\n";
    }
    header << "\nvoid bind_optimized_ports(" << shortRoot << "& obj);\n"
           << "extern \"C\" void cpphdl_optimized_bind_ports_abi(void* obj);\n"
           << "void calc_all(" << shortRoot
           << "& obj, bool reset = false);\n"
           << "void commit_optimized_regs(" << shortRoot << "& obj);\n";
    if (!finishOutput(header, headerPath)) {
      return false;
    }

    std::set<std::string> headers;
    std::set<std::string> headerNames;
    for (const std::string &include : sourceIncludes) {
      if (include.ends_with(".h")) {
        headers.insert(include);
        headerNames.insert(
            std::filesystem::path(include).filename().string());
      }
    }
    for (const auto &[name, info] : classes) {
      if (!info.header.empty() && info.header.ends_with(".h")) {
        headers.insert(info.header);
        headerNames.insert(
            std::filesystem::path(info.header).filename().string());
      }
    }

    constexpr size_t valuesPerChunk = 400;
    size_t dynamicValuesPerChunk = 200;
    size_t dynamicBytesPerChunk = 2 * 1024 * 1024;
    // Dynamic graphs can range from a few nodes to whole SoCs. Let build flows
    // trade translation-unit count against compiler memory without changing the
    // graph or generated runtime behavior.
    if (const char *configured =
            std::getenv("CPPHDL_OPTIMIZE_COMBS_DYNAMIC_VALUES_PER_CHUNK")) {
      try {
        dynamicValuesPerChunk = std::stoull(configured);
      } catch (const std::exception &) {
        dynamicValuesPerChunk = 0;
      }
      if (dynamicValuesPerChunk == 0) {
        error =
            "invalid CPPHDL_OPTIMIZE_COMBS_DYNAMIC_VALUES_PER_CHUNK=" +
            std::string(configured);
        return false;
      }
    }
    if (const char *configured =
            std::getenv("CPPHDL_OPTIMIZE_COMBS_DYNAMIC_BYTES_PER_CHUNK")) {
      try {
        dynamicBytesPerChunk = std::stoull(configured);
      } catch (const std::exception &) {
        dynamicBytesPerChunk = 0;
      }
      if (dynamicBytesPerChunk == 0) {
        error =
            "invalid CPPHDL_OPTIMIZE_COMBS_DYNAMIC_BYTES_PER_CHUNK=" +
            std::string(configured);
        return false;
      }
    }
    dynamicSchedule =
        clusterDynamicSchedule(dynamicSchedule, dynamicValuesPerChunk);
    constexpr size_t bindingsPerChunk = 400;
    constexpr size_t modelTypesPerChunk = 50;
    constexpr size_t memoryWritesPerChunk = 400;

    struct CombComponent {
      std::vector<size_t> values;
      uint64_t weight = 0;
    };
    struct CombStage {
      size_t begin = 0;
      size_t end = 0;
      size_t componentCount = 0;
      std::vector<std::vector<size_t>> lanes;
      std::vector<uint64_t> laneWeights;
    };
    struct CombPartition {
      uint64_t estimatedCost = 0;
      size_t componentCount = 0;
      std::vector<CombStage> stages;
    };

    std::vector<uint64_t> nodeWeights(nodes.size(), 1);
    std::vector<uint64_t> cumulativeWeights(schedule.size() + 1, 0);
    for (size_t position = 0; position < schedule.size(); ++position) {
      const size_t id = schedule[position];
      nodeWeights[id] = std::max<uint64_t>(1, nodes[id].expression.size());
      cumulativeWeights[position + 1] =
          cumulativeWeights[position] + nodeWeights[id];
    }

    const size_t requestedThreads =
        std::min(threadCount, std::max<size_t>(1, schedule.size()));
    const auto buildStage = [&](size_t begin, size_t end) {
      CombStage result;
      result.begin = begin;
      result.end = end;
      std::vector<bool> member(nodes.size(), false);
      std::vector<size_t> componentParent(nodes.size());
      for (size_t position = begin; position < end; ++position) {
        const size_t id = schedule[position];
        member[id] = true;
        componentParent[id] = id;
      }
      const auto findComponent = [&](size_t id, const auto &self) -> size_t {
        if (componentParent[id] != id) {
          componentParent[id] = self(componentParent[id], self);
        }
        return componentParent[id];
      };
      const auto mergeComponents = [&](size_t left, size_t right) {
        left = findComponent(left, findComponent);
        right = findComponent(right, findComponent);
        if (left != right) {
          componentParent[right] = left;
        }
      };
      for (size_t position = begin; position < end; ++position) {
        const size_t id = schedule[position];
        for (const size_t dependency : nodes[id].dependencies) {
          if (dependency < member.size() && member[dependency]) {
            mergeComponents(id, dependency);
          }
        }
      }

      std::vector<CombComponent> components;
      std::unordered_map<size_t, size_t> componentIds;
      for (size_t position = begin; position < end; ++position) {
        const size_t id = schedule[position];
        const size_t root = findComponent(id, findComponent);
        const auto [componentPosition, inserted] =
            componentIds.emplace(root, components.size());
        if (inserted) {
          components.emplace_back();
        }
        CombComponent &component = components[componentPosition->second];
        component.values.push_back(id);
        component.weight += nodeWeights[id];
      }
      result.componentCount = components.size();

      result.lanes.resize(requestedThreads);
      result.laneWeights.assign(requestedThreads, 0);
      std::vector<size_t> componentOrder(components.size());
      std::iota(componentOrder.begin(), componentOrder.end(), 0);
      std::stable_sort(componentOrder.begin(), componentOrder.end(),
                       [&](size_t left, size_t right) {
                         return components[left].weight > components[right].weight;
                       });
      for (const size_t componentId : componentOrder) {
        const size_t lane = static_cast<size_t>(std::distance(
            result.laneWeights.begin(),
            std::min_element(result.laneWeights.begin(),
                             result.laneWeights.end())));
        CombComponent &component = components[componentId];
        result.lanes[lane].insert(result.lanes[lane].end(),
                                  component.values.begin(),
                                  component.values.end());
        result.laneWeights[lane] += component.weight;
      }
      return result;
    };

    const auto buildPartition = [&](size_t stageCount) {
      CombPartition result;
      size_t begin = 0;
      for (size_t stage = 0; stage < stageCount; ++stage) {
        size_t end = schedule.size();
        if (stage + 1 != stageCount) {
          const uint64_t target =
              cumulativeWeights.back() * (stage + 1) / stageCount;
          const auto first = cumulativeWeights.begin() + begin + 1;
          const auto last = cumulativeWeights.end() - (stageCount - stage - 1);
          end = static_cast<size_t>(std::distance(
              cumulativeWeights.begin(), std::lower_bound(first, last, target)));
        }
        CombStage combStage = buildStage(begin, end);
        result.componentCount += combStage.componentCount;
        result.estimatedCost += *std::max_element(
            combStage.laneWeights.begin(), combStage.laneWeights.end());
        result.stages.push_back(std::move(combStage));
        begin = end;
      }
      return result;
    };

    CombPartition partition = buildPartition(1);
    if (requestedThreads > 1 && schedule.size() > requestedThreads) {
      const size_t maximumStages = std::min<size_t>(8, schedule.size());
      // A barrier executes once per simulated cycle.  On the large generated
      // models this costs more than another complete comb traversal, so only
      // accept an additional stage when it removes more than one traversal of
      // critical work.  This normally preserves a single dispatch/join while
      // retaining staged scheduling for unusually parallel, expensive DAGs.
      const uint64_t barrierPenalty = cumulativeWeights.back();
      uint64_t bestScore = partition.estimatedCost;
      for (size_t stageCount = 2; stageCount <= maximumStages; ++stageCount) {
        CombPartition candidate = buildPartition(stageCount);
        const uint64_t score = candidate.estimatedCost +
                               barrierPenalty * (stageCount - 1);
        if (score < bestScore) {
          bestScore = score;
          partition = std::move(candidate);
        }
      }
    }

    const std::vector<size_t> prefixValues;
    const uint64_t prefixWeight = 0;
    const size_t componentCount = partition.componentCount;
    const uint64_t estimatedParallelWeight = partition.estimatedCost;
    std::vector<CombStage> combStages = std::move(partition.stages);
    const size_t combThreadCount = requestedThreads;
    const bool threadedCombs = combThreadCount > 1;

    std::vector<std::vector<size_t>> prefixChunks;
    for (size_t begin = 0; begin < prefixValues.size(); begin += valuesPerChunk) {
      const size_t end = std::min(prefixValues.size(), begin + valuesPerChunk);
      prefixChunks.emplace_back(prefixValues.begin() + begin,
                                prefixValues.begin() + end);
    }
    std::vector<std::vector<std::vector<std::vector<size_t>>>> stageLaneChunks(
        combStages.size());
    size_t threadChunkCount = 0;
    if (threadedCombs) {
      for (size_t stage = 0; stage < combStages.size(); ++stage) {
        stageLaneChunks[stage].resize(combThreadCount);
        for (size_t lane = 0; lane < combThreadCount; ++lane) {
          const auto &values = combStages[stage].lanes[lane];
          for (size_t begin = 0; begin < values.size();
               begin += valuesPerChunk) {
            const size_t end =
                std::min(values.size(), begin + valuesPerChunk);
            stageLaneChunks[stage][lane].emplace_back(values.begin() + begin,
                                                      values.begin() + end);
            ++threadChunkCount;
          }
        }
      }
    }
    const size_t chunkCount =
        threadedCombs
            ? 0
            : (schedule.size() + valuesPerChunk - 1) / valuesPerChunk;
    std::vector<std::pair<size_t, size_t>> dynamicChunks;
    for (size_t begin = 0; begin < dynamicSchedule.size();) {
      size_t end = begin;
      size_t estimatedBytes = 0;
      while (end < dynamicSchedule.size() &&
             end - begin < dynamicValuesPerChunk) {
        const size_t nodeBytes = nodes[dynamicSchedule[end]].expression.size() +
                                 512;
        if (end != begin && estimatedBytes + nodeBytes > dynamicBytesPerChunk) {
          break;
        }
        estimatedBytes += nodeBytes;
        ++end;
      }
      dynamicChunks.emplace_back(begin, end);
      begin = end;
    }
    const size_t dynamicChunkCount = dynamicChunks.size();
    const size_t bindChunkCount =
        (bindingStatements.size() + bindingsPerChunk - 1) / bindingsPerChunk;
    const std::vector<std::string> workChunks = splitWork(work, 400);
    const std::vector<std::string> strobeChunks = splitWork(strobe, 400);
    const size_t memoryChunkCount =
        (deferredWrites.size() + memoryWritesPerChunk - 1) /
        memoryWritesPerChunk;
    const auto modelBodies = lifecycleBodies(
        [](const ClassInfo &info, std::vector<ClassInfo::Body> &bodies) {
          if (info.constructorBody) bodies.push_back(info.constructorBody);
          if (info.destructorBody) bodies.push_back(info.destructorBody);
        });
    if (!materializeBodies(modelBodies)) {
      return false;
    }
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
    internal << "#pragma once\n#include <atomic>\n#include <cstddef>\n#include <cstdint>\n#include <limits>\n#include <memory>\n#include <thread>\n#include "
                "<type_traits>\n#include <unordered_map>\n#include <utility>\n";
    // Load CppHDL and its standard-library dependencies before changing
    // access control. The root and concrete model headers are then included
    // under the access shim; including seed headers first would lock their
    // private declarations behind include guards before the shim is active.
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
    internal << "#undef private\n\n";
    if (mathOptimization && mathBitReversals != 0) {
      internal << R"CPP(namespace cpphdl_optimized_math {
inline std::uint32_t bit_reverse32(std::uint32_t value) {
#if defined(__clang__) && __has_builtin(__builtin_bitreverse32)
  return __builtin_bitreverse32(value);
#else
  value = ((value >> 1) & 0x55555555u) | ((value & 0x55555555u) << 1);
  value = ((value >> 2) & 0x33333333u) | ((value & 0x33333333u) << 2);
  value = ((value >> 4) & 0x0f0f0f0fu) | ((value & 0x0f0f0f0fu) << 4);
  value = ((value >> 8) & 0x00ff00ffu) | ((value & 0x00ff00ffu) << 8);
  return (value >> 16) | (value << 16);
#endif
}

inline std::uint64_t bit_reverse64(std::uint64_t value) {
#if defined(__clang__) && __has_builtin(__builtin_bitreverse64)
  return __builtin_bitreverse64(value);
#else
  value = ((value >> 1) & 0x5555555555555555ull) |
          ((value & 0x5555555555555555ull) << 1);
  value = ((value >> 2) & 0x3333333333333333ull) |
          ((value & 0x3333333333333333ull) << 2);
  value = ((value >> 4) & 0x0f0f0f0f0f0f0f0full) |
          ((value & 0x0f0f0f0f0f0f0f0full) << 4);
  value = ((value >> 8) & 0x00ff00ff00ff00ffull) |
          ((value & 0x00ff00ff00ff00ffull) << 8);
  value = ((value >> 16) & 0x0000ffff0000ffffull) |
          ((value & 0x0000ffff0000ffffull) << 16);
  return (value >> 32) | (value << 32);
#endif
}
} // namespace cpphdl_optimized_math

)CPP";
    }
    if (threadedCombs) {
      internal << R"CPP(inline void cpphdl_optimized_thread_pause() {
#if defined(__i386__) || defined(__x86_64__)
  __builtin_ia32_pause();
#elif defined(__aarch64__)
  asm volatile("yield");
#else
  std::this_thread::yield();
#endif
}

)CPP";
    }
    internal << "struct " << shortRoot << "_optimized_combs_state {\n";
    internal << "  long evaluated_system_clock = "
                "std::numeric_limits<long>::min();\n";
    std::unordered_set<size_t> stateNodes(schedule.begin(), schedule.end());
    stateNodes.insert(dynamicStateSchedule.begin(), dynamicStateSchedule.end());
    for (size_t id = 0; id < nodes.size(); ++id) {
      if (!stateNodes.contains(id)) {
        continue;
      }
      const Node &node = nodes[id];
      if (node.kind != NodeKind::Port) {
        continue;
      }
      const std::string type =
          "std::remove_reference_t<decltype(" +
          instanceExpression(*node.instance,
                             "std::declval<" + shortRoot + "&>()") +
          "." + node.name + "())>";
      if (portUsesOwnedStorage(node)) {
        internal << "  " << type << " p" << id << "_storage;\n"
                 << "  " << type << "* p" << id
                 << "_pointer = std::addressof(p" << id << "_storage);\n";
      } else {
        internal << "  " << type << "* p" << id << "_pointer{};\n";
      }
    }
    // Ports and _LAZY_COMB are the source-language recursion boundaries. Their
    // timestamps expose retained storage at a recursive edge. Ordinary comb
    // methods remain repeatable and need no optimizer-owned state.
    for (const size_t id : dynamicStateSchedule) {
      if (cachesForSystemClock(nodes[id])) {
        internal << "  long evaluated" << id
                 << " = std::numeric_limits<long>::min();\n";
      }
    }
    for (const DeferredWrite &write : deferredWrites) {
      internal << "  bool w" << write.id << "_pending{};\n";
      for (size_t dimension = 0; dimension < write.indexes.size();
           ++dimension) {
        internal << "  std::size_t w" << write.id << "_index_" << dimension
                 << "{};\n";
      }
      internal << "  cpphdl::value_type_for_ref_t<decltype("
               << instanceExpression(
                      *write.instance,
                      "std::declval<" + shortRoot + "&>()")
               << "." << write.field;
      for (size_t dimension = 0; dimension < write.indexes.size();
           ++dimension) {
        internal << "[0]";
      }
      internal << ")> w" << write.id << "_value{};\n";
    }
    internal << "};\n\n";
    for (const size_t id : dynamicSchedule) {
      if (inlineDynamicEvaluators.contains(id)) {
        internal << "[[gnu::always_inline]] inline void ";
      } else {
        // Evaluators can be called from another generated partition, but they
        // are private to this specialized executable. Hidden visibility prevents
        // ELF interposition from blocking same-TU inlining at -O2.
        internal << "[[gnu::visibility(\"hidden\")]] void ";
      }
      internal << shortRoot << "_optimized_comb_eval_" << id
               << "(" << shortRoot << "&, "
               << shortRoot << "_optimized_combs_state&);\n";
    }
    internal << '\n';
    for (const size_t id : dynamicSchedule) {
      if (!inlineDynamicEvaluators.contains(id)) {
        continue;
      }
      const Node &node = nodes[id];
      const std::set<size_t> usedInstances =
          referencedInstances(node.expression, {0, node.instance->id});
      internal << "[[gnu::always_inline]] inline void " << shortRoot
               << "_optimized_comb_eval_" << id << "(" << shortRoot
               << "& obj, " << shortRoot
               << "_optimized_combs_state& s) {\n";
      emitAliases(internal, usedInstances);
      internal << "  " << nodeAssignment(id, node.expression)
               << ";\n}\n\n";
    }
    for (size_t chunk = 0; chunk < prefixChunks.size(); ++chunk) {
      internal << "void " << shortRoot << "_optimized_combs_prefix_chunk_"
               << chunk << "(" << shortRoot << "&, " << shortRoot
               << "_optimized_combs_state&);\n";
    }
    if (threadedCombs) {
      for (size_t stage = 0; stage < stageLaneChunks.size(); ++stage) {
        for (size_t lane = 0; lane < stageLaneChunks[stage].size(); ++lane) {
          for (size_t chunk = 0;
               chunk < stageLaneChunks[stage][lane].size(); ++chunk) {
            internal << "void " << shortRoot << "_optimized_combs_thread_"
                     << stage << "_" << lane << "_" << chunk << "("
                     << shortRoot << "&, " << shortRoot
                     << "_optimized_combs_state&);\n";
          }
        }
      }
    } else {
      for (size_t chunk = 0; chunk < chunkCount; ++chunk) {
        internal << "void " << shortRoot << "_optimized_combs_chunk_" << chunk
                 << "(" << shortRoot << "&, " << shortRoot
                 << "_optimized_combs_state&);\n";
      }
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
    for (size_t chunk = 0; chunk < memoryChunkCount; ++chunk) {
      internal << "void " << shortRoot << "_optimized_combs_memory_chunk_"
               << chunk << "(" << shortRoot << "&, " << shortRoot
               << "_optimized_combs_state&);\n";
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
    releaseBodies(modelBodies);

    std::unordered_set<size_t> emitted;
    for (const size_t id : schedule) {
      if (!emitted.insert(id).second) {
        error = "internal error: scheduled comb twice";
        return false;
      }
      const Node &node = nodes[id];
      for (const size_t dependency : node.dependencies) {
        // Dynamic dependencies are invoked at their expression site and are
        // deliberately absent from the eager emitted set.
        if (!dynamicNodes.contains(dependency) &&
            !emitted.contains(dependency)) {
          error = "internal error: dependency emitted after consumer " +
                  node.instance->path + "." + node.name;
          return false;
        }
      }
    }

    const auto appendNode = [&](std::ostringstream &body, size_t id) {
      const Node &node = nodes[id];
      body << "  " << nodeAssignment(id, node.expression) << ";\n";
    };

    for (size_t chunk = 0; chunk < prefixChunks.size(); ++chunk) {
      std::ostringstream body;
      std::set<size_t> usedInstances{0};
      for (const size_t id : prefixChunks[chunk]) {
        const Node &node = nodes[id];
        usedInstances.insert(node.instance->id);
        usedInstances = referencedInstances(node.expression, usedInstances);
        body << "  " << nodeAssignment(id, node.expression) << ";\n";
      }
      const std::filesystem::path chunkPath =
          std::filesystem::path(directory) /
          (shortRoot + "_optimized_combs_prefix_" + std::to_string(chunk) +
           ".cpp");
      std::ofstream chunkSource(chunkPath);
      if (!chunkSource) {
        error = "cannot write " + chunkPath.string();
        return false;
      }
      chunkSource << "#include \"" << shortRoot
                  << "_optimized_combs_internal.h\"\n\nvoid " << shortRoot
                  << "_optimized_combs_prefix_chunk_" << chunk << "("
                  << shortRoot << "& obj, " << shortRoot
                  << "_optimized_combs_state& s) {\n";
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
      const auto [begin, end] = dynamicChunks[chunk];
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
        if (inlineDynamicEvaluators.contains(id)) {
          continue;
        }
        const Node &node = nodes[id];
        const std::set<size_t> usedInstances =
            referencedInstances(node.expression, {0, node.instance->id});
        chunkSource << "void " << shortRoot << "_optimized_comb_eval_" << id
                    << "(" << shortRoot
                    << "& obj, " << shortRoot
                    << "_optimized_combs_state& s) {\n";
        emitAliases(chunkSource, usedInstances);
        if (cachesForSystemClock(node)) {
          chunkSource << "  s.evaluated" << id << " = _system_clock;\n";
        }
        chunkSource << "  " << nodeAssignment(id, node.expression)
                    << ";\n";
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

    if (threadedCombs) {
      for (size_t stage = 0; stage < stageLaneChunks.size(); ++stage) {
        for (size_t lane = 0; lane < stageLaneChunks[stage].size(); ++lane) {
          for (size_t chunk = 0;
               chunk < stageLaneChunks[stage][lane].size(); ++chunk) {
          std::ostringstream body;
          std::set<size_t> usedInstances{0};
          for (const size_t id : stageLaneChunks[stage][lane][chunk]) {
            const Node &node = nodes[id];
            usedInstances.insert(node.instance->id);
            usedInstances = referencedInstances(node.expression, usedInstances);
            appendNode(body, id);
          }
          const std::filesystem::path chunkPath =
              std::filesystem::path(directory) /
              (shortRoot + "_optimized_combs_thread_" +
               std::to_string(stage) + "_" + std::to_string(lane) + "_" +
               std::to_string(chunk) + ".cpp");
          std::ofstream chunkSource(chunkPath);
          if (!chunkSource) {
            error = "cannot write " + chunkPath.string();
            return false;
          }
          chunkSource << "#include \"" << shortRoot
                      << "_optimized_combs_internal.h\"\n\nvoid " << shortRoot
                      << "_optimized_combs_thread_" << stage << "_" << lane
                      << "_" << chunk << "(" << shortRoot << "& obj, "
                      << shortRoot << "_optimized_combs_state& s) {\n";
          emitAliases(chunkSource, referencedInstances({}, usedInstances));
          chunkSource << body.str() << "}\n";
          if (!finishOutput(chunkSource, chunkPath)) {
            return false;
          }
          }
        }
      }
    } else {
      for (size_t chunk = 0; chunk < chunkCount; ++chunk) {
        const size_t begin = chunk * valuesPerChunk;
        const size_t end = std::min(schedule.size(), begin + valuesPerChunk);
        std::ostringstream body;
        std::set<size_t> usedInstances{0};
        for (size_t position = begin; position < end; ++position) {
          const size_t id = schedule[position];
          const Node &node = nodes[id];
          usedInstances.insert(node.instance->id);
          usedInstances = referencedInstances(node.expression, usedInstances);
          appendNode(body, id);
        }
        const std::filesystem::path chunkPath =
            std::filesystem::path(directory) /
            (shortRoot + "_optimized_combs_" + std::to_string(chunk) +
             ".cpp");
        std::ofstream chunkSource(chunkPath);
        if (!chunkSource) {
          error = "cannot write " + chunkPath.string();
          return false;
        }
        chunkSource << "#include \"" << shortRoot
                    << "_optimized_combs_internal.h\"\n\nvoid " << shortRoot
                    << "_optimized_combs_chunk_" << chunk << "(" << shortRoot
                    << "& obj, " << shortRoot
                    << "_optimized_combs_state& s) {\n";
        emitAliases(chunkSource, referencedInstances({}, usedInstances));
        chunkSource << body.str() << "}\n";
        if (!finishOutput(chunkSource, chunkPath)) {
          return false;
        }
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

    for (size_t chunk = 0; chunk < memoryChunkCount; ++chunk) {
      const size_t begin = chunk * memoryWritesPerChunk;
      const size_t end =
          std::min(deferredWrites.size(), begin + memoryWritesPerChunk);
      const std::filesystem::path chunkPath =
          std::filesystem::path(directory) /
          (shortRoot + "_optimized_combs_memory_" + std::to_string(chunk) +
           ".cpp");
      std::ofstream chunkSource(chunkPath);
      if (!chunkSource) {
        error = "cannot write " + chunkPath.string();
        return false;
      }
      std::set<size_t> usedInstances{0};
      for (size_t position = begin; position < end; ++position) {
        usedInstances.insert(deferredWrites[position].instance->id);
      }
      chunkSource << "#include \"" << shortRoot
                  << "_optimized_combs_internal.h\"\n\nvoid " << shortRoot
                  << "_optimized_combs_memory_chunk_" << chunk << "("
                  << shortRoot << "& obj, " << shortRoot
                  << "_optimized_combs_state& s) {\n";
      emitAliases(chunkSource, referencedInstances({}, usedInstances));
      for (size_t position = begin; position < end; ++position) {
        const DeferredWrite &write = deferredWrites[position];
        chunkSource << "  if (s.w" << write.id << "_pending) "
                    << write.instance->alias << "." << write.field;
        for (size_t dimension = 0; dimension < write.indexes.size();
             ++dimension) {
          chunkSource << "[s.w" << write.id << "_index_" << dimension << "]";
        }
        chunkSource << " = s.w" << write.id << "_value;\n";
      }
      chunkSource << "}\n";
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
           << "#include <array>\n#include <unordered_map>\n#include <vector>\n"
           << "#if defined(__linux__)\n#include <pthread.h>\n#include <sched.h>\n#endif\n\n"
           << "namespace {\nthread_local std::unordered_map<" << shortRoot
           << "*, " << shortRoot << "_optimized_combs_state> " << shortRoot
           << "_optimized_states;\n\n" << shortRoot
           << "_optimized_combs_state& optimized_state(" << shortRoot
           << "& obj) {\n  return " << shortRoot
           << "_optimized_states[std::addressof(obj)];\n}\n\n";
    if (threadedCombs) {
      source << "class " << shortRoot << "_optimized_combs_runtime {\n"
             << "public:\n  " << shortRoot
             << "_optimized_combs_runtime() {\n"
             << "    pin_caller();\n"
             << "    workers_.reserve(" << (combThreadCount - 1) << ");\n"
             << "    for (std::size_t lane = 1; lane < " << combThreadCount
             << "; ++lane) {\n"
             << "      workers_.emplace_back([this, lane] { worker(lane); });\n"
             << "    }\n  }\n\n"
             << "  ~" << shortRoot << "_optimized_combs_runtime() {\n"
             << "    command_.fetch_or(stop_bit, std::memory_order_release);\n"
             << "    for (auto& worker : workers_) {\n"
             << "      worker.join();\n    }\n"
             << "    restore_caller();\n  }\n\n"
             << "  void run(" << shortRoot << "& obj, " << shortRoot
             << "_optimized_combs_state& state) {\n"
             << "    object_ = std::addressof(obj);\n"
             << "    state_ = std::addressof(state);\n"
             << "    const std::uint64_t command = ++command_value_;\n"
             << "    command_.store(command, std::memory_order_release);\n";
      const bool singleStage = combStages.size() == 1;
      if (singleStage) {
        source << "    run_lane(0, obj, state);\n"
               << "    for (std::size_t lane = 1; lane < " << combThreadCount
               << "; ++lane) {\n"
               << "      while (completed_[lane - 1].value.load("
                  "std::memory_order_acquire) != command) {\n"
               << "        cpphdl_optimized_thread_pause();\n      }\n"
               << "    }\n  }\n\n"
               << "private:\n  void run_lane(std::size_t lane, "
               << shortRoot << "& obj, " << shortRoot
               << "_optimized_combs_state& state) {\n"
               << "    switch (lane) {\n";
        for (size_t lane = 0; lane < stageLaneChunks.front().size(); ++lane) {
          source << "    case " << lane << ":\n";
          for (size_t chunk = 0;
               chunk < stageLaneChunks.front()[lane].size(); ++chunk) {
            source << "      " << shortRoot << "_optimized_combs_thread_0_"
                   << lane << "_" << chunk << "(obj, state);\n";
          }
          source << "      break;\n";
        }
        source << "    }\n  }\n\n";
      } else {
        source << "    for (std::size_t stage = 0; stage < "
               << combStages.size() << "; ++stage) {\n"
               << "      run_stage(stage, 0, obj, state);\n"
               << "      const std::uint64_t token = command * "
               << (combStages.size() + 1) << " + stage + 1;\n"
               << "      for (std::size_t lane = 1; lane < "
               << combThreadCount << "; ++lane) {\n"
               << "        while (completed_[lane - 1].value.load("
                  "std::memory_order_acquire) != token) {\n"
               << "          cpphdl_optimized_thread_pause();\n        }\n"
               << "      }\n"
               << "      if (stage + 1 != " << combStages.size() << ") {\n"
               << "        stage_release_.store(token, "
                  "std::memory_order_release);\n"
               << "      }\n"
               << "    }\n  }\n\n"
               << "private:\n  void run_stage(std::size_t stage, "
                  "std::size_t lane, " << shortRoot
               << "& obj, " << shortRoot
               << "_optimized_combs_state& state) {\n"
               << "    switch (stage) {\n";
        for (size_t stage = 0; stage < stageLaneChunks.size(); ++stage) {
          source << "    case " << stage << ":\n"
                 << "      switch (lane) {\n";
          for (size_t lane = 0; lane < stageLaneChunks[stage].size(); ++lane) {
            source << "      case " << lane << ":\n";
            for (size_t chunk = 0;
                 chunk < stageLaneChunks[stage][lane].size(); ++chunk) {
              source << "        " << shortRoot << "_optimized_combs_thread_"
                     << stage << "_" << lane << "_" << chunk
                     << "(obj, state);\n";
            }
            source << "        break;\n";
          }
          source << "      }\n      break;\n";
        }
        source << "    }\n  }\n\n";
      }
      source << "  void worker(std::size_t lane) {\n"
             << "    pin_worker(lane);\n"
             << "    std::uint64_t observed = 0;\n"
             << "    for (;;) {\n"
             << "      std::uint64_t command;\n"
             << "      do {\n"
             << "        command = command_.load(std::memory_order_acquire);\n"
             << "        if ((command & stop_bit) != 0) {\n"
             << "          return;\n        }\n"
             << "        if (command == observed) {\n"
             << "          cpphdl_optimized_thread_pause();\n        }\n"
             << "      } while (command == observed);\n"
             << "      observed = command;\n"
             << "      auto* object = object_;\n      auto* state = state_;\n";
      if (singleStage) {
        source << "      run_lane(lane, *object, *state);\n"
               << "      completed_[lane - 1].value.store("
                  "observed, std::memory_order_release);\n";
      } else {
        source << "      for (std::size_t stage = 0; stage < "
               << combStages.size() << "; ++stage) {\n"
               << "        run_stage(stage, lane, *object, *state);\n"
               << "        const std::uint64_t token = observed * "
               << (combStages.size() + 1) << " + stage + 1;\n"
               << "        completed_[lane - 1].value.store(token, "
                  "std::memory_order_release);\n"
               << "        if (stage + 1 != " << combStages.size()
               << ") {\n"
               << "          while (stage_release_.load("
                  "std::memory_order_acquire) != token) {\n"
               << "            cpphdl_optimized_thread_pause();\n          }\n"
               << "        }\n"
               << "      }\n";
      }
      source << "    }\n  }\n\n"
             << R"CPP(  void pin_caller() {
#if defined(__linux__)
    if (pthread_getaffinity_np(pthread_self(), sizeof(original_affinity_),
                               &original_affinity_) != 0) {
      return;
    }
    caller_cpu_ = sched_getcpu();
    if (caller_cpu_ < 0 || !CPU_ISSET(caller_cpu_, &original_affinity_)) {
      caller_cpu_ = -1;
      return;
    }
    cpu_set_t affinity;
    CPU_ZERO(&affinity);
    CPU_SET(caller_cpu_, &affinity);
    caller_pinned_ = pthread_setaffinity_np(
                         pthread_self(), sizeof(affinity), &affinity) == 0;
#endif
  }

  void pin_worker(std::size_t lane) {
#if defined(__linux__)
    if (!caller_pinned_) {
      return;
    }
    std::size_t worker_index = 0;
    int worker_cpu = -1;
    for (int cpu = 0; cpu < CPU_SETSIZE; ++cpu) {
      if (!CPU_ISSET(cpu, &original_affinity_) || cpu == caller_cpu_) {
        continue;
      }
      if (worker_index++ == lane - 1) {
        worker_cpu = cpu;
        break;
      }
    }
    if (worker_cpu < 0) {
      return;
    }
    cpu_set_t affinity;
    CPU_ZERO(&affinity);
    CPU_SET(worker_cpu, &affinity);
    (void)pthread_setaffinity_np(pthread_self(), sizeof(affinity), &affinity);
#else
    (void)lane;
#endif
  }

  void restore_caller() {
#if defined(__linux__)
    if (caller_pinned_) {
      (void)pthread_setaffinity_np(pthread_self(), sizeof(original_affinity_),
                                   &original_affinity_);
    }
#endif
  }

)CPP"
             << "  static constexpr std::uint64_t stop_bit = "
                "std::uint64_t{1} << 63;\n"
             << "  struct alignas(64) completion_slot {\n"
             << "    std::atomic<std::uint64_t> value{0};\n  };\n"
             << "  std::vector<std::thread> workers_;\n"
             << "  " << shortRoot << "* object_ = nullptr;\n"
             << "  " << shortRoot
             << "_optimized_combs_state* state_ = nullptr;\n"
             << "  alignas(64) std::atomic<std::uint64_t> command_{0};\n"
             << "  alignas(64) std::atomic<std::uint64_t> stage_release_{0};\n"
             << "  std::uint64_t command_value_ = 0;\n"
             << "  std::array<completion_slot, " << (combThreadCount - 1)
             << "> completed_{};\n"
             << "#if defined(__linux__)\n"
             << "  cpu_set_t original_affinity_{};\n"
             << "  int caller_cpu_ = -1;\n"
             << "  bool caller_pinned_ = false;\n"
             << "#endif\n"
             << "};\n\n"
             << "thread_local std::unordered_map<" << shortRoot
             << "*, std::unique_ptr<" << shortRoot
             << "_optimized_combs_runtime>> " << shortRoot
             << "_optimized_runtimes;\n\n"
             << shortRoot << "_optimized_combs_runtime& optimized_runtime("
             << shortRoot << "& obj) {\n"
             << "  auto& runtime = " << shortRoot
             << "_optimized_runtimes[std::addressof(obj)];\n"
             << "  if (!runtime) {\n    runtime = std::make_unique<" << shortRoot
             << "_optimized_combs_runtime>();\n  }\n"
             << "  return *runtime;\n}\n\n";
    }
    source << "}\n\nvoid bind_optimized_ports(" << shortRoot << "& obj) {\n";
    for (size_t chunk = 0; chunk < bindChunkCount; ++chunk) {
      source << "  " << shortRoot << "_optimized_combs_bind_chunk_" << chunk
             << "(obj);\n";
    }
    if (!rootOutputPorts.empty()) {
      source << "  auto& s = optimized_state(obj);\n";
      std::set<size_t> usedInstances{0};
      for (const RootOutputPort &port : rootOutputPorts) {
        usedInstances.insert(nodes[port.value].instance->id);
      }
      emitAliases(source, usedInstances);
      for (const RootOutputPort &port : rootOutputPorts) {
        // calc_all materializes every externally visible root output. Rebind
        // its function_ref to that stable lvalue so a host read cannot re-enter
        // the original recursive std::function/comb graph.
        source << "  obj." << port.name << " = _ASSIGN_REG("
               << nodeValue(port.value) << ");\n";
      }
    }
    source << "}\n\nextern \"C\" void "
           << "cpphdl_optimized_bind_ports_abi(void* raw_obj) {\n"
           << "  bind_optimized_ports(*static_cast<" << shortRoot
           << "*>(raw_obj));\n";
    // Flattening replaces calls to many module _work methods, but their reset
    // argument remains part of the model's cycle semantics. Thread that one
    // runtime value through the dispatcher and every emitted work chunk.
    source << "}\n\n"
           << "void calc_all(" << shortRoot << "& obj, bool reset) {\n"
           << "  auto& s = optimized_state(obj);\n";
    // calc_all may be reached by more than one observer in a host cycle.
    // Match function_ref and _LAZY_COMB by evaluating the specialized graph
    // only once for each root object and _system_clock value.
    source << "  if (s.evaluated_system_clock == _system_clock) return;\n"
           << "  s.evaluated_system_clock = _system_clock;\n";
    for (const DeferredWrite &write : deferredWrites) {
      source << "  s.w" << write.id << "_pending = false;\n";
    }
    // Clock-cached evaluators compare their timestamps directly with
    // _system_clock, avoiding an O(graph-size) flag-clear pass. Ordinary comb
    // recursion flags are cleared when each invocation returns.
    for (size_t chunk = 0; chunk < prefixChunks.size(); ++chunk) {
      source << "  " << shortRoot << "_optimized_combs_prefix_chunk_" << chunk
             << "(obj, s);\n";
    }
    if (threadedCombs) {
      source << "  optimized_runtime(obj).run(obj, s);\n";
    } else {
      for (size_t chunk = 0; chunk < chunkCount; ++chunk) {
        source << "  " << shortRoot << "_optimized_combs_chunk_" << chunk
               << "(obj, s);\n";
      }
    }
    for (size_t chunk = 0; chunk < workChunks.size(); ++chunk) {
      source << "  " << shortRoot << "_optimized_combs_work_chunk_" << chunk
             << "(obj, s, reset);\n";
    }
    std::set<size_t> demandedRootOutputs;
    for (const RootOutputPort &port : rootOutputPorts) {
      const size_t id = port.value;
      if (!dynamicNodes.contains(id) || !demandedRootOutputs.insert(id).second) {
        continue;
      }
      const std::string state =
          cachesForSystemClock(nodes[id])
              ? "s.evaluated" + std::to_string(id) + " == _system_clock"
              : std::string{};
      if (state.empty()) {
        source << "  " << shortRoot << "_optimized_comb_eval_" << id
               << "(obj, s);\n";
      } else {
        source << "  if (!(" << state << ")) " << shortRoot
               << "_optimized_comb_eval_" << id << "(obj, s);\n";
      }
    }
    source << "}\n\nvoid commit_optimized_regs(" << shortRoot << "& obj) {\n";
    if (memoryChunkCount != 0) {
      source << "  auto& s = optimized_state(obj);\n";
    }
    for (size_t chunk = 0; chunk < memoryChunkCount; ++chunk) {
      source << "  " << shortRoot << "_optimized_combs_memory_chunk_" << chunk
             << "(obj, s);\n";
    }
    for (size_t chunk = 0; chunk < strobeChunks.size(); ++chunk) {
      source << "  " << shortRoot << "_optimized_combs_strobe_chunk_" << chunk
             << "(obj);\n";
    }
    source << "}\n";
    if (!finishOutput(source, sourcePath)) {
      return false;
    }
    // Restore timestamps only after every output has closed. A size and content
    // hash match means downstream dependency checks can safely reuse the old
    // object even though generation used a truncate-and-rewrite stream.
    for (const auto &[name, previous] : previousOutputs) {
      const std::filesystem::path path =
          std::filesystem::path(directory) / name;
      if (std::filesystem::is_regular_file(path) &&
          std::filesystem::file_size(path) == previous.size &&
          fingerprint(path) == previous.hash) {
        std::filesystem::last_write_time(path, previous.modified);
      }
    }
    llvm::outs() << "CppHDL comb optimizer: " << instances.size()
                 << " instances, " << schedule.size() << " scheduled values, "
                 << (threadedCombs ? combThreadCount : 1) << " comb threads, "
                 << componentCount << " independent stage components, "
                 << combStages.size() << " dependency stages, "
                 << "0 within-stage cross-lane edges, "
                 << (threadedCombs
                         ? std::to_string(combStages.size()) +
                               " stage joins, " +
                               std::to_string(combStages.size() - 1) +
                               " stage releases, "
                                   : "0 end-of-cycle joins, ")
                 << (threadedCombs ? threadChunkCount : chunkCount)
                 << " comb chunks, "
                 << prefixValues.size() << " serial-prefix values (weight "
                 << prefixWeight << "), "
                 << bindChunkCount
                 << " bind chunks, " << workChunks.size() << " work chunks, "
                 << strobeChunks.size() << " strobe chunks, "
                 << modelChunkCount << " model chunks, "
                 << memoryChunkCount << " memory chunks, "
                 << dynamicSchedule.size() << " dynamic evaluators, "
                 << inlineDynamicEvaluators.size() << " inline evaluators, "
                 << clockCachedDynamicStates << " dynamic states, "
                 << lazyCycleBackEdges << " lazy cycle back-edges\n"
                 << (threadedCombs ? "  estimated staged weight: " : "")
                 << (threadedCombs
                         ? std::to_string(estimatedParallelWeight) + "\n"
                         : "")
                 << (threadedCombs
                         ? [&]() {
                             std::ostringstream weights;
                             for (size_t stage = 0;
                                  stage < combStages.size(); ++stage) {
                               weights << "  stage " << stage
                                       << " lane weights: ";
                               for (size_t lane = 0;
                                    lane < combStages[stage].laneWeights.size();
                                    ++lane) {
                                 if (lane != 0) {
                                   weights << ",";
                                 }
                                 weights << combStages[stage].laneWeights[lane];
                               }
                               weights << "\n";
                             }
                             return weights.str();
                           }()
                         : "")
                 << (mathOptimization ? "  math replacements: " : "")
                 << (mathOptimization ? std::to_string(mathBitReversals) +
                                             " reversals, " +
                                             std::to_string(mathReplications) +
                                             " replications, " +
                                             std::to_string(mathSignExtensions) +
                                             " sign extensions\n"
                                      : "")
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
  Collector collector(impl->classes, impl->freeHelpers, impl->sourceIncludes,
                      impl->constantValues, impl->rootName, context,
                      impl->collectionOnly);
  collector.collectMainFileIncludes();
  collector.collectRootHierarchy();
  collector.TraverseDecl(context.getTranslationUnitDecl());
  // A specialization and its primary-template methods can arrive in separate
  // collection shards. Reconcile their deferred body handles after all loaded
  // metadata and the final AST have been merged; this does not load body text.
  collector.applyTemplateMethods(!impl->collectionBodies.empty());
  collector.materializeRootAlias();
}

void CombsOptimizer::setL1Scheduling(bool enabled) {
  impl->l1Scheduling = enabled;
}

void CombsOptimizer::setCollectionOnly(bool enabled) {
  impl->collectionOnly = enabled;
}

bool CombsOptimizer::saveCollection(const std::string &path) const {
  return impl->saveCollection(path);
}

bool CombsOptimizer::loadCollection(const std::string &path) {
  return impl->loadCollection(path);
}

void CombsOptimizer::setMathOptimization(bool enabled) {
  impl->mathOptimization = enabled;
}

void CombsOptimizer::setThreadCount(std::size_t count) {
  impl->threadCount = std::max<std::size_t>(1, count);
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
