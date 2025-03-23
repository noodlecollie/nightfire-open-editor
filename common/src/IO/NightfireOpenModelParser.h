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

#pragma once

#include "Assets/EntityModel_Forward.h"
#include "IO/AssimpParser.h"

#include <filesystem>
#include <string>

namespace TrenchBroom::IO
{
class FileSystem;
class AssimpParser;
class Reader;

class NightfireOpenModelParser : public AssimpParser
{
private:
  std::filesystem::path m_path;
  std::filesystem::path m_textureRoot;
  const FileSystem& m_fs;

public:
  NightfireOpenModelParser(std::filesystem::path path, std::filesystem::path textureRoot, const FileSystem& fs);

  static bool canParse(const std::filesystem::path& path, Reader reader);

private:
  std::unique_ptr<Assets::EntityModel> doInitializeModel(Logger& logger) override;
  std::vector<AssimpParser::TextureDim> generateTextureDims() const;
};

} // namespace TrenchBroom::IO
