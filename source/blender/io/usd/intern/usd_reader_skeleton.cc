/* SPDX-FileCopyrightText: 2021 NVIDIA Corporation. All rights reserved.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "usd_reader_skeleton.hh"
#include "usd_skel_convert.hh"

#include "BKE_armature.hh"
#include "BKE_object.hh"

#include "DNA_armature_types.h"
#include "DNA_object_types.h"

namespace blender::io::usd {

void USDSkeletonReader::create_object(Main *bmain)
{
  bArmature *arm = BKE_armature_add(bmain, name_.c_str());

  object_ = BKE_object_add_only_object(bmain, OB_ARMATURE, name_.c_str());
  object_->data = arm;
}

void USDSkeletonReader::read_object_data(Main *bmain, const pxr::UsdTimeCode time)
{
  if (!object_ || !object_->data) {
    return;
  }

  import_skeleton(bmain, object_, skel_, reports());

  USDXformReader::read_object_data(bmain, time);
}

}  // namespace blender::io::usd
