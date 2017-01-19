/******************************************************************************
 *                       ____    _    _____                                   *
 *                      / ___|  / \  |  ___|    C++                           *
 *                     | |     / _ \ | |_       Actor                         *
 *                     | |___ / ___ \|  _|      Framework                     *
 *                      \____/_/   \_|_|                                      *
 *                                                                            *
 * Copyright (C) 2011 - 2016                                                  *
 * Dominik Charousset <dominik.charousset (at) haw-hamburg.de>                *
 *                                                                            *
 * Distributed under the terms and conditions of the BSD 3-Clause License or  *
 * (at your option) under the terms and conditions of the Boost Software      *
 * License 1.0. See accompanying files LICENSE and LICENSE_ALTERNATIVE.       *
 *                                                                            *
 * If you did not receive a copy of the license files, see                    *
 * http://opensource.org/licenses/BSD-3-Clause and                            *
 * http://www.boost.org/LICENSE_1_0.txt.                                      *
 ******************************************************************************/

#include <utility>

#include "caf/abstract_downstream.hpp"

#include "caf/send.hpp"
#include "caf/downstream_path.hpp"
#include "caf/downstream_policy.hpp"

namespace caf {

abstract_downstream::abstract_downstream(local_actor* selfptr,
                                         const stream_id& sid,
                                         std::unique_ptr<downstream_policy> ptr)
    : self_(selfptr),
      sid_(sid),
      policy_(std::move(ptr)) {
  // nop
}

abstract_downstream::~abstract_downstream() {
  // nop
}

size_t abstract_downstream::total_credit() const {
  auto f = [](size_t x, path_cref y) {
    // printf("f(%d, %d)\n", (int) x, (int) y.open_credit);
    return x + y.open_credit;
  };
  return fold_paths(0, f);
}

size_t abstract_downstream::max_credit() const {
  auto f = [](size_t x, path_cref y) { return std::max(x, y.open_credit); };
  return fold_paths(0, f);
}

size_t abstract_downstream::min_credit() const {
  auto f = [](size_t x, path_cref y) { return std::min(x, y.open_credit); };
  return fold_paths(std::numeric_limits<size_t>::max(), f);
}

bool abstract_downstream::add_path(strong_actor_ptr ptr,
                                   std::vector<atom_value> filter,
                                   bool redeployable) {
  auto predicate = [&](const path_uptr& x) { return x->ptr == ptr; };
  if (std::none_of(paths_.begin(), paths_.end(), predicate)) {
    paths_.emplace_back(
      new path(std::move(ptr), std::move(filter), redeployable));
    recalculate_active_filters();
    return true;
  }
  return false;
}

bool abstract_downstream::remove_path(strong_actor_ptr& ptr) {
  auto predicate = [&](const path_uptr& x) { return x->ptr == ptr; };
  auto e = paths_.end();
  auto i = std::find_if(paths_.begin(), e, predicate);
  if (i != e) {
    CAF_ASSERT((*i)->ptr != nullptr);
    if (i != paths_.end() - 1)
      std::swap(*i, paths_.back());
    auto x = std::move(paths_.back());
    paths_.pop_back();
    unsafe_send_as(self_, x->ptr, make<stream_msg::close>(sid_));
    //policy_->reclaim(this, x);
    return true;
  }
  return false;
}

void abstract_downstream::close() {
  for (auto& x : paths_)
    unsafe_send_as(self_, x->ptr, make<stream_msg::close>(sid_));
  paths_.clear();
}

void abstract_downstream::abort(strong_actor_ptr& cause, const error& reason) {
  for (auto& x : paths_)
    if (x->ptr != cause)
      unsafe_send_as(self_, x->ptr,
                     make<stream_msg::abort>(this->sid_, reason));
}

auto abstract_downstream::find(const strong_actor_ptr& ptr) const
-> optional<path&> {
  auto predicate = [&](const path_uptr& y) {
    return y->ptr == ptr;
  };
  auto e = paths_.end();
  auto i = std::find_if(paths_.begin(), e, predicate);
  if (i != e)
    return *(*i);
  return none;
}

void abstract_downstream::recalculate_active_filters() {
  active_filters_.clear();
  for (auto& x : paths_)
    active_filters_.emplace(x->filter);
}

void abstract_downstream::send_batch(downstream_path& dest, size_t chunk_size,
                                     message chunk) {
  auto scs = static_cast<int32_t>(chunk_size);
  auto batch_id = dest.next_batch_id++;
  stream_msg::batch batch{scs, std::move(chunk), batch_id};
  if (dest.redeployable)
    dest.unacknowledged_batches.emplace_back(batch_id, batch);
  unsafe_send_as(self_, dest.ptr, stream_msg{sid_, std::move(batch)});
}

void abstract_downstream::sort_by_credit() {
  auto cmp = [](const path_uptr& x, const path_uptr& y) {
    return x->open_credit > y->open_credit;
  };
  std::sort(paths_.begin(), paths_.end(), cmp);
}

} // namespace caf
