//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// nlj_as_hash_join.cpp
//
// Identification: src/optimizer/nlj_as_hash_join.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <memory>
#include <optional>
#include <utility>
#include <vector>
#include "catalog/column.h"
#include "catalog/schema.h"
#include "common/exception.h"
#include "common/macros.h"
#include "execution/expressions/abstract_expression.h"
#include "execution/expressions/column_value_expression.h"
#include "execution/expressions/comparison_expression.h"
#include "execution/expressions/constant_value_expression.h"
#include "execution/expressions/logic_expression.h"
#include "execution/plans/abstract_plan.h"
#include "execution/plans/filter_plan.h"
#include "execution/plans/hash_join_plan.h"
#include "execution/plans/nested_loop_join_plan.h"
#include "execution/plans/projection_plan.h"
#include "optimizer/optimizer.h"
#include "type/type_id.h"

namespace bustub {

// @brief slit the equi condition expr into 2 col exprs
auto SplitEquiJoinExprIntoHashKeys(const AbstractExpressionRef &expr, std::vector<AbstractExpressionRef> *left_keys,
                                   std::vector<AbstractExpressionRef> *right_keys) -> bool {
  const auto *cmp_expr = dynamic_cast<const ComparisonExpression *>(expr.get());

  if (cmp_expr == nullptr) {
    return false;
  }
  if (cmp_expr->comp_type_ == ComparisonType::Equal) {
    const auto *col_one = dynamic_cast<const ColumnValueExpression *>(cmp_expr->GetChildAt(0).get());
    const auto *col_two = dynamic_cast<const ColumnValueExpression *>(cmp_expr->GetChildAt(1).get());

    if (col_one == nullptr || col_two == nullptr) {
      return false;
    }

    if (col_one->GetTupleIdx() == 0 && col_two->GetTupleIdx() == 1) {
      left_keys->emplace_back(expr->GetChildAt(0));
      right_keys->emplace_back(expr->GetChildAt(1));
      return true;
    }
    if (col_one->GetTupleIdx() == 1 && col_two->GetTupleIdx() == 0) {
      left_keys->emplace_back(expr->GetChildAt(1));
      right_keys->emplace_back(expr->GetChildAt(0));
      return true;
    }
  }

  return false;
}
// @brief flatten the original predicate expression from the NLJ into left_keys and right_keys respectively
auto ResolveNLJToHashKey(const AbstractExpressionRef &expr, std::vector<AbstractExpressionRef> *left_keys,
                         std::vector<AbstractExpressionRef> *right_keys) -> bool {
  BUSTUB_ASSERT(left_keys != nullptr, "OUT PARAM vector cannot be null");
  BUSTUB_ASSERT(right_keys != nullptr, "OUT PARAM vector cannot be null");

  const auto *logic_expr = dynamic_cast<const LogicExpression *>(expr.get());

  if (logic_expr == nullptr) {
    return SplitEquiJoinExprIntoHashKeys(expr, left_keys, right_keys);
  }

  if (logic_expr->logic_type_ == LogicType::And) {
    BUSTUB_ASSERT(logic_expr->children_.size() == 2, "Equi AND logical expr must have exact 2 children");
    return ResolveNLJToHashKey(expr->GetChildAt(0), left_keys, right_keys) &&
           ResolveNLJToHashKey(expr->GetChildAt(1), left_keys, right_keys);
  }

  return false;
}
/**
 * @brief optimize nested loop join into hash join.
 * In the starter code, we will check NLJs with exactly one equal condition. You can further support optimizing joins
 * with multiple eq conditions.
 */
auto Optimizer::OptimizeNLJAsHashJoin(const AbstractPlanNodeRef &plan) -> AbstractPlanNodeRef {
  auto children = std::vector<AbstractPlanNodeRef>{};
  for (const auto &child : plan->GetChildren()) {
    children.emplace_back(OptimizeNLJAsHashJoin(child));
  }

  auto optimized_plan = plan->CloneWithChildren(std::move(children));

  if (optimized_plan->GetType() == PlanType::NestedLoopJoin) {
    const auto &nlj_plan = dynamic_cast<const NestedLoopJoinPlanNode &>(*optimized_plan);

    BUSTUB_ASSERT(!optimized_plan->children_.empty(), "nested loop join plan must have children");

    auto expr = nlj_plan.Predicate();
    if (expr.get() != nullptr) {
      auto left_keys = std::vector<AbstractExpressionRef>{};
      auto right_keys = std::vector<AbstractExpressionRef>{};

      if (ResolveNLJToHashKey(expr, &left_keys, &right_keys)) {
        if (!left_keys.empty() && !right_keys.empty()) {
          return std::make_shared<HashJoinPlanNode>(nlj_plan.output_schema_, nlj_plan.GetLeftPlan(),
                                                    nlj_plan.GetRightPlan(), std::move(left_keys),
                                                    std::move(right_keys), nlj_plan.GetJoinType());
        }
      }
    }
  }
  return optimized_plan;
}

}  // namespace bustub
