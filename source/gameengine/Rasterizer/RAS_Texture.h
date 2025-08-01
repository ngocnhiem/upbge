/*
 * ***** BEGIN GPL LICENSE BLOCK *****
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file RAS_Texture.h
 *  \ingroup ketsji
 */

#pragma once

#include <array>
#include <string>

namespace blender::gpu {
class Texture;
}  // namespace blender::gpu

struct Image;

class RAS_Texture {
 protected:
  std::string m_name;

 public:
  RAS_Texture();
  virtual ~RAS_Texture();

  virtual bool Ok() const = 0;
  virtual bool IsCubeMap() const = 0;

  virtual Image *GetImage() const = 0;
  virtual blender::gpu::Texture *GetGPUTexture() const = 0;
  std::string &GetName();

  virtual unsigned int GetTextureType() = 0;

  enum { MaxUnits = 32 };
};
