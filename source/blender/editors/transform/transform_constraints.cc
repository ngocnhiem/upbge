/* SPDX-FileCopyrightText: 2001-2002 NaN Holding BV. All rights reserved.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup edtransform
 */

#include <cmath>
#include <cstdlib>
#include <cstring>

#include "DNA_scene_types.h"
#include "DNA_screen_types.h"
#include "DNA_space_types.h"
#include "DNA_userdef_types.h"
#include "DNA_view3d_types.h"

#include "GPU_immediate.hh"
#include "GPU_matrix.hh"
#include "GPU_state.hh"

#include "BLI_math_geom.h"
#include "BLI_math_matrix.h"
#include "BLI_math_rotation.h"
#include "BLI_rect.h"
#include "BLI_string_utf8.h"
#include "BLI_utildefines.h"

#include "BLT_translation.hh"

#include "UI_resources.hh"

#include "transform.hh"
#include "transform_gizmo.hh"
#include "transform_orientations.hh"
#include "transform_snap.hh"

/* Own include. */
#include "transform_constraints.hh"

namespace blender::ed::transform {

static void drawObjectConstraint(TransInfo *t);

/* -------------------------------------------------------------------- */
/** \name Internal Utilities
 * \{ */

static void projection_matrix_calc(const TransInfo *t, float r_pmtx[3][3])
{
  unit_m3(r_pmtx);

  if (!(t->con.mode & CON_AXIS0)) {
    zero_v3(r_pmtx[0]);
  }

  if (!(t->con.mode & CON_AXIS1)) {
    zero_v3(r_pmtx[1]);
  }

  if (!(t->con.mode & CON_AXIS2)) {
    zero_v3(r_pmtx[2]);
  }

  float mat[3][3];
  mul_m3_m3m3(mat, r_pmtx, t->spacemtx_inv);
  mul_m3_m3m3(r_pmtx, t->spacemtx, mat);
}

/* ************************** CONSTRAINTS ************************* */
#define CONSTRAIN_EPSILON 0.0001f

static void constraint_plane_normal_calc(const TransInfo *t, float r_plane_no[3])
{
  const float *constraint_vector[2];
  int n = 0;
  for (int i = 0; i < 3; i++) {
    if (t->con.mode & (CON_AXIS0 << i)) {
      constraint_vector[n++] = t->spacemtx[i];
      if (n == 2) {
        break;
      }
    }
  }
  BLI_assert(n == 2);

  cross_v3_v3v3(r_plane_no, constraint_vector[0], constraint_vector[1]);
  normalize_v3(r_plane_no);
}

void constraintNumInput(TransInfo *t, float vec[3])
{
  int mode = t->con.mode;
  if (mode & CON_APPLY) {
    float nval = (t->flag & T_NULL_ONE) ? 1.0f : 0.0f;

    const int dims = getConstraintSpaceDimension(t);
    if (dims == 2) {
      int axis = mode & (CON_AXIS0 | CON_AXIS1 | CON_AXIS2);
      if (axis == (CON_AXIS0 | CON_AXIS1)) {
        // vec[0] = vec[0]; /* Same. */
        // vec[1] = vec[1]; /* Same. */
        vec[2] = nval;
      }
      else if (axis == (CON_AXIS1 | CON_AXIS2)) {
        vec[2] = vec[1];
        vec[1] = vec[0];
        vec[0] = nval;
      }
      else if (axis == (CON_AXIS0 | CON_AXIS2)) {
        // vec[0] = vec[0]; /* Same. */
        vec[2] = vec[1];
        vec[1] = nval;
      }
    }
    else if (dims == 1) {
      if (mode & CON_AXIS0) {
        // vec[0] = vec[0]; /* Same. */
        vec[1] = nval;
        vec[2] = nval;
      }
      else if (mode & CON_AXIS1) {
        vec[1] = vec[0];
        vec[0] = nval;
        vec[2] = nval;
      }
      else if (mode & CON_AXIS2) {
        vec[2] = vec[0];
        vec[0] = nval;
        vec[1] = nval;
      }
    }
  }
}

static void viewAxisCorrectCenter(const TransInfo *t, float t_con_center[3])
{
  if (t->spacetype == SPACE_VIEW3D) {
    // View3D *v3d = t->area->spacedata.first;
    const float min_dist = 1.0f; /* `v3d->clip_start`. */
    float dir[3];
    float l;

    sub_v3_v3v3(dir, t_con_center, t->viewinv[3]);
    if (dot_v3v3(dir, t->viewinv[2]) < 0.0f) {
      negate_v3(dir);
    }
    project_v3_v3v3(dir, dir, t->viewinv[2]);

    l = len_v3(dir);

    if (l < min_dist) {
      float diff[3];
      normalize_v3_v3_length(diff, t->viewinv[2], min_dist - l);
      sub_v3_v3(t_con_center, diff);
    }
  }
}

/**
 * Axis calculation taking the view into account, correcting view-aligned axis.
 */
static void axisProjection(const TransInfo *t,
                           const float axis[3],
                           const float in[3],
                           float out[3])
{
  float vec[3], factor, angle;
  float t_con_center[3];

  if (is_zero_v3(in)) {
    return;
  }

  copy_v3_v3(t_con_center, t->center_global);

  /* Checks for center being too close to the view center. */
  viewAxisCorrectCenter(t, t_con_center);

  angle = fabsf(angle_v3v3(axis, t->viewinv[2]));
  if (angle > float(M_PI_2)) {
    angle = float(M_PI) - angle;
  }

  /* For when view is parallel to constraint... will cause NaNs otherwise
   * So we take vertical motion in 3D space and apply it to the
   * constraint axis. Nice for camera grab + MMB. */
  if (angle < DEG2RADF(5.0f)) {
    project_v3_v3v3(vec, in, t->viewinv[1]);
    factor = dot_v3v3(t->viewinv[1], vec) * 2.0f;
    /* Since camera distance is quite relative, use quadratic relationship.
     * holding shift can compensate. */
    if (factor < 0.0f) {
      factor *= -factor;
    }
    else {
      factor *= factor;
    }

    /* -factor makes move down going backwards. */
    normalize_v3_v3_length(out, axis, -factor);
  }
  else {
    float v[3];
    float norm[3], norm_center[3];
    float plane[3];

    view_vector_calc(t, t_con_center, norm_center);
    cross_v3_v3v3(plane, norm_center, axis);

    project_v3_v3v3(vec, in, plane);
    sub_v3_v3v3(vec, in, vec);

    add_v3_v3v3(v, vec, t_con_center);
    view_vector_calc(t, v, norm);

    /* Give arbitrary large value if projection is impossible. */
    factor = dot_v3v3(axis, norm);
    if (1.0f - fabsf(factor) < 0.0002f) {
      copy_v3_v3(out, axis);
      if (factor > 0) {
        mul_v3_fl(out, 1000000000.0f);
      }
      else {
        mul_v3_fl(out, -1000000000.0f);
      }
    }
    else {
      /* Use ray-ray intersection instead of line-line because this gave
       * precision issues adding small values to large numbers. */
      float mul;
      if (isect_ray_ray_v3(t_con_center, axis, v, norm, &mul, nullptr)) {
        mul_v3_v3fl(out, axis, mul);
      }
      else {
        /* In practice this should never fail. */
        BLI_assert(0);
      }

      /* Possible some values become nan when viewpoint and object are both zero. */
      if (!isfinite(out[0])) {
        out[0] = 0.0f;
      }
      if (!isfinite(out[1])) {
        out[1] = 0.0f;
      }
      if (!isfinite(out[2])) {
        out[2] = 0.0f;
      }
    }
  }
}

/**
 * Snap to the intersection between the edge direction and the constraint plane.
 */
static void constraint_snap_plane_to_edge(const TransInfo *t,
                                          const float plane_no[3],
                                          float r_out[3])
{
  float lambda;
  const float *edge_snap_point = t->tsnap.snap_target;
  const float *edge_dir = t->tsnap.snapNormal;
  bool is_aligned = fabsf(dot_v3v3(edge_dir, plane_no)) < CONSTRAIN_EPSILON;
  if (!is_aligned && isect_ray_plane_v3_factor(
                         edge_snap_point, edge_dir, t->tsnap.snap_source, plane_no, &lambda))
  {
    madd_v3_v3v3fl(r_out, edge_snap_point, edge_dir, lambda);
    sub_v3_v3(r_out, t->tsnap.snap_source);
  }
}

static void UNUSED_FUNCTION(constraint_snap_plane_to_face(const TransInfo *t,
                                                          const float plane[4],
                                                          float r_out[3]))
{
  float face_plane[4], isect_orig[3], isect_dir[3];
  const float *face_snap_point = t->tsnap.snap_target;
  const float *face_normal = t->tsnap.snapNormal;
  plane_from_point_normal_v3(face_plane, face_snap_point, face_normal);
  bool is_aligned = fabsf(dot_v3v3(plane, face_plane)) > (1.0f - CONSTRAIN_EPSILON);
  if (!is_aligned && isect_plane_plane_v3(plane, face_plane, isect_orig, isect_dir)) {
    closest_to_ray_v3(r_out, face_snap_point, isect_orig, isect_dir);
    sub_v3_v3(r_out, t->tsnap.snap_source);
  }
}

void transform_constraint_snap_axis_to_edge(const TransInfo *t,
                                            const float axis[3],
                                            float r_out[3])
{
  float lambda;
  const float *edge_snap_point = t->tsnap.snap_target;
  const float *edge_dir = t->tsnap.snapNormal;
  bool is_aligned = fabsf(dot_v3v3(axis, edge_dir)) > (1.0f - CONSTRAIN_EPSILON);
  if (!is_aligned &&
      isect_ray_ray_v3(t->tsnap.snap_source, axis, edge_snap_point, edge_dir, &lambda, nullptr))
  {
    mul_v3_v3fl(r_out, axis, lambda);
  }
}

void transform_constraint_snap_axis_to_face(const TransInfo *t,
                                            const float axis[3],
                                            float r_out[3])
{
  float lambda;
  const float *face_snap_point = t->tsnap.snap_target;
  const float *face_normal = t->tsnap.snapNormal;
  bool is_aligned = fabsf(dot_v3v3(axis, face_normal)) < CONSTRAIN_EPSILON;
  if (!is_aligned &&
      isect_ray_plane_v3_factor(t->tsnap.snap_source, axis, face_snap_point, face_normal, &lambda))
  {
    mul_v3_v3fl(r_out, axis, lambda);
  }
}

/**
 * Return true if the 2x axis are both aligned when projected into the view.
 * In this case, we can't usefully project the cursor onto the plane.
 */
static bool isPlaneProjectionViewAligned(const TransInfo *t, const float plane_no[3])
{
  const float eps = 0.001f;
  float view_to_plane[3];
  view_vector_calc(t, t->center_global, view_to_plane);

  float factor = dot_v3v3(plane_no, view_to_plane);
  return fabsf(factor) < eps;
}

static void planeProjection(const TransInfo *t,
                            const float plane_no[3],
                            const float in[3],
                            float out[3])
{

  float pos[3], view_vec[3], factor;

  add_v3_v3v3(pos, in, t->center_global);
  view_vector_calc(t, pos, view_vec);

  if (isect_ray_plane_v3_factor(pos, view_vec, t->center_global, plane_no, &factor)) {
    madd_v3_v3v3fl(out, in, view_vec, factor);
  }
}

static short transform_orientation_or_default(const TransInfo *t)
{
  short orientation = t->orient[t->orient_curr].type;
  if (orientation == V3D_ORIENT_CUSTOM_MATRIX) {
    /* Use the real value of the "orient_type". */
    orientation = t->orient[O_DEFAULT].type;
  }
  return orientation;
}

static const float (*transform_object_axismtx_get(const TransInfo *t,
                                                  const TransDataContainer *tc,
                                                  const TransData *td))[3]
{
  if (transform_orientation_or_default(t) == V3D_ORIENT_GIMBAL) {
    BLI_assert(t->orient_type_mask & (1 << V3D_ORIENT_GIMBAL));
    if (t->options & (CTX_POSE_BONE | CTX_OBJECT)) {
      TransDataExtension *td_ext = &tc->data_ext[td - tc->data];
      return td_ext->axismtx_gimbal;
    }
  }
  return td->axismtx;
}

void transform_constraint_get_nearest(const TransInfo *t, const float3 &vec, float r_vec[3])
{
  bool is_snap_to_point = false, is_snap_to_edge = false, is_snap_to_face = false;

  if (transform_snap_is_active(t)) {
    if (validSnap(t)) {
      is_snap_to_edge = (t->tsnap.target_type & SCE_SNAP_TO_EDGE) != 0;
      is_snap_to_face = (t->tsnap.target_type & SCE_SNAP_TO_FACE) != 0;
      is_snap_to_point = !is_snap_to_edge && !is_snap_to_face;
    }
  }

  /* Fallback for when axes are aligned. */
  mul_v3_m3v3(r_vec, t->con.pmtx, vec);

  if (is_snap_to_point) {
    /* Pass. With snap points, a projection is alright, no adjustments needed. */
  }
  else {
    const int dims = getConstraintSpaceDimension(t);
    if (dims == 2) {
      if (!is_zero_v3(r_vec)) {
        float plane_no[3];
        constraint_plane_normal_calc(t, plane_no);

        if (is_snap_to_edge) {
          constraint_snap_plane_to_edge(t, plane_no, r_vec);
        }
        else if (is_snap_to_face) {
          /* Disabled, as it has not proven to be really useful. (See #82386). */
          // constraint_snap_plane_to_face(t, plane, out);
        }
        else if (!isPlaneProjectionViewAligned(t, plane_no)) {
          /* View alignment correction. */
          planeProjection(t, plane_no, vec, r_vec);
        }
      }
    }
    else if (dims == 1) {
      float c[3];

      if (t->con.mode & CON_AXIS0) {
        copy_v3_v3(c, t->spacemtx[0]);
      }
      else if (t->con.mode & CON_AXIS1) {
        copy_v3_v3(c, t->spacemtx[1]);
      }
      else {
        BLI_assert(t->con.mode & CON_AXIS2);
        copy_v3_v3(c, t->spacemtx[2]);
      }

      if (is_snap_to_edge) {
        transform_constraint_snap_axis_to_edge(t, c, r_vec);
      }
      else if (is_snap_to_face) {
        transform_constraint_snap_axis_to_face(t, c, r_vec);
      }
      else {
        /* View alignment correction. */
        axisProjection(t, c, vec, r_vec);
      }
    }
  }
}

/**
 * Generic callback for constant spatial constraints applied to linear motion
 *
 * The `in` vector in projected into the constrained space and then further
 * projected along the view vector.
 * (in perspective mode, the view vector is relative to the position on screen)
 */
static void applyAxisConstraintVec(const TransInfo *t,
                                   const TransDataContainer * /*tc*/,
                                   const TransData *td,
                                   const float in[3],
                                   float out[3])
{
  if (td || !(t->con.mode & CON_APPLY)) {
    copy_v3_v3(out, in);
    return;
  }

  transform_constraint_get_nearest(t, in, out);
}

/**
 * Generic callback for object based spatial constraints applied to linear motion
 *
 * At first, the following is applied without orientation
 * The IN vector in projected into the constrained space and then further
 * projected along the view vector.
 * (in perspective mode, the view vector is relative to the position on screen).
 *
 * Further down, that vector is mapped to each data's space.
 */
static void applyObjectConstraintVec(const TransInfo *t,
                                     const TransDataContainer *tc,
                                     const TransData *td,
                                     const float in[3],
                                     float out[3])
{
  if (!td) {
    applyAxisConstraintVec(t, tc, td, in, out);
  }
  else {
    /* Specific TransData's space. */
    copy_v3_v3(out, in);
    if (t->con.mode & CON_APPLY) {
      mul_m3_v3(t->spacemtx_inv, out);
      const float(*axismtx)[3] = transform_object_axismtx_get(t, tc, td);
      mul_m3_v3(axismtx, out);
      if (t->flag & T_EDIT) {
        mul_m3_v3(tc->mat3_unit, out);
      }
    }
  }
}

/**
 * Generic callback for constant spatial constraints applied to resize motion.
 */
static void applyAxisConstraintSize(const TransInfo *t,
                                    const TransDataContainer * /*tc*/,
                                    const TransData *td,
                                    float r_smat[3][3])
{
  if (!td && t->con.mode & CON_APPLY) {
    float tmat[3][3];

    if (!(t->con.mode & CON_AXIS0)) {
      r_smat[0][0] = 1.0f;
    }
    if (!(t->con.mode & CON_AXIS1)) {
      r_smat[1][1] = 1.0f;
    }
    if (!(t->con.mode & CON_AXIS2)) {
      r_smat[2][2] = 1.0f;
    }

    mul_m3_m3m3(tmat, r_smat, t->spacemtx_inv);
    mul_m3_m3m3(r_smat, t->spacemtx, tmat);
  }
}

/**
 * Callback for object based spatial constraints applied to resize motion.
 */
static void applyObjectConstraintSize(const TransInfo *t,
                                      const TransDataContainer *tc,
                                      const TransData *td,
                                      float r_smat[3][3])
{
  if (td && t->con.mode & CON_APPLY) {
    float tmat[3][3];
    float imat[3][3];

    const float(*axismtx)[3] = transform_object_axismtx_get(t, tc, td);
    invert_m3_m3(imat, axismtx);

    if (!(t->con.mode & CON_AXIS0)) {
      r_smat[0][0] = 1.0f;
    }
    if (!(t->con.mode & CON_AXIS1)) {
      r_smat[1][1] = 1.0f;
    }
    if (!(t->con.mode & CON_AXIS2)) {
      r_smat[2][2] = 1.0f;
    }

    mul_m3_m3m3(tmat, r_smat, imat);
    if (t->flag & T_EDIT) {
      mul_m3_m3m3(r_smat, tc->mat3_unit, r_smat);
    }
    mul_m3_m3m3(r_smat, axismtx, tmat);
  }
}

static void constraints_rotation_impl(const TransInfo *t,
                                      const float axismtx[3][3],
                                      float r_axis[3])
{
  BLI_assert(t->con.mode & CON_APPLY);
  int mode = t->con.mode & (CON_AXIS0 | CON_AXIS1 | CON_AXIS2);

  switch (mode) {
    case CON_AXIS0:
    case (CON_AXIS1 | CON_AXIS2):
      copy_v3_v3(r_axis, axismtx[0]);
      break;
    case CON_AXIS1:
    case (CON_AXIS0 | CON_AXIS2):
      copy_v3_v3(r_axis, axismtx[1]);
      break;
    case CON_AXIS2:
    case (CON_AXIS0 | CON_AXIS1):
      copy_v3_v3(r_axis, axismtx[2]);
      break;
  }
}

/**
 * Generic callback for constant spatial constraints applied to rotations
 *
 * The rotation axis is copied into `vec`.
 *
 * In the case of single axis constraints, the rotation axis is directly the one constrained to.
 * For planar constraints (2 axis), the rotation axis is the normal of the plane.
 */
static void applyAxisConstraintRot(const TransInfo *t,
                                   const TransDataContainer * /*tc*/,
                                   const TransData *td,
                                   float r_axis[3])
{
  if (!td && t->con.mode & CON_APPLY) {
    constraints_rotation_impl(t, t->spacemtx, r_axis);
  }
}

/**
 * Callback for object based spatial constraints applied to rotations
 *
 * The rotation axis is copied into `vec`.
 *
 * In the case of single axis constraints, the rotation axis is directly the one constrained to.
 * For planar constraints (2 axis), the rotation axis is the normal of the plane.
 */
static void applyObjectConstraintRot(const TransInfo *t,
                                     const TransDataContainer *tc,
                                     const TransData *td,
                                     float r_axis[3])
{
  if (t->con.mode & CON_APPLY) {
    float tmp_axismtx[3][3];
    const float(*axismtx)[3];

    /* On setup call, use first object. */
    if (td == nullptr) {
      BLI_assert(tc == nullptr);
      tc = TRANS_DATA_CONTAINER_FIRST_OK(t);
      td = tc->data;
    }

    if (t->flag & T_EDIT) {
      mul_m3_m3m3(tmp_axismtx, tc->mat3_unit, td->axismtx);
      axismtx = tmp_axismtx;
    }
    else {
      axismtx = transform_object_axismtx_get(t, tc, td);
    }

    constraints_rotation_impl(t, axismtx, r_axis);
  }
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Internal Setup Calls
 * \{ */

void setConstraint(TransInfo *t, int mode, const char text[])
{
  BLI_strncpy_utf8(t->con.text + 1, text, sizeof(t->con.text) - 1);
  t->con.mode = eTConstraint(mode);
  projection_matrix_calc(t, t->con.pmtx);

  startConstraint(t);

  t->con.drawExtra = nullptr;
  t->con.applyVec = applyAxisConstraintVec;
  t->con.applySize = applyAxisConstraintSize;
  t->con.applyRot = applyAxisConstraintRot;
  t->redraw = TREDRAW_HARD;
}

void setAxisMatrixConstraint(TransInfo *t, int mode, const char text[])
{
  BLI_strncpy_utf8(t->con.text + 1, text, sizeof(t->con.text) - 1);
  t->con.mode = eTConstraint(mode);
  projection_matrix_calc(t, t->con.pmtx);

  startConstraint(t);

  t->con.drawExtra = drawObjectConstraint;
  t->con.applyVec = applyObjectConstraintVec;
  t->con.applySize = applyObjectConstraintSize;
  t->con.applyRot = applyObjectConstraintRot;
  t->redraw = TREDRAW_HARD;
}

void setLocalConstraint(TransInfo *t, int mode, const char text[])
{
  if ((t->flag & T_EDIT) || t->data_len_all == 1) {
    /* Although in edit-mode each object has its local space, use the
     * orientation of the active object. */
    setConstraint(t, mode, text);
  }
  else {
    setAxisMatrixConstraint(t, mode, text);
  }
}

void setUserConstraint(TransInfo *t, int mode, const char text_[])
{
  char text[256];
  const short orientation = transform_orientation_or_default(t);
  const char *spacename = transform_orientations_spacename_get(t, orientation);
  SNPRINTF_UTF8(text, text_, spacename);

  switch (orientation) {
    case V3D_ORIENT_LOCAL:
    case V3D_ORIENT_GIMBAL:
      setLocalConstraint(t, mode, text);
      break;
    case V3D_ORIENT_NORMAL:
      if (checkUseAxisMatrix(t)) {
        setAxisMatrixConstraint(t, mode, text);
        break;
      }
      ATTR_FALLTHROUGH;
    case V3D_ORIENT_GLOBAL:
    case V3D_ORIENT_VIEW:
    case V3D_ORIENT_CURSOR:
    case V3D_ORIENT_CUSTOM_MATRIX:
    case V3D_ORIENT_CUSTOM:
    default: {
      setConstraint(t, mode, text);
      break;
    }
  }
  t->con.mode |= CON_USER;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Drawing Constraints
 * \{ */

static void drawLine(
    TransInfo *t, const float center[3], const float dir[3], char axis, short options)
{
  if (!ELEM(t->spacetype, SPACE_VIEW3D, SPACE_SEQ)) {
    return;
  }

  float v1[3], v2[3], v3[3];
  uchar col[3], col2[3];

  if (t->spacetype == SPACE_VIEW3D) {
    View3D *v3d = static_cast<View3D *>(t->view);

    copy_v3_v3(v3, dir);
    mul_v3_fl(v3, v3d->clip_end);

    sub_v3_v3v3(v2, center, v3);
    add_v3_v3v3(v1, center, v3);
  }
  else if (t->spacetype == SPACE_SEQ) {
    View2D *v2d = static_cast<View2D *>(t->view);

    copy_v3_v3(v3, dir);
    float max_dist = max_ff(BLI_rctf_size_x(&v2d->cur), BLI_rctf_size_y(&v2d->cur));
    mul_v3_fl(v3, max_dist);

    sub_v3_v3v3(v2, center, v3);
    add_v3_v3v3(v1, center, v3);
  }

  GPU_matrix_push();

  if (options & DRAWLIGHT) {
    col[0] = col[1] = col[2] = 220;
  }
  else {
    UI_GetThemeColor3ubv(TH_GRID, col);
  }
  UI_make_axis_color(col, axis, col2);

  uint pos = GPU_vertformat_attr_add(
      immVertexFormat(), "pos", blender::gpu::VertAttrType::SFLOAT_32_32_32);

  float viewport[4];
  GPU_viewport_size_get_f(viewport);
  GPU_blend(GPU_BLEND_ALPHA);

  immBindBuiltinProgram(GPU_SHADER_3D_POLYLINE_UNIFORM_COLOR);
  immUniform2fv("viewportSize", &viewport[2]);
  immUniform1f("lineWidth", U.pixelsize * 2.0f);

  immUniformColor3ubv(col2);

  immBegin(GPU_PRIM_LINES, 2);
  immVertex3fv(pos, v1);
  immVertex3fv(pos, v2);
  immEnd();

  immUnbindProgram();

  GPU_matrix_pop();
}

void drawConstraint(TransInfo *t)
{
  TransCon *tc = &(t->con);

  if (!ELEM(t->spacetype, SPACE_VIEW3D, SPACE_IMAGE, SPACE_NODE, SPACE_SEQ)) {
    return;
  }
  if (!(tc->mode & CON_APPLY)) {
    return;
  }
  if (t->flag & T_NO_CONSTRAINT) {
    return;
  }

  if (tc->drawExtra) {
    tc->drawExtra(t);
  }
  else {
    if (tc->mode & CON_SELECT) {
      float vec[3];

      convertViewVec(t, vec, (t->mval[0] - t->mouse.imval[0]), (t->mval[1] - t->mouse.imval[1]));
      add_v3_v3(vec, t->center_global);

      drawLine(t, t->center_global, t->spacemtx[0], 'X', 0);
      drawLine(t, t->center_global, t->spacemtx[1], 'Y', 0);
      drawLine(t, t->center_global, t->spacemtx[2], 'Z', 0);

      eGPUDepthTest depth_test_enabled = GPU_depth_test_get();
      if (depth_test_enabled) {
        GPU_depth_test(GPU_DEPTH_NONE);
      }

      const uint shdr_pos = GPU_vertformat_attr_add(
          immVertexFormat(), "pos", blender::gpu::VertAttrType::SFLOAT_32_32_32);

      immBindBuiltinProgram(GPU_SHADER_3D_LINE_DASHED_UNIFORM_COLOR);

      float viewport_size[4];
      GPU_viewport_size_get_f(viewport_size);
      immUniform2f("viewport_size", viewport_size[2], viewport_size[3]);

      immUniform1i("colors_len", 0); /* "simple" mode. */
      immUniformColor4f(1.0f, 1.0f, 1.0f, 1.0f);
      immUniform1f("dash_width", 2.0f);
      immUniform1f("udash_factor", 0.5f);

      immBegin(GPU_PRIM_LINES, 2);
      immVertex3fv(shdr_pos, t->center_global);
      immVertex3fv(shdr_pos, vec);
      immEnd();

      immUnbindProgram();

      if (depth_test_enabled) {
        GPU_depth_test(GPU_DEPTH_LESS_EQUAL);
      }
    }

    if (tc->mode & CON_AXIS0) {
      drawLine(t, t->center_global, t->spacemtx[0], 'X', DRAWLIGHT);
    }
    if (tc->mode & CON_AXIS1) {
      drawLine(t, t->center_global, t->spacemtx[1], 'Y', DRAWLIGHT);
    }
    if (tc->mode & CON_AXIS2) {
      drawLine(t, t->center_global, t->spacemtx[2], 'Z', DRAWLIGHT);
    }
  }
}

void drawPropCircle(TransInfo *t)
{
  if (t->flag & T_PROP_EDIT) {
    const RegionView3D *rv3d = nullptr;
    float tmat[4][4], imat[4][4];

    if (t->spacetype == SPACE_VIEW3D) {
      if (t->region && (t->region->regiontype == RGN_TYPE_WINDOW)) {
        rv3d = static_cast<const RegionView3D *>(t->region->regiondata);
      }
    }

    if (rv3d != nullptr) {
      copy_m4_m4(tmat, rv3d->viewmat);
      invert_m4_m4(imat, tmat);
    }
    else {
      unit_m4(tmat);
      unit_m4(imat);
    }

    GPU_matrix_push();

    if (t->spacetype == SPACE_VIEW3D) {
      /* Pass. */
    }
    else if (t->spacetype == SPACE_IMAGE) {
      GPU_matrix_scale_2f(1.0f / t->aspect[0], 1.0f / t->aspect[1]);
    }

    eGPUDepthTest depth_test_enabled = GPU_depth_test_get();
    if (depth_test_enabled) {
      GPU_depth_test(GPU_DEPTH_NONE);
    }

    uint pos = GPU_vertformat_attr_add(
        immVertexFormat(), "pos", blender::gpu::VertAttrType::SFLOAT_32_32_32);

    immBindBuiltinProgram(GPU_SHADER_3D_POLYLINE_UNIFORM_COLOR);

    float viewport[4];
    GPU_viewport_size_get_f(viewport);
    GPU_blend(GPU_BLEND_ALPHA);

    immUniform2fv("viewportSize", &viewport[2]);
    immUniform1f("lineWidth", 3.0f * U.pixelsize);

    immUniformThemeColorShadeAlpha(TH_GRID, -20, 255);
    imm_drawcircball(t->center_global, t->prop_size, imat, pos);

    immUniform1f("lineWidth", 1.0f * U.pixelsize);
    immUniformThemeColorShadeAlpha(TH_GRID, 20, 255);
    imm_drawcircball(t->center_global, t->prop_size, imat, pos);

    immUnbindProgram();

    if (depth_test_enabled) {
      GPU_depth_test(GPU_DEPTH_LESS_EQUAL);
    }

    GPU_matrix_pop();
  }
}

void drawPropRange(TransInfo *t)
{
  if ((t->flag & T_PROP_EDIT) == 0) {
    return;
  }

  uint pos = GPU_vertformat_attr_add(
      immVertexFormat(), "pos", blender::gpu::VertAttrType::SFLOAT_32_32_32);

  immBindBuiltinProgram(GPU_SHADER_3D_POLYLINE_UNIFORM_COLOR);

  float viewport[4];
  GPU_viewport_size_get_f(viewport);
  GPU_blend(GPU_BLEND_ALPHA);

  immUniform2fv("viewportSize", &viewport[2]);

  View2D *v2d = &t->region->v2d;
  const float x1 = t->center_global[0] - t->prop_size;
  const float y1 = v2d->cur.ymin;
  const float x2 = t->center_global[0] + t->prop_size;
  const float y2 = v2d->cur.ymax;

  immUniform1f("lineWidth", 3.0f * U.pixelsize);
  immUniformThemeColorShadeAlpha(TH_GRID, -20, 255);
  imm_draw_box_wire_3d(pos, x1, y1, x2, y2);

  immUniform1f("lineWidth", 1.0f * U.pixelsize);
  immUniformThemeColorShadeAlpha(TH_GRID, 20, 255);
  imm_draw_box_wire_3d(pos, x1, y1, x2, y2);

  immUnbindProgram();
  GPU_blend(GPU_BLEND_NONE);
}

static void drawObjectConstraint(TransInfo *t)
{
  /* Draw the first one lighter because that's the one who controls the others.
   * Meaning the transformation is projected on that one and just copied on the others
   * constraint space.
   * In a nutshell, the object with light axis is controlled by the user and the others follow.
   * Without drawing the first light, users have little clue what they are doing.
   */
  short options = DRAWLIGHT;
  float tmp_axismtx[3][3];

  FOREACH_TRANS_DATA_CONTAINER (t, tc) {
    TransData *td = tc->data;
    for (int i = 0; i < tc->data_len; i++, td++) {
      float co[3];
      const float(*axismtx)[3];

      if (t->flag & T_PROP_EDIT) {
        /* We're sorted, so skip the rest. */
        if (td->factor == 0.0f) {
          break;
        }
      }

      if (t->options & CTX_GPENCIL_STROKES) {
        /* Only draw a constraint line for one point, otherwise we can't see anything. */
        if ((options & DRAWLIGHT) == 0) {
          break;
        }
      }

      if (t->options & CTX_SEQUENCER_IMAGE) {
        /* Because we construct an "L" shape to deform the sequence, we should skip
         * all points except the first vertex. Otherwise we will draw the same axis constraint line
         * 3 times for each strip.
         */
        if (i % 3 != 0) {
          continue;
        }
      }

      if (t->flag & T_EDIT) {
        mul_v3_m4v3(co, tc->mat, td->center);

        mul_m3_m3m3(tmp_axismtx, tc->mat3_unit, td->axismtx);
        axismtx = tmp_axismtx;
      }
      else {
        if (t->options & CTX_POSE_BONE) {
          mul_v3_m4v3(co, tc->mat, td->center);
        }
        else {
          copy_v3_v3(co, td->center);
        }
        axismtx = transform_object_axismtx_get(t, tc, td);
      }

      if (t->con.mode & CON_AXIS0) {
        drawLine(t, co, axismtx[0], 'X', options);
      }
      if (t->con.mode & CON_AXIS1) {
        drawLine(t, co, axismtx[1], 'Y', options);
      }
      if (t->con.mode & CON_AXIS2) {
        drawLine(t, co, axismtx[2], 'Z', options);
      }
      options &= ~DRAWLIGHT;
    }
  }
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Start / Stop Constraints
 * \{ */

void startConstraint(TransInfo *t)
{
  t->con.mode |= CON_APPLY;
  *t->con.text = ' ';
  t->num.idx_max = min_ii(getConstraintSpaceDimension(t) - 1, t->idx_max);
}

void stopConstraint(TransInfo *t)
{
  if (t->orient_curr != O_DEFAULT) {
    transform_orientations_current_set(t, O_DEFAULT);
  }

  t->con.mode &= ~(CON_APPLY | CON_SELECT);
  *t->con.text = '\0';
  t->num.idx_max = t->idx_max;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Middle Mouse Button Select
 * \{ */

void initSelectConstraint(TransInfo *t)
{
  if (t->orient_curr == O_DEFAULT) {
    transform_orientations_current_set(t, O_SCENE);
  }

  setUserConstraint(t, CON_APPLY | CON_SELECT, "%s");
}

void selectConstraint(TransInfo *t)
{
  if (t->con.mode & CON_SELECT) {
    setNearestAxis(t);
    startConstraint(t);
  }
}

void postSelectConstraint(TransInfo *t)
{
  t->con.mode &= ~CON_SELECT;
  if (!(t->con.mode & (CON_AXIS0 | CON_AXIS1 | CON_AXIS2))) {
    t->con.mode &= ~CON_APPLY;
  }
}

static void setNearestAxis2d(TransInfo *t)
{
  /* Clear any prior constraint flags. */
  t->con.mode &= ~(CON_AXIS0 | CON_AXIS1 | CON_AXIS2);

  /* No correction needed... just use whichever one is lower. */
  float2 dvec = t->mval - t->mouse.imval;
  if (abs(dvec.x) < abs(dvec.y)) {
    t->con.mode |= CON_AXIS1;
    STRNCPY_UTF8(t->con.text, IFACE_(" along Y axis"));
  }
  else {
    t->con.mode |= CON_AXIS0;
    STRNCPY_UTF8(t->con.text, IFACE_(" along X axis"));
  }
}

static void setNearestAxis3d(TransInfo *t)
{
  /* Clear any prior constraint flags. */
  t->con.mode &= ~(CON_AXIS0 | CON_AXIS1 | CON_AXIS2);

  float zfac;
  float mvec[3], proj[3];
  float len[3];
  int i;

  /* Calculate mouse movement. */
  mvec[0] = t->mval[0] - t->mouse.imval[0];
  mvec[1] = t->mval[1] - t->mouse.imval[1];
  mvec[2] = 0.0f;

  /* We need to correct axis length for the current zoom-level of view,
   * this to prevent projected values to be clipped behind the camera
   * and to overflow the short integers.
   * The formula used is a bit stupid, just a simplification of the subtraction
   * of two 2D points 30 pixels apart (that's the last factor in the formula) after
   * projecting them with #ED_view3d_win_to_delta and then get the length of that vector. */
  zfac = mul_project_m4_v3_zfac(t->persmat, t->center_global);
  zfac = len_v3(t->persinv[0]) * 2.0f / t->region->winx * zfac * 30.0f;

  for (i = 0; i < 3; i++) {
    float axis[3], axis_2d[2];

    copy_v3_v3(axis, t->spacemtx[i]);

    mul_v3_fl(axis, zfac);
    /* Now we can project to get window coordinate. */
    add_v3_v3(axis, t->center_global);
    projectFloatView(t, axis, axis_2d);

    sub_v2_v2v2(axis, axis_2d, t->center2d);
    axis[2] = 0.0f;

    if (normalize_v3(axis) > 1e-3f) {
      project_v3_v3v3(proj, mvec, axis);
      sub_v3_v3v3(axis, mvec, proj);
      len[i] = normalize_v3(axis);
    }
    else {
      len[i] = 1e10f;
    }
  }

  if (len[0] <= len[1] && len[0] <= len[2]) {
    if (t->modifiers & MOD_CONSTRAINT_SELECT_PLANE) {
      t->con.mode |= (CON_AXIS1 | CON_AXIS2);
      SNPRINTF_UTF8(t->con.text, IFACE_(" locking %s X axis"), t->spacename);
    }
    else {
      t->con.mode |= CON_AXIS0;
      SNPRINTF_UTF8(t->con.text, IFACE_(" along %s X axis"), t->spacename);
    }
  }
  else if (len[1] <= len[0] && len[1] <= len[2]) {
    if (t->modifiers & MOD_CONSTRAINT_SELECT_PLANE) {
      t->con.mode |= (CON_AXIS0 | CON_AXIS2);
      SNPRINTF_UTF8(t->con.text, IFACE_(" locking %s Y axis"), t->spacename);
    }
    else {
      t->con.mode |= CON_AXIS1;
      SNPRINTF_UTF8(t->con.text, IFACE_(" along %s Y axis"), t->spacename);
    }
  }
  else if (len[2] <= len[1] && len[2] <= len[0]) {
    if (t->modifiers & MOD_CONSTRAINT_SELECT_PLANE) {
      t->con.mode |= (CON_AXIS0 | CON_AXIS1);
      SNPRINTF_UTF8(t->con.text, IFACE_(" locking %s Z axis"), t->spacename);
    }
    else {
      t->con.mode |= CON_AXIS2;
      SNPRINTF_UTF8(t->con.text, IFACE_(" along %s Z axis"), t->spacename);
    }
  }
}

void setNearestAxis(TransInfo *t)
{
  eTConstraint mode_prev = t->con.mode;

  /* Constraint setting - depends on spacetype. */
  if (t->spacetype == SPACE_VIEW3D) {
    /* 3d-view. */
    setNearestAxis3d(t);
  }
  else {
    /* Assume that this means a 2D-Editor. */
    setNearestAxis2d(t);
  }

  if (mode_prev != t->con.mode) {
    projection_matrix_calc(t, t->con.pmtx);
    transform_gizmo_3d_model_from_constraint_and_mode_set(t);
  }
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Helper Functions
 * \{ */

int constraintModeToIndex(const TransInfo *t)
{
  if ((t->con.mode & CON_APPLY) == 0) {
    return -1;
  }
  switch (int(t->con.mode & (CON_AXIS0 | CON_AXIS1 | CON_AXIS2))) {
    case (CON_AXIS0):
    case (CON_AXIS1 | CON_AXIS2):
      return 0;
    case (CON_AXIS1):
    case (CON_AXIS0 | CON_AXIS2):
      return 1;
    case (CON_AXIS2):
    case (CON_AXIS0 | CON_AXIS1):
      return 2;
    default:
      return -1;
  }
}

bool isLockConstraint(const TransInfo *t)
{
  int mode = t->con.mode;

  if ((mode & (CON_AXIS0 | CON_AXIS1)) == (CON_AXIS0 | CON_AXIS1)) {
    return true;
  }

  if ((mode & (CON_AXIS1 | CON_AXIS2)) == (CON_AXIS1 | CON_AXIS2)) {
    return true;
  }

  if ((mode & (CON_AXIS0 | CON_AXIS2)) == (CON_AXIS0 | CON_AXIS2)) {
    return true;
  }

  return false;
}

int getConstraintSpaceDimension(const TransInfo *t)
{
  int n = 0;

  if (t->con.mode & CON_AXIS0) {
    n++;
  }

  if (t->con.mode & CON_AXIS1) {
    n++;
  }

  if (t->con.mode & CON_AXIS2) {
    n++;
  }

  return n;
  /* Someone willing to do it cryptically could do the following instead:
   *
   * `return t->con & (CON_AXIS0|CON_AXIS1|CON_AXIS2);`
   *
   * Based on the assumptions that the axis flags are one after the other and start at 1
   */
}

/** \} */

}  // namespace blender::ed::transform
