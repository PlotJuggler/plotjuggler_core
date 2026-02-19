#pragma once
#include "pj/base/types.hpp"
#include "pj/engine/derived_engine.hpp"

namespace pj::engine {

// ---------------------------------------------------------------------------
// DerivativeTransform
// ---------------------------------------------------------------------------
// Numerical derivative: d(value)/d(t) in units/second.
// Skips the first row (no previous sample). Assumes float64 input/output.
class DerivativeTransform : public ISISOTransform {
  pj::Timestamp prev_time_ = 0;
  double prev_value_ = 0.0;
  bool has_prev_ = false;

 public:
  void reset() override;
  StorageKind output_kind(StorageKind input_kind) const override;
  bool calculate(pj::Timestamp time, const VarValue& input, pj::Timestamp& out_time, VarValue& out_value) override;
};

}  // namespace pj::engine
