#include "io_state.h"

#include "parameters.h"

#include <algorithm>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>

namespace audio_bridge::state {
namespace {

constexpr std::uint32_t kCurrentVersion = 2U;
constexpr std::size_t kMaximumStateBytes = 1U << 22;
constexpr std::uint32_t kMaximumDeviceIdBytes = 1U << 20;

void Append(const void* const data, const std::size_t size,
            std::string& output) {
  output.append(static_cast<const char*>(data), size);
}

template <typename T>
void AppendValue(const T value, std::string& output) {
  Append(&value, sizeof(value), output);
}

auto Encode(const Data& value) -> std::string {
  auto result = std::string{};
  AppendValue(kCurrentVersion, result);
  AppendValue(value.input_gain_db, result);
  AppendValue(value.output_gain_db, result);
  AppendValue(value.output_delay_ms, result);
  AppendValue<std::uint8_t>(value.exclusive ? 1U : 0U, result);
  const auto append_string = [&result](const std::string& text) {
    AppendValue(static_cast<std::uint32_t>(text.size()), result);
    Append(text.data(), text.size(), result);
  };
  append_string(value.input_id);
  append_string(value.output_id);
  return result;
}

auto Decode(const std::string& bytes, Data& value) -> bool {
  std::size_t offset = 0U;
  const auto read = [&bytes, &offset](void* const destination,
                                      const std::size_t size) -> bool {
    if (offset > bytes.size() || size > bytes.size() - offset) {
      return false;
    }
    if (size > 0U) {
      std::memcpy(destination, bytes.data() + offset, size);
    }
    offset += size;
    return true;
  };

  std::uint32_t version = 0U;
  std::uint8_t exclusive = 0U;
  if (!read(&version, sizeof(version)) ||
      (version != 1U && version != kCurrentVersion) ||
      !read(&value.input_gain_db, sizeof(value.input_gain_db)) ||
      !read(&value.output_gain_db, sizeof(value.output_gain_db)) ||
      (version >= 2U &&
       !read(&value.output_delay_ms, sizeof(value.output_delay_ms))) ||
      !read(&exclusive, sizeof(exclusive))) {
    return false;
  }
  if (version == 1U) {
    value.output_delay_ms = 0.0;
  }

  const auto read_string = [&read](std::string& destination) -> bool {
    std::uint32_t size = 0U;
    if (!read(&size, sizeof(size)) || size > kMaximumDeviceIdBytes) {
      return false;
    }
    destination.resize(size);
    return size == 0U || read(destination.data(), size);
  };
  if (!read_string(value.input_id) || !read_string(value.output_id)) {
    return false;
  }
  value.exclusive = exclusive != 0U;
  return true;
}

}  // namespace

auto Sanitize(Data value) -> Data {
  value.input_gain_db =
      std::clamp(value.input_gain_db, vst::kMinimumGainDb,
                 vst::kMaximumGainDb);
  value.output_gain_db =
      std::clamp(value.output_gain_db, vst::kMinimumGainDb,
                 vst::kMaximumGainDb);
  value.output_delay_ms =
      std::clamp(value.output_delay_ms, 0.0, vst::kMaximumOutputDelayMs);
  return value;
}

auto Read(Steinberg::IBStream* const stream, Data& value) -> bool {
  std::int32_t size = 0;
  if (stream == nullptr ||
      stream->read(&size, sizeof(size)) != Steinberg::kResultTrue || size < 0 ||
      static_cast<std::size_t>(size) > kMaximumStateBytes) {
    return false;
  }
  auto bytes = std::string(static_cast<std::size_t>(size), '\0');
  if (size > 0 &&
      stream->read(bytes.data(), static_cast<std::uint32_t>(size)) !=
          Steinberg::kResultTrue) {
    return false;
  }
  auto decoded = Data{};
  if (!Decode(bytes, decoded)) {
    return false;
  }
  value = Sanitize(std::move(decoded));
  return true;
}

auto Write(Steinberg::IBStream* const stream, const Data& value) -> bool {
  const auto bytes = Encode(Sanitize(value));
  if (stream == nullptr || bytes.size() > static_cast<std::size_t>(INT32_MAX)) {
    return false;
  }
  auto size = static_cast<std::int32_t>(bytes.size());
  if (stream->write(&size, sizeof(size)) != Steinberg::kResultTrue) {
    return false;
  }
  return bytes.empty() ||
         stream->write(const_cast<char*>(bytes.data()),
                       static_cast<std::uint32_t>(bytes.size())) ==
             Steinberg::kResultTrue;
}

}  // namespace audio_bridge::state
