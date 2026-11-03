#include "HeightMap.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numeric>
#include <random>
#include <thread>
#include <vector>

#include <spdlog/spdlog.h>

namespace
{

class PerlinNoise
{
public:
  explicit PerlinNoise(uint32_t seed)
  {
    std::iota(permutation.begin(), permutation.begin() + 256, 0);

    std::mt19937 rng{seed};
    std::shuffle(permutation.begin(), permutation.begin() + 256, rng);

    for (int i = 0; i < 256; ++i)
      permutation[256 + i] = permutation[i];
  }

  float noise(float x, float y) const
  {
    const float floorX = std::floor(x);
    const float floorY = std::floor(y);

    const int xi = static_cast<int>(floorX) & 255;
    const int yi = static_cast<int>(floorY) & 255;

    const float tx = x - floorX;
    const float ty = y - floorY;

    const float u = fade(tx);
    const float v = fade(ty);

    const int aa = permutation[permutation[xi] + yi];
    const int ab = permutation[permutation[xi] + yi + 1];
    const int ba = permutation[permutation[xi + 1] + yi];
    const int bb = permutation[permutation[xi + 1] + yi + 1];

    const float x1 = lerp(grad(aa, tx, ty), grad(ba, tx - 1.0f, ty), u);
    const float x2 = lerp(grad(ab, tx, ty - 1.0f), grad(bb, tx - 1.0f, ty - 1.0f), u);

    return lerp(x1, x2, v);
  }

  float fbm(float x, float y, int octaves, float lacunarity, float gain) const
  {
    float sum = 0.0f;
    float amplitude = 1.0f;
    float frequency = 1.0f;
    float normalization = 0.0f;

    for (int i = 0; i < octaves; ++i)
    {
      sum += amplitude * noise(x * frequency, y * frequency);
      normalization += amplitude;
      amplitude *= gain;
      frequency *= lacunarity;
    }

    return sum / normalization;
  }

private:
  static float fade(float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); }

  static float lerp(float a, float b, float t) { return a + t * (b - a); }

  static float grad(int hash, float x, float y)
  {
    switch (hash & 7)
    {
    case 0:
      return x + y;
    case 1:
      return -x + y;
    case 2:
      return x - y;
    case 3:
      return -x - y;
    case 4:
      return x;
    case 5:
      return -x;
    case 6:
      return y;
    default:
      return -y;
    }
  }

  std::array<int, 512> permutation{};
};

}

void generateHeightMap(float* data, uint32_t resolution)
{
  constexpr int kOctaves = 6;
  constexpr float kLacunarity = 2.0f;
  constexpr float kGain = 0.5f;
  constexpr float kBaseFrequency = 4.0f;

  spdlog::info("Generating {}x{} Perlin height map...", resolution, resolution);

  const PerlinNoise noise{1337};

  const unsigned int hardwareThreads = std::thread::hardware_concurrency();
  const uint32_t threadCount = std::clamp(hardwareThreads == 0 ? 4u : hardwareThreads, 1u, 16u);

  std::vector<std::thread> threads;
  threads.reserve(threadCount);

  for (uint32_t threadIndex = 0; threadIndex < threadCount; ++threadIndex)
  {
    threads.emplace_back([&, threadIndex]() {
      for (uint32_t y = threadIndex; y < resolution; y += threadCount)
      {
        const float v = static_cast<float>(y) / static_cast<float>(resolution);

        for (uint32_t x = 0; x < resolution; ++x)
        {
          const float u = static_cast<float>(x) / static_cast<float>(resolution);

          const float value = noise.fbm(
            u * kBaseFrequency + 17.3f, v * kBaseFrequency + 9.1f, kOctaves, kLacunarity, kGain);

          data[static_cast<std::size_t>(y) * resolution + x] = value;
        }
      }
    });
  }

  for (auto& thread : threads)
    thread.join();

  const std::size_t texelCount = static_cast<std::size_t>(resolution) * resolution;

  float minValue = std::numeric_limits<float>::max();
  float maxValue = std::numeric_limits<float>::lowest();

  for (std::size_t i = 0; i < texelCount; ++i)
  {
    minValue = std::min(minValue, data[i]);
    maxValue = std::max(maxValue, data[i]);
  }

  const float range = std::max(maxValue - minValue, 1e-6f);
  for (std::size_t i = 0; i < texelCount; ++i)
    data[i] = (data[i] - minValue) / range;

  spdlog::info("Height map generated (range was [{}, {}])", minValue, maxValue);
}
