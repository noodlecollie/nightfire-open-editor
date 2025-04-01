/*
 Copyright (C) 2010 Kristian Duske

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

#include "NightfireOpenModelLoader.h"

#include "io/File.h"
#include "io/FileSystem.h"
#include "io/MaterialUtils.h"
#include "io/ParserException.h"
#include "io/ReadFreeImageTexture.h"
#include "io/ReaderException.h"
#include "mdl/Material.h"
#include "mdl/Texture.h"
#include "mdl/TextureResource.h"

#include "kdl/path_utils.h"

#include <fmt/format.h>

namespace tb::io
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
#pragma pack(pop)
} // namespace MdlLayout

static tb::mdl::Texture loadTextureFromFileSystem(
  const std::filesystem::path& path, const FileSystem& fs, Logger& logger)
{
  return fs.openFile(path) | kdl::and_then([](auto file) {
           auto reader = file->reader().buffer();
           return readFreeImageTexture(reader);
         })
         | kdl::or_else(makeReadTextureErrorHandler(fs, logger)) | kdl::value();
}

NightfireOpenModelLoader::NightfireOpenModelLoader(
  std::filesystem::path path, std::filesystem::path textureRoot, const FileSystem& fs)
  : AssimpLoader(path, fs)
  , m_path{std::move(path)}
  , m_textureRoot{std::move(textureRoot)}
  , m_fs{fs}
{
  setTextureDims(generateTextureDims());
}

bool NightfireOpenModelLoader::canParse(const std::filesystem::path& path, Reader reader)
{
  if (!kdl::path_has_extension(kdl::path_to_lower(path), ".mdl"))
  {
    return false;
  }

  // Double check Assimp is OK to parse this file.
  if (!AssimpLoader::canParse(path))
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

Result<mdl::EntityModelData> NightfireOpenModelLoader::load(Logger& logger)
{
  return AssimpLoader::load(logger)
         | kdl::transform([this, &logger](mdl::EntityModelData modelData) {
             const size_t surfaceCount = modelData.surfaceCount();

             for (size_t surfaceIndex = 0; surfaceIndex < surfaceCount; ++surfaceIndex)
             {
               mdl::EntityModelSurface& surface = modelData.surface(surfaceIndex);
               const size_t skinCount = surface.skinCount();

               // Unsure whether we should try and share textures across multiple
               // surfaces that refer to the same one. For now, we don't try and
               // optimise for that.
               std::vector<mdl::Material> newSurfaceMaterials;

               for (size_t skinIndex = 0; skinIndex < skinCount; ++skinIndex)
               {
                 const mdl::Material* material = surface.skin(skinIndex);

                 if (!material)
                 {
                   throw ParserException{fmt::format(
                     "Model surface {} skin {} was null", surfaceIndex, skinIndex)};
                 }

                 const std::filesystem::path texturePath =
                   m_textureRoot / material->name();

                 mdl::Texture texture =
                   loadTextureFromFileSystem(texturePath, m_fs, logger);

                 std::shared_ptr<mdl::TextureResource> textureResource =
                   mdl::createTextureResource(std::move(texture));

                 newSurfaceMaterials.push_back(
                   mdl::Material{"", std::move(textureResource)});
               }

               surface.setSkins(std::move(newSurfaceMaterials));
             }

             return modelData;
           });
}

std::vector<AssimpLoader::TextureDim> NightfireOpenModelLoader::generateTextureDims()
  const
{
  using ResultType = Result<std::vector<AssimpLoader::TextureDim>>;

  return m_fs.openFile(m_path.string()) | kdl::and_then([this](auto file) -> ResultType {
           BufferedReader reader = file->reader().buffer();

           if (!NightfireOpenModelLoader::canParse(m_path, reader))
           {
             return std::vector<AssimpLoader::TextureDim>();
           }

           reader.seekFromBegin(MdlLayout::NumTexturesOffset);
           const int32_t numTextures = reader.readInt<int32_t>();

           reader.seekFromBegin(MdlLayout::MdlHeaderSize);
           const MdlLayout::NfMdlHeader nfHeader =
             reader.read<MdlLayout::NfMdlHeader, MdlLayout::NfMdlHeader>();

           if (nfHeader.id != MdlLayout::NfIdent)
           {
             // Invalid format?? Nothing to do.
             return std::vector<AssimpLoader::TextureDim>();
           }

           std::vector<AssimpLoader::TextureDim> outDims;
           reader.seekFromBegin(static_cast<size_t>(nfHeader.textureDimsIndex));

           for (int32_t textureIndex = 0; textureIndex < numTextures; ++textureIndex)
           {
             const MdlLayout::NfMdlTextureDim dim =
               reader.read<MdlLayout::NfMdlTextureDim, MdlLayout::NfMdlTextureDim>();

             outDims.emplace_back(dim.width, dim.height);
           }

           return outDims;
         })
         | kdl::or_else([](const auto&) -> ResultType {
             return std::vector<AssimpLoader::TextureDim>();
           })
         | kdl::value();
}

} // namespace tb::io
