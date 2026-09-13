#pragma once

namespace clang { class ASTContext; class Sema; }

// Audit native simulation calls before the SV lowering removes them.
void checkModuleLifecycleCalls(clang::ASTContext& context, clang::Sema& sema);
