/* SPDX-FileCopyrightText: 2023 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup spview3d
 */

#include "BLI_math_matrix.h"
#include "BLI_math_vector.h"
#include "BLI_utildefines.h"

#include "BKE_action.hh"
#include "BKE_context.hh"
#include "BKE_layer.hh"
#include "BKE_lib_id.hh"
#include "BKE_object.hh"

#include "DNA_armature_types.h"
#include "DNA_object_types.h"

#include "ED_gizmo_library.hh"

#include "UI_resources.hh"

#include "MEM_guardedalloc.h"

#include "RNA_access.hh"

#include "WM_types.hh"

#include "view3d_intern.hh" /* own include */

/* -------------------------------------------------------------------- */
/** \name Armature Spline Gizmo
 * \{ */

/*
 * TODO(@ideasman42): Current conversion is a approximation (usable not correct),
 * we'll need to take the next/previous bones into account to get the tangent directions.
 * First last matrices from #BKE_pchan_bbone_spline_setup are close but also not quite accurate
 * since they're not at either end-points on the curve.
 *
 * Likely we'll need a function especially to get the first/last orientations.
 */

#define BBONE_SCALE_Y 3.0f

struct BoneSplineHandle {
  wmGizmo *gizmo;
  bPoseChannel *pchan;
  /* We could remove, keep since at the moment for checking the conversion. */
  float co[3];
  int index;
};

struct BoneSplineWidgetGroup {
  BoneSplineHandle handles[2];
};

static void gizmo_bbone_offset_get(const wmGizmo * /*gz*/, wmGizmoProperty *gz_prop, void *value_p)
{
  BoneSplineHandle *bh = static_cast<BoneSplineHandle *>(gz_prop->custom_func.user_data);
  bPoseChannel *pchan = bh->pchan;

  float *value = static_cast<float *>(value_p);
  BLI_assert(gz_prop->type->array_length == 3);

  if (bh->index == 0) {
    bh->co[1] = pchan->bone->ease1 / BBONE_SCALE_Y;
    bh->co[0] = pchan->curve_in_x;
    bh->co[2] = pchan->curve_in_z;
  }
  else {
    bh->co[1] = -pchan->bone->ease2 / BBONE_SCALE_Y;
    bh->co[0] = pchan->curve_out_x;
    bh->co[2] = pchan->curve_out_z;
  }
  copy_v3_v3(value, bh->co);
}

static void gizmo_bbone_offset_set(const wmGizmo * /*gz*/,
                                   wmGizmoProperty *gz_prop,
                                   const void *value_p)
{
  BoneSplineHandle *bh = static_cast<BoneSplineHandle *>(gz_prop->custom_func.user_data);
  bPoseChannel *pchan = bh->pchan;

  const float *value = static_cast<const float *>(value_p);

  BLI_assert(gz_prop->type->array_length == 3);
  copy_v3_v3(bh->co, value);

  if (bh->index == 0) {
    pchan->bone->ease1 = max_ff(0.0f, bh->co[1] * BBONE_SCALE_Y);
    pchan->curve_in_x = bh->co[0];
    pchan->curve_in_z = bh->co[2];
  }
  else {
    pchan->bone->ease2 = max_ff(0.0f, -bh->co[1] * BBONE_SCALE_Y);
    pchan->curve_out_x = bh->co[0];
    pchan->curve_out_z = bh->co[2];
  }
}

static bool WIDGETGROUP_armature_spline_poll(const bContext *C, wmGizmoGroupType * /*gzgt*/)
{
  View3D *v3d = CTX_wm_view3d(C);
  if (v3d->gizmo_flag & (V3D_GIZMO_HIDE | V3D_GIZMO_HIDE_CONTEXT)) {
    return false;
  }

  const Scene *scene = CTX_data_scene(C);
  ViewLayer *view_layer = CTX_data_view_layer(C);
  BKE_view_layer_synced_ensure(scene, view_layer);
  Base *base = BKE_view_layer_active_base_get(view_layer);
  if (base && BASE_SELECTABLE(v3d, base)) {
    Object *ob = BKE_object_pose_armature_get(base->object);
    if (ob) {
      const bArmature *arm = static_cast<const bArmature *>(ob->data);
      if (arm->drawtype == ARM_DRAW_TYPE_B_BONE) {
        bPoseChannel *pchan = BKE_pose_channel_active_if_bonecoll_visible(ob);
        if (pchan && pchan->bone->segments > 1) {
          if (BKE_id_is_editable(CTX_data_main(C), &arm->id)) {
            return true;
          }
        }
      }
    }
  }
  return false;
}

static void WIDGETGROUP_armature_spline_setup(const bContext *C, wmGizmoGroup *gzgroup)
{
  const Scene *scene = CTX_data_scene(C);
  ViewLayer *view_layer = CTX_data_view_layer(C);
  BKE_view_layer_synced_ensure(scene, view_layer);
  Object *ob = BKE_object_pose_armature_get(BKE_view_layer_active_object_get(view_layer));
  bPoseChannel *pchan = BKE_pose_channel_active_if_bonecoll_visible(ob);

  const wmGizmoType *gzt_move = WM_gizmotype_find("GIZMO_GT_move_3d", true);

  BoneSplineWidgetGroup *bspline_group = MEM_callocN<BoneSplineWidgetGroup>(__func__);
  gzgroup->customdata = bspline_group;

  /* Handles */
  for (int i = 0; i < ARRAY_SIZE(bspline_group->handles); i++) {
    wmGizmo *gz;
    gz = bspline_group->handles[i].gizmo = WM_gizmo_new_ptr(gzt_move, gzgroup, nullptr);
    RNA_enum_set(gz->ptr, "draw_style", ED_GIZMO_MOVE_STYLE_RING_2D);
    RNA_enum_set(gz->ptr,
                 "draw_options",
                 ED_GIZMO_MOVE_DRAW_FLAG_FILL | ED_GIZMO_MOVE_DRAW_FLAG_ALIGN_VIEW);
    WM_gizmo_set_flag(gz, WM_GIZMO_DRAW_VALUE, true);

    UI_GetThemeColor3fv(TH_GIZMO_PRIMARY, gz->color);
    UI_GetThemeColor3fv(TH_GIZMO_HI, gz->color_hi);

    gz->scale_basis = 0.06f;

    if (i == 0) {
      copy_v3_v3(gz->matrix_basis[3], pchan->loc);
    }
  }
}

static void WIDGETGROUP_armature_spline_refresh(const bContext *C, wmGizmoGroup *gzgroup)
{
  const Scene *scene = CTX_data_scene(C);
  ViewLayer *view_layer = CTX_data_view_layer(C);
  BKE_view_layer_synced_ensure(scene, view_layer);
  Object *ob = BKE_object_pose_armature_get(BKE_view_layer_active_object_get(view_layer));

  if (!gzgroup->customdata) {
    return;
  }

  BoneSplineWidgetGroup *bspline_group = static_cast<BoneSplineWidgetGroup *>(gzgroup->customdata);
  bPoseChannel *pchan = BKE_pose_channel_active_if_bonecoll_visible(ob);

  /* Handles */
  for (int i = 0; i < ARRAY_SIZE(bspline_group->handles); i++) {
    wmGizmo *gz = bspline_group->handles[i].gizmo;
    bspline_group->handles[i].pchan = pchan;
    bspline_group->handles[i].index = i;

    float mat[4][4];
    mul_m4_m4m4(
        mat, ob->object_to_world().ptr(), (i == 0) ? pchan->disp_mat : pchan->disp_tail_mat);
    copy_m4_m4(gz->matrix_space, mat);

    /* need to set property here for undo. TODO: would prefer to do this in _init. */
    wmGizmoPropertyFnParams params{};
    params.value_get_fn = gizmo_bbone_offset_get;
    params.value_set_fn = gizmo_bbone_offset_set;
    params.range_get_fn = nullptr;
    params.user_data = &bspline_group->handles[i];
    WM_gizmo_target_property_def_func(gz, "offset", &params);
  }
}

void VIEW3D_GGT_armature_spline(wmGizmoGroupType *gzgt)
{
  gzgt->name = "Armature Spline Widgets";
  gzgt->idname = "VIEW3D_GGT_armature_spline";

  gzgt->flag = (WM_GIZMOGROUPTYPE_PERSISTENT | WM_GIZMOGROUPTYPE_3D);

  gzgt->poll = WIDGETGROUP_armature_spline_poll;
  gzgt->setup = WIDGETGROUP_armature_spline_setup;
  gzgt->setup_keymap = WM_gizmogroup_setup_keymap_generic_maybe_drag;
  gzgt->refresh = WIDGETGROUP_armature_spline_refresh;
}

/** \} */
