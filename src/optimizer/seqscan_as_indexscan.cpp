//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// seqscan_as_indexscan.cpp
//
// Identification: src/optimizer/seqscan_as_indexscan.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include <memory>
#include <optional>
#include <tuple>
#include <utility>
#include <vector>
#include "common/macros.h"
#include "execution/expressions/abstract_expression.h"
#include "execution/expressions/column_value_expression.h"
#include "execution/expressions/comparison_expression.h"
#include "execution/expressions/constant_value_expression.h"
#include "execution/expressions/logic_expression.h"
#include "execution/plans/abstract_plan.h"
#include "execution/plans/index_scan_plan.h"
#include "execution/plans/seq_scan_plan.h"
#include "optimizer/optimizer.h"

namespace bustub {

using DeComparisonExprType = std::tuple<unsigned int, AbstractExpressionRef, AbstractExpressionRef>;
/**
 * @brief Convert/Split ComparisonExpression to a pair of ColumnValueExpression and ConstantValueExpression
 * @params shared_ptr of ComparisonExpression
 * @return optional tuple of { ColumnIdx, ColumnValueExpressionRef, ConstantValueExpressionRef}
 */
auto SplitComparisonExpr(AbstractExpressionRef expr) -> std::optional<DeComparisonExprType> {
  // single constant comparison, e.g WHERE colA = x;
  const auto *comp_expr = dynamic_cast<const ComparisonExpression *>(expr.get());

  if (comp_expr == nullptr) {
    return std::nullopt;
  }
  if (comp_expr->comp_type_ == ComparisonType::Equal) {
    auto left = comp_expr->children_[0];
    auto right = comp_expr->children_[1];
    auto *column_expr = dynamic_cast<const ColumnValueExpression *>(left.get());
    auto *constant_value_expr = dynamic_cast<const ConstantValueExpression *>(right.get());

    if (column_expr != nullptr && constant_value_expr != nullptr) {
      return std::make_optional(DeComparisonExprType{column_expr->GetColIdx(), left, right});
    }
    // try flipping in case of WHERE x = colA;
    column_expr = dynamic_cast<const ColumnValueExpression *>(right.get());
    constant_value_expr = dynamic_cast<const ConstantValueExpression *>(left.get());
    if (column_expr != nullptr && constant_value_expr != nullptr) {
      return std::make_optional(DeComparisonExprType{column_expr->GetColIdx(), right, left});
    }
  }
  return std::nullopt;
}

/*
 * @brief ResolveExpr heler that will bnreak up the Compare Expr or Logical Or Epxr
 * @params, expr that needs to be resolved/splitted
 * @params OUT result_vector of tuples, tuple = {ColumnIdx, ColumnValueExpressionRef, ConstantValueExpressionRef}
 */
auto ResolveExpr(AbstractExpressionRef expr, std::vector<DeComparisonExprType> *result_vector) -> bool {
  BUSTUB_ASSERT(result_vector != nullptr, "in param vector can not be null");

  const auto *logic_expr = dynamic_cast<const LogicExpression *>(expr.get());

  if (logic_expr == nullptr) {
    // this is the last comparison expr in the nested tree
    auto optional_col_val = SplitComparisonExpr(expr);
    if (!optional_col_val.has_value()) {
      return false;
    }
    result_vector->emplace_back(std::move(optional_col_val.value()));
    return true;
  }

  if (logic_expr->logic_type_ != LogicType::Or) {
    return false;
  }
  // expr is a logical OR
  for (const auto &child_expr : logic_expr->children_) {
    if (!ResolveExpr(child_expr, result_vector)) {
      return false;
    }
  }

  return true;
}

/**
 * @brief Optimizes seq scan as index scan if there's an index on a table
 */
auto Optimizer::OptimizeSeqScanAsIndexScan(const bustub::AbstractPlanNodeRef &plan) -> AbstractPlanNodeRef {
  std::vector<AbstractPlanNodeRef> children;
  for (const auto &child : plan->GetChildren()) {
    children.emplace_back(OptimizeSeqScanAsIndexScan(child));
  }

  auto optimized_plan = plan->CloneWithChildren(std::move(children));

  if (optimized_plan->GetType() == PlanType::SeqScan) {
    const auto &seq_plan = dynamic_cast<const SeqScanPlanNode &>(*optimized_plan);
    BUSTUB_ASSERT(optimized_plan->children_.empty(), "should not have any children");
    auto expr = seq_plan.filter_predicate_;
    if (expr != nullptr) {
      // assume that this is a single comparsion, e.g WHERE colA = x;
      auto result_vector = std::vector<DeComparisonExprType>{};
      if (ResolveExpr(expr, &result_vector)) {
        if (!result_vector.empty()) {
          auto [first_col_id, col_expr_ref, const_value_expr_ref] = result_vector[0];

          if (auto index = MatchIndex(seq_plan.table_name_, first_col_id); index.has_value()) {
            auto const_value_ref_vector =
                std::vector<AbstractExpressionRef>{};  // used to construct the index scan plan
                                                       //
            auto [index_oid, index_name] = index.value();

            for (auto &p : result_vector) {
              // all pair should have the same column id
              auto [c_col_id, c_col_expr_ref, c_const_value_expr_ref] = p;
              if (c_col_id != first_col_id) {
                return optimized_plan;  // all column idx are not consistent
              }
              const_value_ref_vector.emplace_back(std::move(c_const_value_expr_ref));
            }
            return std::make_shared<IndexScanPlanNode>(seq_plan.output_schema_, seq_plan.table_oid_, index_oid, nullptr,
                                                       const_value_ref_vector);
          }
        }
      }
    }
  }
  return optimized_plan;
}

}  // namespace bustub
