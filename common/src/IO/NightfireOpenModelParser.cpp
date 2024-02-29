/*
 Copyright (C) 2024 NoodleCollie
 Copyright (C) 2023 Daniel Walder
 Copyright (C) 2022 Amara M. Kilic
 Copyright (C) 2022 Kristian Duske

 This file is part of TrenchBroom.

 TrenchBroom is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 TrenchBroom is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with TrenchBroom. If not, see <http://www.gnu.org/licenses/>.
 */

#include "NightfireOpenModelParser.h"

#include "Assets/EntityModel.h"
#include "Assets/Texture.h"
#include "Error.h"
#include "Exceptions.h"
#include "IO/AssimpParser.h"
#include "IO/File.h"
#include "IO/FileSystem.h"
#include "IO/ReadFreeImageTexture.h"
#include "IO/Reader.h"
#include "Logger.h"

#include "kdl/result.h"
#include "kdl/string_utils.h"

#include <fmt/format.h>

namespace TrenchBroom::IO
{
namespace MdlLayout
{
static const int32_t Ident = (('T' << 24) + ('S' << 16) + ('D' << 8) + 'I');
static const int32_t Version10 = 10;
static const size_t MdlHeaderSize = 244;
static const int32_t NfIdent = (('P' << 24) + ('O' << 16) + ('F' << 8) + 'N');
static const size_t NumTexturesOffset = 64 +                     // Name
                                        (14 * sizeof(int32_t)) + // Int properties
                                        (5 * 3 * sizeof(float)); // Vector properties

#pragma pack(push, 1)
struct NfMdlHeader
{
  // Expected to be equal to NfIdent
  uint32_t id;

  // Version of this struct.
  uint32_t version;

  // Offset of gait bones section.
  int32_t gaitBonesIndex;

  // Number of gait bone entries.
  int32_t gaitBonesCount;

  // Offset of texture timensions section.
  // The number of texture dimensions is the
  // same as the number of textures.
  int32_t textureDimsIndex;
};

struct NfMdlTextureDim
{
  // Original width of the texture.
  int32_t width;

  // Original height of the texture.
  int32_t height;
};
} // namespace MdlLayout
#pragma pack(pop)

static Assets::Texture loadTextureFromFileSystem(
  const std::filesystem::path& path, const FileSystem& fs, Logger& logger)
{
  return fs.openFile(path)
    .and_then([](auto file) {
      auto reader = file->reader().buffer();
      return readFreeImageTexture("", reader);
    })
    .or_else(makeReadTextureErrorHandler(fs, logger))
    .value();
}

NightfireOpenModelParser::NightfireOpenModelParser(
  std::filesystem::path path, std::filesystem::path textureRoot, const FileSystem& fs)
  : AssimpParser{path, fs} // Path deliberately copied here, not moved
  , m_path{std::move(path)}
  , m_textureRoot{std::move(textureRoot)}
  , m_fs{fs}
{
  setTextureDims(generateTextureDims());
}

bool NightfireOpenModelParser::canParse(const std::filesystem::path& path, Reader reader)
{
  if (kdl::str_to_lower(path.extension().string()) != ".mdl")
  {
    return false;
  }

  // Double check Assimp is OK to parse this file.
  if (!AssimpParser::canParse(path))
  {
    return false;
  }

  reader.seekFromBegin(0);
  const int32_t ident = reader.readInt<int32_t>();
  const int32_t version = reader.readInt<int32_t>();

  if (ident != MdlLayout::Ident || version != MdlLayout::Version10)
  {
    return false;
  }

  reader.seekFromBegin(MdlLayout::MdlHeaderSize);

  const MdlLayout::NfMdlHeader nfHeader =
    reader.read<MdlLayout::NfMdlHeader, MdlLayout::NfMdlHeader>();

  // We currently support all versions of the NF MDL format,
  // so don't need to check the version.
  return nfHeader.id == MdlLayout::NfIdent;
}

std::unique_ptr<Assets::EntityModel> NightfireOpenModelParser::doInitializeModel(
  Logger& logger)
{
  std::unique_ptr<Assets::EntityModel> outModel = AssimpParser::doInitializeModel(logger);

  const size_t surfaceCount = outModel->surfaceCount();

  for (size_t surfaceIndex = 0; surfaceIndex < surfaceCount; ++surfaceIndex)
  {
    Assets::EntityModelSurface& surface = outModel->surface(surfaceIndex);
    const size_t textureCount = surface.skinCount();

    // Unsure whether we should try and share textures across multiple surfaces
    // that refer to the same one. For now, we don't try and optimise for that.
    std::vector<Assets::Texture> newSurfaceTextures;

    for (size_t textureIndex = 0; textureIndex < textureCount; ++textureIndex)
    {
      const Assets::Texture* texture = surface.skin(textureIndex);

      if (!texture)
      {
        throw ParserException{fmt::format(
          "Model surface {} texture {} was null", surfaceIndex, textureIndex)};
      }

      const std::filesystem::path texturePath = m_textureRoot / texture->name();
      newSurfaceTextures.push_back(loadTextureFromFileSystem(texturePath, m_fs, logger));
    }

    surface.setSkins(std::move(newSurfaceTextures));
  }

  return outModel;
}

std::vector<AssimpParser::TextureDim> NightfireOpenModelParser::generateTextureDims()
  const
{
  using ResultType = Result<std::vector<AssimpParser::TextureDim>>;

  return m_fs.openFile(m_path.string())
    .and_then([this](auto file) -> ResultType {
      BufferedReader reader = file->reader().buffer();

      if (!NightfireOpenModelParser::canParse(m_path, reader))
      {
        return ResultType(std::vector<AssimpParser::TextureDim>());
      }

      reader.seekFromBegin(MdlLayout::NumTexturesOffset);
      const int32_t numTextures = reader.readInt<int32_t>();

      reader.seekFromBegin(MdlLayout::MdlHeaderSize);
      const MdlLayout::NfMdlHeader nfHeader =
        reader.read<MdlLayout::NfMdlHeader, MdlLayout::NfMdlHeader>();

      if (nfHeader.id != MdlLayout::NfIdent)
      {
        // Invalid format?? Nothing to do.
        return ResultType(std::vector<AssimpParser::TextureDim>());
      }

      std::vector<AssimpParser::TextureDim> outDims;
      reader.seekFromBegin(static_cast<size_t>(nfHeader.textureDimsIndex));

      for (int32_t textureIndex = 0; textureIndex < numTextures; ++textureIndex)
      {
        const MdlLayout::NfMdlTextureDim dim =
          reader.read<MdlLayout::NfMdlTextureDim, MdlLayout::NfMdlTextureDim>();

        outDims.emplace_back(dim.width, dim.height);
      }

      return ResultType(std::move(outDims));
    })
    .or_else([](const auto&) {
      return ResultType(std::vector<AssimpParser::TextureDim>());
    })
    .value();
}

} // namespace TrenchBroom::IO
