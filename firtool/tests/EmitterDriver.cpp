#include "circt/Conversion/Exportcpphdl.h"
#include "circt/Dialect/Comb/CombDialect.h"
#include "circt/Dialect/HW/HWDialect.h"
#include "circt/Dialect/Seq/SeqDialect.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/Parser/Parser.h"
#include "llvm/Support/raw_ostream.h"

int main(int argc, char** argv) {
    if (argc != 3) return 2;
    mlir::MLIRContext context;
    context.loadDialect<circt::hw::HWDialect, circt::comb::CombDialect, circt::seq::SeqDialect>();
    auto module = mlir::parseSourceFile<mlir::ModuleOp>(argv[1], &context);
    if (!module) return 1;
    if (llvm::StringRef(argv[2]) == "--single")
        return mlir::failed(circt::exportcpphdl(*module, llvm::outs()));
    return mlir::failed(circt::exportSplitcpphdl(*module, argv[2]));
}
