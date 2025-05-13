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

#pragma once

#include "FileLocation.h"
#include "Result.h"
#include "io/StandardMapParser.h"
#include "mdl/BezierPatch.h"
#include "mdl/Brush.h"
#include "mdl/BrushFace.h"
#include "mdl/EntityProperties.h"

#include "vm/bbox.h"

#include <optional>
#include <string_view>
#include <variant>
#include <vector>

namespace kdl
{
class task_manager;
}

namespace tb::mdl
{
class BrushNode;
class EntityNode;
class EntityNodeBase;
class EntityProperty;
class GroupNode;
class LayerNode;
enum class MapFormat;
class Node;
class WorldNode;
} // namespace tb::mdl

namespace tb::io
{

class ParserStatus;

/**
 * Abstract superclass containing common code for:
 *
 *  - WorldReader (loading a whole .map)
 *  - NodeReader (reading part of a map, for pasting into an existing map)
 *  - BrushFaceReader (reading faces when copy/pasting UV alignment)
 *
 * The flow of control is:
 *
 * 1. MapParser callbacks get called with the raw data, which we just store
 * (m_objectInfos).
 * 2. Convert the raw data to nodes in parallel (createNodes) and record any additional
 * information necessary to restore the parent / child relationships.
 * 3. Validate the created nodes.
 * 4. Post process the nodes to find the correct parent nodes (createNodes).
 * 5. Call the appropriate callbacks (onWorldspawn, onLayer, ...).
 */
class MapReader : public StandardMapParser
{
public: // only public so that helper methods can see these declarations
  struct EntityInfo
  {
    std::vector<mdl::EntityProperty> properties;
    FileLocation startLocation;
    std::optional<FileLocation> endLocation;
  };

  struct BrushInfo
  {
    std::vector<mdl::BrushFace> faces;
    FileLocation startLocation;
    std::optional<FileLocation> endLocation;
    std::optional<size_t> parentIndex;
  };

  struct PatchInfo
  {
    size_t rowCount;
    size_t columnCount;
    std::vector<mdl::BezierPatch::Point> controlPoints;
    std::string materialName;
    FileLocation startLocation;
    std::optional<FileLocation> endLocation;
    std::optional<size_t> parentIndex;
  };

  using ObjectInfo = std::variant<EntityInfo, BrushInfo, PatchInfo>;

private:
  mdl::EntityPropertyConfig m_entityPropertyConfig;
  vm::bbox3d m_worldBounds;

private: // data populated in response to MapParser callbacks
  std::vector<ObjectInfo> m_objectInfos;
  std::vector<size_t> m_currentEntityInfo;

protected:
  /**
   * Creates a new reader where the given string is expected to be formatted in the given
   * source map format, and the created objects are converted to the given target format.
   *
   * @param str the string to parse
   * @param sourceMapFormat the expected format of the given string
   * @param targetMapFormat the format to convert the created objects to
   * @param entityPropertyConfig the entity property config to use
   * if orphaned
   */
  MapReader(
    std::string_view str,
    mdl::MapFormat sourceMapFormat,
    mdl::MapFormat targetMapFormat,
    mdl::EntityPropertyConfig entityPropertyConfig);

  /**
   * Attempts to parse as one or more entities.
   */
  Result<void> readEntities(
    const vm::bbox3d& worldBounds, ParserStatus& status, kdl::task_manager& taskManager);
  /**
   * Attempts to parse as one or more brushes without any enclosing entity.
   */
  Result<void> readBrushes(
    const vm::bbox3d& worldBounds, ParserStatus& status, kdl::task_manager& taskManager);
  /**
   * Attempts to parse as one or more brush faces.
   */
  Result<void> readBrushFaces(const vm::bbox3d& worldBounds, ParserStatus& status);

protected: // implement MapParser interface
  void onBeginEntity(
    const FileLocation& location,
    std::vector<mdl::EntityProperty> properties,
    ParserStatus& status) override;
  void onEndEntity(const FileLocation& endLocation, ParserStatus& status) override;
  void onBeginBrush(const FileLocation& location, ParserStatus& status) override;
  void onEndBrush(const FileLocation& endLocation, ParserStatus& status) override;
  void onStandardBrushFace(
    const FileLocation& location,
    mdl::MapFormat targetMapFormat,
    const vm::vec3d& point1,
    const vm::vec3d& point2,
    const vm::vec3d& point3,
    const mdl::BrushFaceAttributes& attribs,
    ParserStatus& status) override;
  void onValveBrushFace(
    const FileLocation& location,
    mdl::MapFormat targetMapFormat,
    const vm::vec3d& point1,
    const vm::vec3d& point2,
    const vm::vec3d& point3,
    const mdl::BrushFaceAttributes& attribs,
    const vm::vec3d& uAxis,
    const vm::vec3d& vAxis,
    ParserStatus& status) override;
  void onPatch(
    const FileLocation& startLocation,
    const FileLocation& endLocation,
    mdl::MapFormat targetMapFormat,
    size_t rowCount,
    size_t columnCount,
    std::vector<vm::vec<double, 5>> controlPoints,
    std::string materialName,
    ParserStatus& status) override;

private: // helper methods
  void createNodes(ParserStatus& status, kdl::task_manager& taskManager);
  std::optional<size_t> getCurrentEntityInfo() const;

private: // subclassing interface - these will be called in the order that nodes should be
         // inserted
  /**
   * Called for the first worldspawn entity. Subclasses cannot capture the given world
   * node but must create their own instead.
   *
   * If a world node was created, then this function is guaranteed to be called before any
   * other callback.
   *
   * Returns a pointer to a node which should become the parent of any node that belongs
   * to the world. This could be the default layer of the world node, or a dummy entity.
   */
  virtual mdl::Node* onWorldNode(
    std::unique_ptr<mdl::WorldNode> worldNode, ParserStatus& status) = 0;

  /**
   * Called for each custom layer.
   */
  virtual void onLayerNode(
    std::unique_ptr<mdl::Node> layerNode, ParserStatus& status) = 0;

  /**
   * Called for each group, entity entity or brush node. The given parent can be null.
   */
  virtual void onNode(
    mdl::Node* parentNode, std::unique_ptr<mdl::Node> node, ParserStatus& status) = 0;

  /**
   * Called for each brush face.
   */
  virtual void onBrushFace(mdl::BrushFace face, ParserStatus& status);
};

} // namespace tb::io
