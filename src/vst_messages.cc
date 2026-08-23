#include "vst_messages.h"

#include <utility>

namespace audio_bridge::vst::messages {
namespace {

using Steinberg::kResultTrue;

auto WriteBinary(Steinberg::Vst::IAttributeList* const attributes,
                 const char* const name, const std::string& value) -> bool {
  return attributes->setBinary(
             name, value.data(),
             static_cast<Steinberg::uint32>(value.size())) == kResultTrue;
}

auto ReadBinary(Steinberg::Vst::IAttributeList* const attributes,
                const char* const name, std::string& value,
                const bool required) -> bool {
  Steinberg::uint32 size = 0U;
  const void* data = nullptr;
  const auto result = attributes->getBinary(name, data, size);
  if (result != kResultTrue) {
    value.clear();
    return !required;
  }
  if (data == nullptr || size == 0U) {
    value.clear();
    return true;
  }
  value.assign(static_cast<const char*>(data), size);
  return true;
}

}  // namespace

auto WriteState(Steinberg::Vst::IAttributeList* const attributes,
                const state::Data& value,
                const bool include_devices) -> bool {
  if (attributes == nullptr ||
      attributes->setFloat(kInputGainDb, value.input_gain_db) != kResultTrue ||
      attributes->setFloat(kOutputGainDb, value.output_gain_db) !=
          kResultTrue ||
      attributes->setFloat(kOutputDelayMs, value.output_delay_ms) !=
          kResultTrue ||
      attributes->setInt(kExclusive, value.exclusive ? 1 : 0) !=
          kResultTrue) {
    return false;
  }
  return !include_devices ||
         (WriteBinary(attributes, kInputId, value.input_id) &&
          WriteBinary(attributes, kOutputId, value.output_id));
}

auto ReadState(Steinberg::Vst::IAttributeList* const attributes,
               state::Data& value, const bool include_devices) -> bool {
  Steinberg::int64 exclusive = 0;
  auto decoded = state::Data{};
  if (attributes == nullptr ||
      attributes->getFloat(kInputGainDb, decoded.input_gain_db) !=
          kResultTrue ||
      attributes->getFloat(kOutputGainDb, decoded.output_gain_db) !=
          kResultTrue ||
      attributes->getFloat(kOutputDelayMs, decoded.output_delay_ms) !=
          kResultTrue ||
      attributes->getInt(kExclusive, exclusive) != kResultTrue) {
    return false;
  }
  decoded.exclusive = exclusive != 0;
  if (include_devices &&
      (!ReadBinary(attributes, kInputId, decoded.input_id, false) ||
       !ReadBinary(attributes, kOutputId, decoded.output_id, false))) {
    return false;
  }
  value = state::Sanitize(std::move(decoded));
  return true;
}

auto WriteRouting(Steinberg::Vst::IAttributeList* const attributes,
                  const std::string& input_id, const std::string& output_id,
                  const bool exclusive) -> bool {
  return attributes != nullptr &&
         WriteBinary(attributes, kInputId, input_id) &&
         WriteBinary(attributes, kOutputId, output_id) &&
         attributes->setInt(kExclusive, exclusive ? 1 : 0) == kResultTrue;
}

auto ReadRouting(Steinberg::Vst::IAttributeList* const attributes,
                 std::string& input_id, std::string& output_id,
                 bool& exclusive) -> bool {
  Steinberg::int64 encoded_exclusive = 0;
  if (attributes == nullptr ||
      !ReadBinary(attributes, kInputId, input_id, true) ||
      !ReadBinary(attributes, kOutputId, output_id, true) ||
      attributes->getInt(kExclusive, encoded_exclusive) != kResultTrue) {
    return false;
  }
  exclusive = encoded_exclusive != 0;
  return true;
}

}  // namespace audio_bridge::vst::messages
