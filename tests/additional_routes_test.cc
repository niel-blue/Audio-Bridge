#include <array>
#include "additional_routes.h"
#include "output_delay.h"
int main() {
  using namespace audio_bridge::audio;
  std::array<float, 4> host{1, -1, 0.5f, 0}, extra{0.5f, 0.5f, -0.25f, 0};
  std::array<float, 4> out{}, external{}, right{};
  auto result = RouteAdditionalBlock(host.data(), host.data(), nullptr, nullptr,
      2, 0, out.data(), right.data(), external.data(), nullptr, 4);
  if (out != std::array<float, 4>{2, -2, 1, 0} || right != out ||
      external != std::array<float, 4>{}) return 1;
  RouteAdditionalBlock(host.data(), host.data(), nullptr, nullptr, 2, 3,
      out.data(), right.data(), nullptr, nullptr, 4);
  if (out != std::array<float, 4>{6, -6, 3, 0} || right != out) return 7;
  RouteAdditionalBlock(host.data(), host.data(), extra.data(), extra.data(), 2, 3,
      out.data(), right.data(), nullptr, nullptr, 4);
  if (out != std::array<float, 4>{6, 0, 0, 0}) return 8;
  OutputDelay host_delay; host_delay.Prepare(1000, 20);
  host_delay.Process(out.data(), right.data(), 4, 2);
  if (out != std::array<float, 4>{0, 0, 6, 0}) return 9;
  RouteAdditionalBlock(nullptr, nullptr, extra.data(), extra.data(), 2, 0.5f,
      out.data(), right.data(), external.data(), nullptr, 4);
  if (out != std::array<float, 4>{1, 1, -0.5f, 0} || external != extra) return 2;
  RouteAdditionalBlock(host.data(), host.data(), extra.data(), extra.data(), 2,
      0.5f, out.data(), right.data(), external.data(), nullptr, 4);
  if (out != std::array<float, 4>{2, 0, 0, 0} || external[0] != 1) return 3;
  OutputDelay delay; delay.Prepare(1000, 20);
  auto external_right = external;
  delay.Process(external.data(), external_right.data(), 4, 2);
  if (external != std::array<float, 4>{0, 0, 1, 0} || out[0] != 2) return 4;
  result = RouteAdditionalBlock(nullptr, nullptr, nullptr, nullptr, 2, 1,
      out.data(), right.data(), nullptr, nullptr, 4);
  if (result.silence != 3 || result.peak != 0) return 5;
  RouteAdditionalBlock(host.data(), host.data(), extra.data(), extra.data(), 1,
      1, host.data(), right.data(), external.data(), nullptr, 4);
  if (host != std::array<float, 4>{1.5f, -0.5f, 0.25f, 0} || external != host) return 6;
  return 0;
}
