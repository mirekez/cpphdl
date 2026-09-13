#pragma once

namespace clang { class ASTContext; }

// Audit native simulation calls before the SV lowering removes them.
void checkModuleLifecycleCalls(clang::ASTContext& context);
