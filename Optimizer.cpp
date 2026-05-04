#include "Optimizer.h"
#include "Expr.h"
#include "Module.h"
#include "Field.h"
#include "Method.h"

#include <algorithm>

using namespace cpphdl;

void Optimizer::collectAccesses(Expr& expr, std::unordered_map<std::string, VarStat>& vars)
{
    expr.traverseIf([&](auto& e){
            if (e.type == Expr::EXPR_MEMBER || e.type == Expr::EXPR_VAR || e.type == Expr::EXPR_INDEX) {
                Expr* curr = &e;
                if (curr->type == Expr::EXPR_INDEX && curr->sub.size()) {
                    curr = &curr->sub[0];
                    while ((curr->type == Expr::EXPR_CAST || curr->type == Expr::EXPR_UNARY) && curr->sub.size()) {
                        curr = &curr->sub[0];
                    }
                }
                while (curr->type == Expr::EXPR_MEMBER && curr->sub.size() && curr->sub[0].type != Expr::EXPR_NONE) {  // we take only very base of expression to check access count
                    curr = &curr->sub[0];
                    while ((curr->type == Expr::EXPR_CAST || curr->type == Expr::EXPR_UNARY) && curr->sub.size()) {
                        curr = &curr->sub[0];
                    }
                }

                std::string name = curr->str();
                if (!name.empty()) {
//std::cout << "~~~~~ " << name << " was accessed:" << e.debug() << "\n";
                    auto& stat = vars[name];
                    ++stat.accessed;
                }
                return true;
            }
            return false;
        });
}

void Optimizer::replaceAssignments(Expr& expr, const std::unordered_map<std::string, VarStat>& vars)
{
    expr.traverseIf([&](auto& e){
            if ((e.type == Expr::EXPR_BINARY || e.type == Expr::EXPR_OPERATORCALL) && e.value == "=" && e.sub.size() >= 2) {
                // traverse left side of =
                e.sub[0].traverseIf([&](auto& e1){
                        if (e1.type == Expr::EXPR_MEMBER && e1.value == "_next" && e1.sub.size()) {
                            Expr* curr = &e1;
                            if (curr->type == Expr::EXPR_INDEX && curr->sub.size()) {
                                curr = &curr->sub[0];
                                while ((curr->type == Expr::EXPR_CAST || curr->type == Expr::EXPR_UNARY) && curr->sub.size()) {
                                    curr = &curr->sub[0];
                                }
                            }
                            if (curr->type == Expr::EXPR_MEMBER && curr->sub.size() && curr->sub[0].type != Expr::EXPR_NONE) {    // we take only very base of expression to check access count
                                curr = &curr->sub[0];
                                while ((curr->type == Expr::EXPR_CAST || curr->type == Expr::EXPR_UNARY) && curr->sub.size()) {
                                    curr = &curr->sub[0];
                                }
                            }

                            std::string name = curr->str();
                            auto stat = vars.find(name);
                            if (stat != vars.end() && stat->second.accessed == 1 && currModule && currModule->isReg(name)) {
                                e1 = e1.sub[0];  // remove _next
                                e.value = "<=";
                                currModule->onceAccessedRegs.insert(name);
                            }
                        }
                        return false;
                    });
                return true;
            }
            return false;
        });
}

void Optimizer::optimizeBlocking(std::vector<Method>& methods)
{
    std::unordered_map<std::string, VarStat> vars;

    for (auto& method: methods) {
        if (str_ending(method.name, "_comb_func")) {
            continue;
        }
        for (auto& stmt : method.statements) {
            collectAccesses(stmt, vars);
        }
    }
    for (auto& method: methods) {
        if (str_ending(method.name, "_comb_func")) {
            continue;
        }
        for (auto& stmt : method.statements) {
            replaceAssignments(stmt, vars);
        }
    }
}
