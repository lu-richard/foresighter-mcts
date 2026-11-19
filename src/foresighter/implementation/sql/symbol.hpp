/**
 * @file symbol.hpp
 * @brief
 * @author Rui Dong
 * @date 4/17/24
 */

#pragma once
#include <cereal/access.hpp>
#include <foresighter/common/hash.hpp>
#include <foresighter/implementation/sql/symbol/const_column_index.hpp>
#include <foresighter/implementation/sql/symbol/const_count_window_function.hpp>
#include <foresighter/implementation/sql/symbol/const_max_window_function.hpp>
#include <foresighter/implementation/sql/symbol/const_min_window_function.hpp>
#include <foresighter/implementation/sql/symbol/const_sum_window_function.hpp>
#include <foresighter/implementation/sql/symbol/nt_aggregation.hpp>
#include <foresighter/implementation/sql/symbol/nt_aggregation_list.hpp>
#include <foresighter/implementation/sql/symbol/nt_column_index.hpp>
#include <foresighter/implementation/sql/symbol/nt_group_by_list.hpp>
#include <foresighter/implementation/sql/symbol/nt_join_predicate.hpp>
#include <foresighter/implementation/sql/symbol/nt_join_predicate_clause.hpp>
#include <foresighter/implementation/sql/symbol/nt_order_key.hpp>
#include <foresighter/implementation/sql/symbol/nt_projection_list.hpp>
#include <foresighter/implementation/sql/symbol/nt_query.hpp>
#include <foresighter/implementation/sql/symbol/nt_select_predicate.hpp>
#include <foresighter/implementation/sql/symbol/nt_select_predicate_clause.hpp>
#include <foresighter/implementation/sql/symbol/nt_select_prim.hpp>
#include <foresighter/implementation/sql/symbol/nt_window_clause.hpp>
#include <foresighter/implementation/sql/symbol/nt_window_function.hpp>
#include <foresighter/implementation/sql/symbol/nt_window_function_list.hpp>
#include <foresighter/implementation/sql/symbol/op_asc.hpp>
#include <foresighter/implementation/sql/symbol/op_avg.hpp>
#include <foresighter/implementation/sql/symbol/op_count.hpp>
#include <foresighter/implementation/sql/symbol/op_desc.hpp>
#include <foresighter/implementation/sql/symbol/op_filter_and.hpp>
#include <foresighter/implementation/sql/symbol/op_filter_eq.hpp>
#include <foresighter/implementation/sql/symbol/op_filter_geq.hpp>
#include <foresighter/implementation/sql/symbol/op_filter_gt.hpp>
#include <foresighter/implementation/sql/symbol/op_filter_leq.hpp>
#include <foresighter/implementation/sql/symbol/op_filter_like.hpp>
#include <foresighter/implementation/sql/symbol/op_filter_lt.hpp>
#include <foresighter/implementation/sql/symbol/op_filter_neq.hpp>
#include <foresighter/implementation/sql/symbol/op_filter_or.hpp>
#include <foresighter/implementation/sql/symbol/op_group_with_key.hpp>
#include <foresighter/implementation/sql/symbol/op_group_without_key.hpp>
#include <foresighter/implementation/sql/symbol/op_identity.hpp>
#include <foresighter/implementation/sql/symbol/op_index_in_interval.hpp>
#include <foresighter/implementation/sql/symbol/op_inner_join.hpp>
#include <foresighter/implementation/sql/symbol/op_is_not_null.hpp>
#include <foresighter/implementation/sql/symbol/op_is_null.hpp>
#include <foresighter/implementation/sql/symbol/op_join_and.hpp>
#include <foresighter/implementation/sql/symbol/op_join_eq.hpp>
#include <foresighter/implementation/sql/symbol/op_left_join.hpp>
#include <foresighter/implementation/sql/symbol/op_max.hpp>
#include <foresighter/implementation/sql/symbol/op_min.hpp>
#include <foresighter/implementation/sql/symbol/op_projection.hpp>
#include <foresighter/implementation/sql/symbol/op_projection_distinct.hpp>
#include <foresighter/implementation/sql/symbol/op_rank_no_partition_order.hpp>
#include <foresighter/implementation/sql/symbol/op_rank_with_partition_order.hpp>
#include <foresighter/implementation/sql/symbol/op_select.hpp>
#include <foresighter/implementation/sql/symbol/op_sum.hpp>
#include <foresighter/implementation/sql/symbol/op_window.hpp>
#include <foresighter/implementation/sql/symbol/op_window_function_no_partition_order.hpp>
#include <foresighter/implementation/sql/symbol/op_window_function_with_partition_order.hpp>
#include <foresighter/implementation/sql/symbol/var_filter_const.hpp>
#include <foresighter/implementation/sql/symbol/var_table.hpp>
#include <variant>

#include <cereal/types/variant.hpp>
#include "foresighter/implementation/sql/symbol/const_column_index.hpp"
#include "foresighter/implementation/sql/symbol/nt_column_index_interval.hpp"
#include "foresighter/implementation/sql/symbol/nt_filter_constant.hpp"
#include "foresighter/implementation/sql/symbol/nt_order_by_list.hpp"
#include "foresighter/implementation/sql/symbol/nt_ordered_group_by_list.hpp"
#include "foresighter/implementation/sql/symbol/op_agg_list.hpp"
#include "foresighter/implementation/sql/symbol/op_count_distinct.hpp"
#include "foresighter/implementation/sql/symbol/op_filter_like.hpp"
#include "foresighter/implementation/sql/symbol/op_group_by_list.hpp"
#include "foresighter/implementation/sql/symbol/op_group_by_order_by_count_limit.hpp"
#include "foresighter/implementation/sql/symbol/op_order_by_asc.hpp"
#include "foresighter/implementation/sql/symbol/op_order_by_col_limit.hpp"
#include "foresighter/implementation/sql/symbol/op_order_by_desc.hpp"
#include "foresighter/implementation/sql/symbol/op_proj_list.hpp"
#include "foresighter/implementation/sql/symbol/op_window_list.hpp"

#include <foresighter/implementation/basics/symbol.hpp>

namespace foresighter::implementation::sql {
using Symbol = foresighter::implementation::basics::Symbol<
    ConstColumnIndex, VarFilterConst, ConstCountWindowFunction,
    ConstMaxWindowFunction, ConstMinWindowFunction, ConstSumWindowFunction,
    NtAggregation, NtAggregationList, NtColumnIndex, NtColumnIndexInterval,
    NtFilterConstant, NtProjectionList, NtGroupByList, NtOrderedGroupByList,
    NtJoinPredicate, NtJoinPredicateClause, NtOrderKey, NtQuery,
    NtSelectPredicate, NtSelectPredicateClause, NtSelectPrim, NtWindowClause,
    NtWindowFunction, NtWindowFunctionList, OpAsc, OpAvg, OpCount,
    OpCountDistinct, OpDesc, OpFilterAnd, OpFilterEq, OpFilterGeq, OpFilterGt,
    OpFilterLeq, OpFilterLt, OpFilterLike, OpFilterNeq, OpFilterOr,
    OpGroupWithKey, OpGroupWithoutKey, OpIdentity, OpIndexInInterval,
    OpIsNotNull, OpIsNull, OpInnerJoin, OpLeftJoin, OpJoinAnd, OpJoinEq,
    OpAggList, OpProjList, OpGroupByList, OpWindowList, OpMax, OpMin,
    OpProjection, OpProjectionDistinct, OpGroupByOrderByCountLimit,
    OpRankNoPartitionOrder, OpRankWithPartitionOrder, OpSelect, OpSum, OpWindow,
    OpWindowFunctionNoPartitionOrder, OpWindowFunctionWithPartitionOrder,
    VarTable, OpOrderByColLimit, NtOrderByList, OpOrderByAsc, OpOrderByDesc>;

}  // namespace foresighter::implementation::sql