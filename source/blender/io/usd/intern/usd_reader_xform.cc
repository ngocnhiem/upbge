/* SPDX-FileCopyrightText: 2021 Tangent Animation. All rights reserved.
 * SPDX-FileCopyrightText: 2023 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Adapted from the Blender Alembic importer implementation. */

#include "usd_reader_xform.hh"

#include "BKE_constraint.h"
#include "BKE_lib_id.hh"
#include "BKE_object.hh"

#include "BLI_math_matrix.h"
#include "BLI_string.h"

#include "DNA_cachefile_types.h"
#include "DNA_constraint_types.h"
#include "DNA_object_types.h"

#include <pxr/base/gf/matrix4f.h>
#include <pxr/usd/usdGeom/xformable.h>

#include <string>

namespace blender::io::usd {

void USDXformReader::create_object(Main *bmain)
{
  object_ = BKE_object_add_only_object(bmain, OB_EMPTY, name_.c_str());
  object_->empty_drawsize = 0.1f;
  object_->data = nullptr;
}

void USDXformReader::read_object_data(Main * /*bmain*/, const pxr::UsdTimeCode time)
{
  bool is_constant;
  float transform_from_usd[4][4];

  read_matrix(transform_from_usd, time, settings_->scene_scale, &is_constant);

  if (!is_constant && settings_->get_cache_file) {
    bConstraint *con = BKE_constraint_add_for_object(
        object_, nullptr, CONSTRAINT_TYPE_TRANSFORM_CACHE);
    bTransformCacheConstraint *data = static_cast<bTransformCacheConstraint *>(con->data);

    pxr::SdfPath object_path = use_parent_xform_ ? prim_.GetParent().GetPath() : this->prim_path();

    STRNCPY(data->object_path, object_path.GetAsString().c_str());

    data->cache_file = settings_->get_cache_file();
    id_us_plus(&data->cache_file->id);
  }

  BKE_object_apply_mat4(object_, transform_from_usd, true, false);

  /* Make sure to collect custom attributes */
  set_props(use_parent_xform(), time);
}

pxr::SdfPath USDXformReader::object_prim_path() const
{
  return get_xformable().GetPrim().GetPath();
}

void USDXformReader::read_matrix(float r_mat[4][4] /* local matrix */,
                                 const pxr::UsdTimeCode time,
                                 const float scale,
                                 bool *r_is_constant) const
{
  BLI_assert(r_mat);
  BLI_assert(r_is_constant);

  *r_is_constant = true;
  unit_m4(r_mat);

  std::optional<XformResult> xf_result = get_local_usd_xform(time);

  if (!xf_result) {
    return;
  }

  std::get<0>(*xf_result).Get(r_mat);
  *r_is_constant = std::get<1>(*xf_result);

  /* Apply global scaling and rotation only to root objects, parenting
   * will propagate it. */
  if ((scale != 1.0 || settings_->do_convert_mat) && is_root_xform_) {

    if (scale != 1.0f) {
      float scale_mat[4][4];
      scale_m4_fl(scale_mat, scale);
      mul_m4_m4m4(r_mat, scale_mat, r_mat);
    }

    if (settings_->do_convert_mat) {
      mul_m4_m4m4(r_mat, settings_->conversion_mat, r_mat);
    }
  }
}

bool USDXformReader::prim_has_xform_ops() const
{
  const pxr::UsdGeomXformable xformable(prim_);

  if (!xformable) {
    /* This might happen if the prim is a Scope. */
    return false;
  }

  bool reset_xform_stack = false;

  return !xformable.GetOrderedXformOps(&reset_xform_stack).empty();
}

bool USDXformReader::is_root_xform_prim() const
{
  if (!prim_.IsValid()) {
    return false;
  }

  if (is_in_proto()) {
    /* We don't consider prototypes to be root prims,
     * because we never want to apply global scaling
     * or rotations to the prototypes themselves. */
    return false;
  }

  if (prim_.IsA<pxr::UsdGeomXformable>()) {
    /* If this prim doesn't have an ancestor that's a
     * UsdGeomXformable, then it's a root prim.  Note
     * that it's not sufficient to only check the immediate
     * parent prim, since the immediate parent could be a
     * UsdGeomScope that has an xformable ancestor. */
    pxr::UsdPrim cur_parent = prim_.GetParent();

    if (use_parent_xform_) {
      cur_parent = cur_parent.GetParent();
    }

    while (cur_parent && !cur_parent.IsPseudoRoot()) {
      if (cur_parent.IsA<pxr::UsdGeomXformable>()) {
        return false;
      }
      cur_parent = cur_parent.GetParent();
    }

    /* We didn't find an xformable ancestor. */
    return true;
  }

  return false;
}

std::optional<XformResult> USDXformReader::get_local_usd_xform(const pxr::UsdTimeCode time) const
{
  const pxr::UsdGeomXformable xformable = get_xformable();

  if (!xformable) {
    /* This might happen if the prim is a Scope. */
    return std::nullopt;
  }

  bool is_constant = !xformable.TransformMightBeTimeVarying();

  bool reset_xform_stack;
  pxr::GfMatrix4d xform;
  if (!xformable.GetLocalTransformation(&xform, &reset_xform_stack, time)) {
    return std::nullopt;
  }

  /* The USD bind transform is a matrix of doubles,
   * but we cast it to GfMatrix4f because Blender expects
   * a matrix of floats. */
  return XformResult(pxr::GfMatrix4f(xform), is_constant);
}

pxr::UsdGeomXformable USDXformReader::get_xformable() const
{
  pxr::UsdPrim prim = use_parent_xform_ ? prim_.GetParent() : prim_;
  return pxr::UsdGeomXformable(prim);
}
}  // namespace blender::io::usd
