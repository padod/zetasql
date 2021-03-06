//
// Copyright 2019 ZetaSQL Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#ifndef ZETASQL_PUBLIC_NUMERIC_VALUE_INL_H_
#define ZETASQL_PUBLIC_NUMERIC_VALUE_INL_H_



#include <cmath>
#include <limits>
#include <string>
#include <type_traits>

#include "zetasql/common/errors.h"
#include "absl/base/config.h"
#include "absl/base/optimization.h"
#include "absl/strings/str_cat.h"
#include "zetasql/base/mathutil.h"
#include "zetasql/base/canonical_errors.h"
#include "zetasql/base/statusor.h"

namespace zetasql {

namespace internal {

constexpr uint32_t k1e9 = 1000000000U;
constexpr uint64_t k1e19 = static_cast<uint64_t>(k1e9) * k1e9 * 10;
constexpr __int128 k1e38 = static_cast<__int128>(k1e19) * k1e19;
constexpr __int128 kNumericMax = k1e38 - 1;
constexpr __int128 kNumericMin = -kNumericMax;
constexpr uint32_t k5to12 = 244140625;
constexpr uint32_t k5to13 = 1220703125;
constexpr std::integral_constant<int32_t, internal::k1e9> kSignedScalingFactor{};

// Adds two __int128 values and returns true if there was an overflow; otherwise
// returns false.
inline bool int128_add_overflow(__int128 a, __int128 b, __int128* res) {
#if ABSL_HAVE_BUILTIN(__builtin_add_overflow)
  return __builtin_add_overflow(a, b, res);
#else
  *res = static_cast<__int128>(
      static_cast<unsigned __int128>(a) + static_cast<unsigned __int128>(b));
  return (a < 0 && b < 0 && *res > 0) || (a > 0 && b > 0 && *res < 0);
#endif
}

}  // namespace internal

inline NumericValue::NumericValue(uint64_t high_bits, uint64_t low_bits)
    : high_bits_(high_bits), low_bits_(low_bits) {}

inline constexpr NumericValue::NumericValue(__int128 value)
    : high_bits_(static_cast<__int128>(value) >> 64),
      low_bits_(value & std::numeric_limits<uint64_t>::max()) {}

inline constexpr NumericValue::NumericValue()
    : NumericValue(static_cast<__int128>(0)) {}

inline constexpr NumericValue::NumericValue(int value)
    : NumericValue(static_cast<__int128>(value) * kScalingFactor) {}

inline constexpr NumericValue::NumericValue(unsigned int value)
    : NumericValue(static_cast<__int128>(value) * kScalingFactor) {}

inline constexpr NumericValue::NumericValue(long value)  // NOLINT(runtime/int)
    : NumericValue(static_cast<__int128>(value) * kScalingFactor) {}

// NOLINTNEXTLINE(runtime/int)
inline constexpr NumericValue::NumericValue(unsigned long value)
    : NumericValue(static_cast<__int128>(value) * kScalingFactor) {}

// NOLINTNEXTLINE(runtime/int)
inline constexpr NumericValue::NumericValue(long long value)
    : NumericValue(static_cast<__int128>(value) * kScalingFactor) {}

// NOLINTNEXTLINE(runtime/int)
inline constexpr NumericValue::NumericValue(unsigned long long value)
    : NumericValue(static_cast<__int128>(value) * kScalingFactor) {}

inline constexpr NumericValue NumericValue::MaxValue() {
  return NumericValue(internal::kNumericMax);
}

inline constexpr NumericValue NumericValue::MinValue() {
  return NumericValue(internal::kNumericMin);
}

inline zetasql_base::StatusOr<NumericValue> NumericValue::FromPackedInt(
    __int128 value) {
  NumericValue ret(value);

  if (ABSL_PREDICT_FALSE(ret < MinValue() || ret > MaxValue())) {
    return MakeEvalError() << "numeric overflow: result out of range";
  }

  return ret;
}

template <int kNumBitsPerWord, int kNumWords>
inline zetasql_base::StatusOr<NumericValue> NumericValue::FromFixedInt(
    const FixedInt<kNumBitsPerWord, kNumWords>& val) {
  constexpr FixedInt<kNumBitsPerWord, kNumWords> kMin(internal::kNumericMin);
  constexpr FixedInt<kNumBitsPerWord, kNumWords> kMax(internal::kNumericMax);
  if (ABSL_PREDICT_TRUE(val >= kMin) && ABSL_PREDICT_TRUE(val <= kMax)) {
    return NumericValue(static_cast<__int128>(val));
  }
  return MakeEvalError() << "numeric overflow";
}

inline zetasql_base::StatusOr<NumericValue> NumericValue::FromHighAndLowBits(
    uint64_t high_bits, uint64_t low_bits) {
  NumericValue ret(high_bits, low_bits);

  if (ABSL_PREDICT_FALSE(ret < MinValue() || ret > MaxValue())) {
    return MakeEvalError() << "numeric overflow: result out of range";
  }

  return ret;
}

inline zetasql_base::StatusOr<NumericValue> NumericValue::Add(NumericValue rh) const {
  FixedInt<64, 2> sum(as_packed_int());
  bool overflow = sum.AddOverflow(FixedInt<64, 2>(rh.as_packed_int()));
  if (ABSL_PREDICT_TRUE(!overflow)) {
    auto numeric_value_status = FromFixedInt(sum);
    if (ABSL_PREDICT_TRUE(numeric_value_status.ok())) {
      return numeric_value_status;
    }
  }
  return MakeEvalError() << "numeric overflow: " << ToString() << " + "
                         << rh.ToString();
}

inline zetasql_base::StatusOr<NumericValue> NumericValue::Subtract(
    NumericValue rh) const {
  FixedInt<64, 2> diff(as_packed_int());
  bool overflow = diff.SubtractOverflow(FixedInt<64, 2>(rh.as_packed_int()));
  if (ABSL_PREDICT_TRUE(!overflow)) {
    auto numeric_value_status = FromFixedInt(diff);
    if (ABSL_PREDICT_TRUE(numeric_value_status.ok())) {
      return numeric_value_status;
    }
  }
  return MakeEvalError() << "numeric overflow: " << ToString() << " - "
                         << rh.ToString();
}

inline NumericValue NumericValue::UnaryMinus(NumericValue value) {
  // The result is expected to be within the valid range.
  return NumericValue(-value.as_packed_int());
}

inline bool NumericValue::operator==(NumericValue rh) const {
  return as_packed_int() == rh.as_packed_int();
}

inline bool NumericValue::operator!=(NumericValue rh) const {
  return as_packed_int() != rh.as_packed_int();
}

inline bool NumericValue::operator<(NumericValue rh) const {
  return as_packed_int() < rh.as_packed_int();
}

inline bool NumericValue::operator>(NumericValue rh) const {
  return as_packed_int() > rh.as_packed_int();
}

inline bool NumericValue::operator>=(NumericValue rh) const {
  return as_packed_int() >= rh.as_packed_int();
}

inline bool NumericValue::operator<=(NumericValue rh) const {
  return as_packed_int() <= rh.as_packed_int();
}

inline std::string NumericValue::ToString() const {
  std::string result;
  AppendToString(&result);
  return result;
}

template <typename H>
inline H AbslHashValue(H h, const NumericValue& v) {
  return H::combine(std::move(h), v.high_bits_, v.low_bits_);
}

template <typename T>
inline std::string TypeName();

template <>
inline std::string TypeName<int32_t>() {
  return "int32";
}

template <>
inline std::string TypeName<uint32_t>() {
  return "uint32";
}

template <>
inline std::string TypeName<int64_t>() {
  return "int64";
}

template <>
inline std::string TypeName<uint64_t>() {
  return "uint64";
}

template<class T>
inline zetasql_base::StatusOr<T> NumericValue::To() const {
  static_assert(std::is_same<T, int32_t>::value ||
                std::is_same<T, int64_t>::value ||
                std::is_same<T, uint32_t>::value ||
                std::is_same<T, uint64_t>::value,
                "In NumericValue::To<T>() T can only be one of "
                "int32, int64_t, uint32_t or uint64");

  __int128 rounded_value =
      static_cast<__int128>(FixedInt<64, 2>(as_packed_int())
          .DivAndRoundAwayFromZero(internal::kSignedScalingFactor));
  T result = static_cast<T>(rounded_value);
  if (rounded_value == result) {
    return result;
  }
  return MakeEvalError() << TypeName<T>() << " out of range: " << ToString();
}

inline constexpr __int128 NumericValue::as_packed_int() const {
  return (static_cast<__int128>(high_bits_) << 64) + low_bits_;
}

inline constexpr uint64_t NumericValue::high_bits() const {
  return high_bits_;
}

inline constexpr uint64_t NumericValue::low_bits() const {
  return low_bits_;
}

inline int32_t NumericValue::GetFractionalPart() const {
  int32_t remainder;
  FixedInt<64, 2>(as_packed_int())
      .DivMod(internal::kSignedScalingFactor, nullptr, &remainder);
  return remainder;
}

inline bool NumericValue::has_fractional_part() const {
  return GetFractionalPart() != 0;
}

inline void NumericValue::Aggregator::Add(NumericValue value) {
  const __int128 v = value.as_packed_int();
  if (ABSL_PREDICT_FALSE(internal::int128_add_overflow(
      sum_lower_, v, &sum_lower_))) {
    sum_upper_ += v < 0 ? -1 : 1;
  }
}

inline void NumericValue::SumAggregator::Add(NumericValue value) {
  sum_ += FixedInt<64, 3>(value.as_packed_int());
}

}  // namespace zetasql

#endif  // ZETASQL_PUBLIC_NUMERIC_VALUE_INL_H_
