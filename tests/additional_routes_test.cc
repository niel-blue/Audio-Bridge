#include <array>
#include "additional_routes.h"
#include "output_delay.h"

int main() {
  using namespace audio_bridge::audio;
  const std::array<float, 4> host{1, -1, 0.5f, 0};
  const std::array<float, 4> extra{0.5f, 0.5f, -0.25f, 0};
  const std::array<float, 4> silent{};
  std::array<float, 4> out{}, external{}, right{};

  auto result = RouteSelectedInputBlock(host.data(), host.data(), nullptr,
      nullptr, 2, 0, out.data(), right.data(), external.data(), nullptr, 4);
  if (out != std::array<float, 4>{2, -2, 1, 0} || right != out ||
      external != std::array<float, 4>{}) return 1;

  RouteSelectedInputBlock(host.data(), host.data(), nullptr, nullptr, 2, 3,
      out.data(), right.data(), nullptr, nullptr, 4);
  if (out != std::array<float, 4>{6, -6, 3, 0} || right != out) return 7;

  RouteSelectedInputBlock(host.data(), host.data(), extra.data(), extra.data(),
      2, 3, out.data(), right.data(), nullptr, nullptr, 4);
  if (out != std::array<float, 4>{3, 3, -1.5f, 0} || right != out) return 8;

  OutputDelay host_delay;
  host_delay.Prepare(1000, 20);
  host_delay.Process(out.data(), right.data(), 4, 2);
  if (out != std::array<float, 4>{0, 0, 3, 3}) return 9;

  auto selected_left = host;
  auto selected_right = host;
  std::array<float, 4> delayed_out{}, delayed_right{};
  OutputDelay input_delay;
  input_delay.Prepare(1000, 20);
  input_delay.Process(selected_left.data(), selected_right.data(), 4, 2);
  RouteSelectedInputBlock(selected_left.data(), selected_right.data(), nullptr,
      nullptr, 2, 1, delayed_out.data(), delayed_right.data(), nullptr,
      nullptr, 4);
  if (delayed_out != std::array<float, 4>{0, 0, 2, -2} ||
      delayed_right != delayed_out) return 10;

  RouteSelectedInputBlock(nullptr, nullptr, extra.data(), extra.data(), 2,
      0.5f, out.data(), right.data(), external.data(), nullptr, 4);
  if (out != std::array<float, 4>{1, 1, -0.5f, 0} ||
      external != extra) return 2;

  RouteSelectedInputBlock(host.data(), host.data(), silent.data(), silent.data(),
      1, 1, out.data(), right.data(), nullptr, nullptr, 4);
  if (out != silent || right != silent) return 3;

  OutputDelay delay;
  delay.Prepare(1000, 20);
  auto external_right = external;
  delay.Process(external.data(), external_right.data(), 4, 2);
  if (external != std::array<float, 4>{0, 0, 0.5f, 0.5f}) return 4;

  result = RouteSelectedInputBlock(nullptr, nullptr, nullptr, nullptr, 2, 1,
      out.data(), right.data(), nullptr, nullptr, 4);
  if (result.silence != 3 || result.peak != 0) return 5;

  auto aliased_host = host;
  RouteSelectedInputBlock(aliased_host.data(), aliased_host.data(),
      extra.data(), extra.data(), 1, 1, aliased_host.data(), right.data(),
      external.data(), nullptr, 4);
  if (aliased_host != extra || right != extra || external != extra) return 6;
  return 0;
}
