#include "TemplatePortInitializer.h"

static_assert(sizeof(TemplatePortInitializer) != 0);
TemplatePortInitializer* cpphdlCombsCollectRoot = nullptr;
InitializedPortLeaf<cpphdl::logic<32>>* cpphdlCombsCollectWide = nullptr;
InitializedPortLeaf<cpphdl::logic<8>>* cpphdlCombsCollectNarrow = nullptr;
