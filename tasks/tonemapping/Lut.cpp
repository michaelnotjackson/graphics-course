#include "Lut.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <vector>

#include <etna/Buffer.hpp>
#include <etna/Etna.hpp>
#include <etna/GlobalContext.hpp>
#include <etna/OneShotCmdMgr.hpp>
#include <stb_image.h>


namespace
{

constexpr int kLutSize = 32;
constexpr int kLutCells = kLutSize * kLutSize * kLutSize;

struct CellCoord
{
  int x = -1;
  int y = -1;
  int z = -1;
};

int lutIndex(int r, int g, int b)
{
  return (b * kLutSize + g) * kLutSize + r;
}

}

etna::Image buildColorLut3D(const std::string& source_path, const std::string& graded_path)
{
  int srcW = 0, srcH = 0, srcC = 0;
  int dstW = 0, dstH = 0, dstC = 0;
  unsigned char* srcPixels = stbi_load(source_path.c_str(), &srcW, &srcH, &srcC, 4);
  unsigned char* dstPixels = stbi_load(graded_path.c_str(), &dstW, &dstH, &dstC, 4);
  if (!srcPixels || !dstPixels || srcW != dstW || srcH != dstH)
  {
    if (srcPixels)
      stbi_image_free(srcPixels);
    if (dstPixels)
      stbi_image_free(dstPixels);
    throw std::runtime_error("Failed to load LUT source/graded images");
  }

  std::vector<double> sumR(kLutCells, 0.0);
  std::vector<double> sumG(kLutCells, 0.0);
  std::vector<double> sumB(kLutCells, 0.0);
  std::vector<uint32_t> counts(kLutCells, 0u);

  const size_t pixelCount = static_cast<size_t>(srcW) * static_cast<size_t>(srcH);
  for (size_t i = 0; i < pixelCount; ++i)
  {
    const int r = srcPixels[i * 4 + 0] >> 3;
    const int g = srcPixels[i * 4 + 1] >> 3;
    const int b = srcPixels[i * 4 + 2] >> 3;
    const int idx = lutIndex(r, g, b);
    sumR[idx] += static_cast<double>(dstPixels[i * 4 + 0]);
    sumG[idx] += static_cast<double>(dstPixels[i * 4 + 1]);
    sumB[idx] += static_cast<double>(dstPixels[i * 4 + 2]);
    counts[idx] += 1u;
  }

  stbi_image_free(srcPixels);
  stbi_image_free(dstPixels);

  std::vector<uint8_t> lutBytes(static_cast<size_t>(kLutCells) * 4u, 255u);
  std::vector<CellCoord> seeds(kLutCells);
  std::vector<CellCoord> nextSeeds(kLutCells);

  for (int b = 0; b < kLutSize; ++b)
  {
    for (int g = 0; g < kLutSize; ++g)
    {
      for (int r = 0; r < kLutSize; ++r)
      {
        const int idx = lutIndex(r, g, b);
        if (counts[idx] > 0u)
        {
          const double inv = 1.0 / static_cast<double>(counts[idx]);
          lutBytes[idx * 4 + 0] = static_cast<uint8_t>(std::clamp(std::lround(sumR[idx] * inv), 0l, 255l));
          lutBytes[idx * 4 + 1] = static_cast<uint8_t>(std::clamp(std::lround(sumG[idx] * inv), 0l, 255l));
          lutBytes[idx * 4 + 2] = static_cast<uint8_t>(std::clamp(std::lround(sumB[idx] * inv), 0l, 255l));
          lutBytes[idx * 4 + 3] = 255u;
          seeds[idx] = CellCoord{r, g, b};
        }
      }
    }
  }

  constexpr std::array<int, 6> kSteps{16, 8, 4, 2, 1, 1};
  for (int step : kSteps)
  {
    for (int z = 0; z < kLutSize; ++z)
    {
      for (int y = 0; y < kLutSize; ++y)
      {
        for (int x = 0; x < kLutSize; ++x)
        {
          const int idx = lutIndex(x, y, z);
          CellCoord best = seeds[idx];
          int bestDist = std::numeric_limits<int>::max();
          if (best.x >= 0)
          {
            const int dx = x - best.x;
            const int dy = y - best.y;
            const int dz = z - best.z;
            bestDist = dx * dx + dy * dy + dz * dz;
          }

          for (int oz = -1; oz <= 1; ++oz)
          {
            const int nz = z + oz * step;
            if (nz < 0 || nz >= kLutSize)
              continue;
            for (int oy = -1; oy <= 1; ++oy)
            {
              const int ny = y + oy * step;
              if (ny < 0 || ny >= kLutSize)
                continue;
              for (int ox = -1; ox <= 1; ++ox)
              {
                const int nx = x + ox * step;
                if (nx < 0 || nx >= kLutSize)
                  continue;
                const CellCoord cand = seeds[lutIndex(nx, ny, nz)];
                if (cand.x < 0)
                  continue;
                const int dx = x - cand.x;
                const int dy = y - cand.y;
                const int dz = z - cand.z;
                const int dist = dx * dx + dy * dy + dz * dz;
                if (dist < bestDist)
                {
                  bestDist = dist;
                  best = cand;
                }
              }
            }
          }
          nextSeeds[idx] = best;
        }
      }
    }
    std::swap(seeds, nextSeeds);
  }

  for (int idx = 0; idx < kLutCells; ++idx)
  {
    if (counts[idx] == 0u && seeds[idx].x >= 0)
    {
      const int srcIdx = lutIndex(seeds[idx].x, seeds[idx].y, seeds[idx].z);
      lutBytes[idx * 4 + 0] = lutBytes[srcIdx * 4 + 0];
      lutBytes[idx * 4 + 1] = lutBytes[srcIdx * 4 + 1];
      lutBytes[idx * 4 + 2] = lutBytes[srcIdx * 4 + 2];
      lutBytes[idx * 4 + 3] = 255u;
    }
  }

  auto& ctx = etna::get_context();
  etna::Image lutImage = ctx.createImage(
    etna::Image::CreateInfo{
      .extent = vk::Extent3D{kLutSize, kLutSize, kLutSize},
      .name = "color_lut_3d",
      .format = vk::Format::eR8G8B8A8Unorm,
      .imageUsage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst,
      .type = vk::ImageType::e3D,
    });

  etna::Buffer staging = ctx.createBuffer(
    etna::Buffer::CreateInfo{
      .size = lutBytes.size(),
      .bufferUsage = vk::BufferUsageFlagBits::eTransferSrc,
      .memoryUsage = VMA_MEMORY_USAGE_AUTO,
      .allocationCreate =
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
      .name = "color_lut_staging",
    });
  staging.map();
  std::memcpy(staging.data(), lutBytes.data(), lutBytes.size());

  auto cmdMgr = ctx.createOneShotCmdMgr();
  auto cmd = cmdMgr->start();
  ETNA_CHECK_VK_RESULT(cmd.begin(vk::CommandBufferBeginInfo{}));
  {
    etna::set_state(
      cmd,
      lutImage.get(),
      vk::PipelineStageFlagBits2::eTransfer,
      vk::AccessFlagBits2::eTransferWrite,
      vk::ImageLayout::eTransferDstOptimal,
      vk::ImageAspectFlagBits::eColor);
    etna::flush_barriers(cmd);

    vk::BufferImageCopy2 copy{
      .bufferOffset = 0,
      .bufferRowLength = 0,
      .bufferImageHeight = 0,
      .imageSubresource =
        vk::ImageSubresourceLayers{
          .aspectMask = vk::ImageAspectFlagBits::eColor,
          .mipLevel = 0,
          .baseArrayLayer = 0,
          .layerCount = 1,
        },
      .imageOffset = vk::Offset3D{0, 0, 0},
      .imageExtent = vk::Extent3D{kLutSize, kLutSize, kLutSize},
    };
    vk::CopyBufferToImageInfo2 info{
      .srcBuffer = staging.get(),
      .dstImage = lutImage.get(),
      .dstImageLayout = vk::ImageLayout::eTransferDstOptimal,
      .regionCount = 1,
      .pRegions = &copy,
    };
    cmd.copyBufferToImage2(info);

    etna::set_state(
      cmd,
      lutImage.get(),
      vk::PipelineStageFlagBits2::eFragmentShader,
      vk::AccessFlagBits2::eShaderSampledRead,
      vk::ImageLayout::eShaderReadOnlyOptimal,
      vk::ImageAspectFlagBits::eColor);
    etna::flush_barriers(cmd);
  }
  ETNA_CHECK_VK_RESULT(cmd.end());
  cmdMgr->submitAndWait(std::move(cmd));

  return lutImage;
}
