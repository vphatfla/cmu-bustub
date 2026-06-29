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

/**
 * @brief Convert/Split ComparisonExpression to a pair of ColumnValueExpression and ConstantValueExpression
 * @params shared_ptr of ComparisonExpression
 * @return optional pair of [ColumnValueExpression, ConstantValueExpression] (both are ref/shared_ptr
 */
auto SplitComparisonExpr(AbstractExpressionRef expr)
    -> std::optional<std::pair<AbstractExpressionRef, AbstractExpressionRef>> {
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
      return std::make_optional(std::pair<AbstractExpressionRef, AbstractExpressionRef>{left, right});
    }
    // try flipping in case of WHERE x = colA;
    column_expr = dynamic_cast<const ColumnValueExpression *>(right.get());
    constant_value_expr = dynamic_cast<const ConstantValueExpression *>(left.get());
    if (column_expr != nullptr && constant_value_expr != nullptr) {
      return std::make_optional(std::pair<AbstractExpressionRef, AbstractExpressionRef>{right, left});
    }
  }
  return std::nullopt;
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
      auto optional_col_and_const_val = SplitComparisonExpr(expr);
      if (optional_col_and_const_val.has_value()) {
        auto [col_expr_ref, const_value_expr_ref] = optional_col_and_const_val.value();
        auto *column_expr = dynamic_cast<const ColumnValueExpression *>(col_expr_ref.get());
        BUSTUB_ASSERT(column_expr != nullptr, "dynamic cast failed, unexpected type");

        if (auto index = MatchIndex(seq_plan.table_name_, column_expr->GetColIdx()); index.has_value()) {
          auto [index_oid, index_name] = index.value();
          return std::make_shared<IndexScanPlanNode>(seq_plan.output_schema_, seq_plan.table_oid_, index_oid, nullptr,
                                                     std::vector<AbstractExpressionRef>{const_value_expr_ref});
        }
      } else if (const auto *logic_expr = dynamic_cast<const LogicExpression *>(expr.get()); logic_expr != nullptr) {
        // logic comparison expr, e.g WHERE colA = x OR colA = y; only handle OR (AND will be treated as seq scan)
        if (logic_expr->logic_type_ == LogicType::Or) {
          BUSTUB_ASSERT(logic_expr->children_.size() == 2, "logic OR epr must have exact 2 cmp expr children");
          auto left_optional_col_const = SplitComparisonExpr(logic_expr->children_[0]);
          auto right_optional_col_const = SplitComparisonExpr(logic_expr->children_[1]);

          // both left and right children must be EQUAL comparison expression
          if (left_optional_col_const.has_value() && right_optional_col_const.has_value()) {
            auto [left_col_expr_ref, left_const_value_expr_ref] = left_optional_col_const.value();
            auto [right_col_expr_ref, right_const_value_epxr_ref] = right_optional_col_const.value();

            auto *left_col_expr = dynamic_cast<const ColumnValueExpression *>(left_col_expr_ref.get());
            auto *right_col_expr = dynamic_cast<const ColumnValueExpression *>(right_col_expr_ref.get());

            BUSTUB_ASSERT(left_col_expr != nullptr && right_col_expr != nullptr,
                          "dynamic cast failed, unexpected type");

            if (left_col_expr->GetColIdx() == right_col_expr->GetColIdx()) {
              if (auto index = MatchIndex(seq_plan.table_name_, left_col_expr->GetColIdx()); index.has_value()) {
                auto [index_oid, index_name] = index.value();
                return std::make_shared<IndexScanPlanNode>(
                    seq_plan.output_schema_, seq_plan.table_oid_, index_oid, nullptr,
                    std::vector<AbstractExpressionRef>{left_const_value_expr_ref, right_const_value_epxr_ref});
              }
            }
          }
        }
      }
    }
  }
  return optimized_plan;
}

}  // namespace bustub
