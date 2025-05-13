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
 along with TrenchBroom.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include "ui/PreferencePane.h"

#include <optional>

class QCheckBox;
class QComboBox;

namespace tb::ui
{
class SliderWithLabel;

class ViewPreferencePane : public PreferencePane
{
  Q_OBJECT
private:
  QComboBox* m_layoutCombo = nullptr;
  QCheckBox* m_link2dCameras = nullptr;
  SliderWithLabel* m_brightnessSlider = nullptr;
  SliderWithLabel* m_gridAlphaSlider = nullptr;
  SliderWithLabel* m_fovSlider = nullptr;
  SliderWithLabel* m_selectionFocusDistanceSlider = nullptr;
  QCheckBox* m_showAxes = nullptr;
  QComboBox* m_filterModeCombo = nullptr;
  QCheckBox* m_enableMsaa = nullptr;
  QComboBox* m_themeCombo = nullptr;
  QComboBox* m_materialBrowserIconSizeCombo = nullptr;
  QComboBox* m_rendererFontSizeCombo = nullptr;

public:
  explicit ViewPreferencePane(QWidget* parent = nullptr);

private:
  void createGui();
  QWidget* createViewPreferences();

  void bindEvents();

  bool canResetToDefaults() override;
  void doResetToDefaults() override;
  void updateControls() override;
  bool validate() override;

  std::optional<size_t> findFilterMode(int minFilter, int magFilter) const;
  int findThemeIndex(const QString& theme);
private slots:
  void layoutChanged(int index);
  void link2dCamerasChanged(int state);
  void brightnessChanged(int value);
  void gridAlphaChanged(int value);
  void fovChanged(int value);
  void selectionFocusDistanceChanged(int value);
  void showAxesChanged(int state);
  void enableMsaaChanged(int state);
  void filterModeChanged(int index);
  void themeChanged(int index);
  void materialBrowserIconSizeChanged(int index);
  void rendererFontSizeChanged(const QString& text);
};

} // namespace tb::ui
