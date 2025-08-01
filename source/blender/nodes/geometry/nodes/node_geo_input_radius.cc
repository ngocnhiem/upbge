/* SPDX-FileCopyrightText: 2023 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "node_geometry_util.hh"

namespace blender::nodes::node_geo_input_radius_cc {

static void node_declare(NodeDeclarationBuilder &b)
{
  b.add_output<decl::Float>("Radius").default_value(1.0f).min(0.0f).field_source();
}

static void node_geo_exec(GeoNodeExecParams params)
{
  Field<float> radius_field = AttributeFieldInput::from<float>("radius");
  params.set_output("Radius", std::move(radius_field));
}

static void node_register()
{
  static blender::bke::bNodeType ntype;

  geo_node_type_base(&ntype, "GeometryNodeInputRadius", GEO_NODE_INPUT_RADIUS);
  ntype.ui_name = "Radius";
  ntype.ui_description = "Retrieve the radius at each point on curve or point cloud geometry";
  ntype.enum_name_legacy = "INPUT_RADIUS";
  ntype.nclass = NODE_CLASS_INPUT;
  ntype.geometry_node_execute = node_geo_exec;
  ntype.declare = node_declare;
  blender::bke::node_register_type(ntype);
}
NOD_REGISTER_NODE(node_register)

}  // namespace blender::nodes::node_geo_input_radius_cc
