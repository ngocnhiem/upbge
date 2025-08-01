# SPDX-FileCopyrightText: 2017-2022 Blender Authors
#
# SPDX-License-Identifier: GPL-2.0-or-later

# ./blender.bin --background --python tests/python/bl_pyapi_idprop_datablock.py -- --verbose

import contextlib
import inspect
import io
import os
import re
import sys
import tempfile

import bpy

from bpy.types import UIList
arr_len = 100
ob_cp_count = 100

# Set before execution.
lib_path = None
test_path = None


def print_fail_msg_and_exit(msg):
    def __LINE__():
        try:
            raise Exception
        except:
            return sys.exc_info()[2].tb_frame.f_back.f_back.f_back.f_lineno

    def __FILE__():
        return inspect.currentframe().f_code.co_filename

    print("'%s': %d >> %s" % (__FILE__(), __LINE__(), msg), file=sys.stderr)
    sys.stderr.flush()
    sys.stdout.flush()
    os._exit(1)


def expect_false_or_abort(expr, msg=None):
    if not expr:
        if not msg:
            msg = "test failed"
        print_fail_msg_and_exit(msg)


def expect_exception_or_abort(*, fn, ex, finalize=None):
    try:
        fn()
        exception = False
    except ex:
        exception = True
    finally:
        if finalize:
            finalize()
    if exception:
        return  # OK
    print_fail_msg_and_exit("test failed")


def expect_output_or_abort(*, fn, match_stderr=None, match_stdout=None, finalize=None):

    stdout, stderr = io.StringIO(), io.StringIO()

    with (contextlib.redirect_stderr(stderr), contextlib.redirect_stdout(stdout)):
        fn()

    for (handle, match) in ((stdout, match_stdout), (stderr, match_stderr)):
        if not match:
            continue
        output = handle.getvalue()
        if not re.match(match, output):
            print_fail_msg_and_exit("%r not found in %r" % (match, output))

    if finalize:
        finalize()


class TestClass(bpy.types.PropertyGroup):
    test_prop: bpy.props.PointerProperty(type=bpy.types.Object)
    name: bpy.props.StringProperty()


def get_scene(lib_name, sce_name):
    for s in bpy.data.scenes:
        if s.name == sce_name:
            if (
                    (s.library and s.library.name == lib_name) or
                    (lib_name is None and s.library is None)
            ):
                return s


def init():
    bpy.utils.register_class(TestClass)
    bpy.types.Object.prop_array = bpy.props.CollectionProperty(
        name="prop_array",
        type=TestClass)
    bpy.types.Object.prop = bpy.props.PointerProperty(type=bpy.types.Object)


def finalize():
    del bpy.types.Object.prop_array
    del bpy.types.Object.prop
    bpy.utils.unregister_class(TestClass)


def make_lib():
    bpy.ops.wm.read_factory_settings()

    # datablock pointer to the Camera object
    bpy.data.objects["Cube"].prop = bpy.data.objects['Camera']

    # array of datablock pointers to the Light object
    for i in range(0, arr_len):
        a = bpy.data.objects["Cube"].prop_array.add()
        a.test_prop = bpy.data.objects['Light']
        a.name = a.test_prop.name

    # make unique named copy of the cube
    ob = bpy.data.objects["Cube"].copy()
    bpy.context.collection.objects.link(ob)

    bpy.data.objects["Cube.001"].name = "Unique_Cube"

    # duplicating of Cube
    for i in range(0, ob_cp_count):
        ob = bpy.data.objects["Cube"].copy()
        bpy.context.collection.objects.link(ob)

    # nodes
    tree = bpy.data.node_groups.new("Compositor Nodes", "CompositorNodeTree")
    bpy.data.scenes["Scene"].compositing_node_group = tree
    rlayers = tree.nodes.new(type="CompositorNodeRLayers")
    sys_idprops = rlayers.bl_system_properties_get(do_create=True)
    sys_idprops["prop"] = bpy.data.objects['Camera']

    # rename scene and save
    bpy.data.scenes["Scene"].name = "Scene_lib"
    bpy.ops.wm.save_as_mainfile(filepath=lib_path)


def check_lib():
    # check pointer
    expect_false_or_abort(bpy.data.objects["Cube"].prop == bpy.data.objects['Camera'])

    # check array of pointers in duplicated object
    for i in range(0, arr_len):
        expect_false_or_abort(
            bpy.data.objects["Cube.001"].prop_array[i].test_prop ==
            bpy.data.objects['Light'])


def check_lib_linking():
    # open startup file
    bpy.ops.wm.read_factory_settings()

    # link scene to the startup file
    with bpy.data.libraries.load(lib_path, link=True) as (data_from, data_to):
        data_to.scenes = ["Scene_lib"]

    bpy.context.window.scene = bpy.data.scenes["Scene_lib"]

    o = bpy.data.scenes["Scene_lib"].objects['Unique_Cube']

    expect_false_or_abort(o.prop_array[0].test_prop == bpy.data.scenes["Scene_lib"].objects['Light'])
    expect_false_or_abort(o.prop == bpy.data.scenes["Scene_lib"].objects['Camera'])
    expect_false_or_abort(o.prop.library == o.library)

    bpy.ops.wm.save_as_mainfile(filepath=test_path)


def check_linked_scene_copying():
    # full copy of the scene with datablock props
    bpy.ops.wm.open_mainfile(filepath=test_path)
    bpy.ops.scene.new(type='FULL_COPY')

    bpy.context.window.scene = get_scene("lib.blend", "Scene_lib")

    # check save/open
    bpy.ops.wm.save_as_mainfile(filepath=test_path)
    bpy.ops.wm.open_mainfile(filepath=test_path)

    intern_sce = get_scene(None, "Scene_lib")
    extern_sce = get_scene("lib.blend", "Scene_lib")

    # check node's props
    # must point to own scene camera
    intern_sys_idprops = intern_sce.node_tree.nodes['Render Layers'].bl_system_properties_get()
    extern_sys_idprops = extern_sce.node_tree.nodes['Render Layers'].bl_system_properties_get()
    expect_false_or_abort(
        intern_sys_idprops["prop"] and not (intern_sys_idprops["prop"] == extern_sys_idprops["prop"]))


def check_scene_copying():
    # full copy of the scene with datablock props
    bpy.ops.wm.open_mainfile(filepath=lib_path)
    bpy.context.window.scene = bpy.data.scenes["Scene_lib"]
    bpy.ops.scene.new(type='FULL_COPY')

    path = test_path + "_"
    # check save/open
    bpy.ops.wm.save_as_mainfile(filepath=path)
    bpy.ops.wm.open_mainfile(filepath=path)

    first_sce = get_scene(None, "Scene_lib")
    second_sce = get_scene(None, "Scene_lib.001")

    # check node's props
    # must point to own scene camera
    first_sys_idprops = first_sce.node_tree.nodes['Render Layers'].bl_system_properties_get()
    second_sys_idprops = second_sce.node_tree.nodes['Render Layers'].bl_system_properties_get()
    expect_false_or_abort(not (first_sys_idprops["prop"] == second_sys_idprops["prop"]))


# count users
def test_users_counting():
    bpy.ops.wm.read_factory_settings()
    Light_us = bpy.data.objects["Light"].data.users
    n = 1000
    for i in range(0, n):
        bpy.data.objects["Cube"]["a%s" % i] = bpy.data.objects["Light"].data
    expect_false_or_abort(bpy.data.objects["Light"].data.users == Light_us + n)

    for i in range(0, int(n / 2)):
        bpy.data.objects["Cube"]["a%s" % i] = 1
    expect_false_or_abort(bpy.data.objects["Light"].data.users == Light_us + int(n / 2))


# linking
def test_linking():
    make_lib()
    check_lib()
    check_lib_linking()
    check_linked_scene_copying()
    check_scene_copying()


# check restrictions for datablock pointers for some classes; GUI for manual testing
def test_restrictions1():
    class TEST_Op(bpy.types.Operator):
        bl_idname = 'scene.test_op'
        bl_label = 'Test'
        bl_options = {"INTERNAL"}

        str_prop: bpy.props.StringProperty(name="str_prop")

        # disallow registration of datablock properties in operators
        # will be checked in the draw method (test manually)
        # also, see console:
        #   ValueError: bpy_struct "SCENE_OT_test_op" doesn't support datablock properties
        id_prop: bpy.props.PointerProperty(type=bpy.types.Object)

        def execute(self, context):
            return {'FINISHED'}

    # just panel for testing the poll callback with lots of objects
    class TEST_PT_DatablockProp(bpy.types.Panel):
        bl_label = "Datablock IDProp"
        bl_space_type = 'PROPERTIES'
        bl_region_type = 'WINDOW'
        bl_context = "render"

        def draw(self, context):
            self.layout.prop_search(context.scene, "prop", bpy.data, "objects")
            self.layout.template_ID(context.scene, "prop1")
            self.layout.prop_search(context.scene, "prop2", bpy.data, "node_groups")

            op = self.layout.operator(TEST_Op.bl_idname)
            op.str_prop = "test string"

            def test_fn(op):
                op["ob"] = bpy.data.objects['Unique_Cube']
            expect_exception_or_abort(
                fn=lambda: test_fn(op),
                ex=ImportError,
            )
            expect_false_or_abort(not hasattr(op, "id_prop"))

    bpy.utils.register_class(TEST_PT_DatablockProp)
    expect_output_or_abort(
        fn=lambda: bpy.utils.register_class(TEST_Op),
        match_stderr="^ValueError: bpy_struct \"SCENE_OT_test_op\" registration error:",
        finalize=lambda: bpy.utils.unregister_class(TEST_Op),
    )
    bpy.utils.unregister_class(TEST_PT_DatablockProp)

    def poll(self, value):
        return value.name in bpy.data.scenes["Scene_lib"].objects

    def poll1(self, value):
        return True

    bpy.types.Scene.prop = bpy.props.PointerProperty(type=bpy.types.Object)
    bpy.types.Scene.prop1 = bpy.props.PointerProperty(type=bpy.types.Object, poll=poll)
    bpy.types.Scene.prop2 = bpy.props.PointerProperty(type=bpy.types.NodeTree, poll=poll1)

    # check poll effect on UI (poll returns false => red alert)
    bpy.context.scene.prop = bpy.data.objects["Light.001"]
    bpy.context.scene.prop1 = bpy.data.objects["Light.001"]

    # check incorrect type assignment
    def sub_test():
        # NodeTree id_prop
        bpy.context.scene.prop2 = bpy.data.objects["Light.001"]

    expect_exception_or_abort(
        fn=sub_test,
        ex=TypeError,
    )

    bpy.context.scene.prop2 = bpy.data.node_groups.new("Shader", "ShaderNodeTree")

    # NOTE: keep since the author thought this useful information.
    # print(
    #     "Please, test GUI performance manually on the Render tab, '%s' panel" %
    #     TEST_PT_DatablockProp.bl_label, file=sys.stderr,
    # )
    sys.stderr.flush()


# check some possible regressions
def test_regressions():
    bpy.types.Object.prop_str = bpy.props.StringProperty(name="str")
    bpy.data.objects["Unique_Cube"].prop_str = "test"

    bpy.types.Object.prop_gr = bpy.props.PointerProperty(
        name="prop_gr",
        type=TestClass,
        description="test")

    bpy.data.objects["Unique_Cube"].prop_gr = None


# test restrictions for datablock pointers
def test_restrictions2():
    class TestClassCollection(bpy.types.PropertyGroup):
        prop: bpy.props.CollectionProperty(
            name="prop_array",
            type=TestClass)
    bpy.utils.register_class(TestClassCollection)

    class TestPrefs(bpy.types.AddonPreferences):
        bl_idname = "testprefs"
        # expecting crash during registering
        my_prop2: bpy.props.PointerProperty(type=TestClass)

        prop: bpy.props.PointerProperty(
            name="prop",
            type=TestClassCollection,
            description="test")

    bpy.types.Addon.a = bpy.props.PointerProperty(type=bpy.types.Object)

    class TEST_UL_list(UIList):
        test: bpy.props.PointerProperty(type=bpy.types.Object)

        def draw_item(self, context, layout, data, item, icon, active_data, active_propname, index):
            layout.prop(item, "name", text="", emboss=False, icon_value=icon)

    expect_exception_or_abort(
        fn=lambda: bpy.utils.register_class(TestPrefs),
        ex=ValueError,
        finalize=lambda: bpy.utils.unregister_class(TestPrefs),
    )
    expect_exception_or_abort(
        fn=lambda: bpy.utils.register_class(TEST_UL_list),
        ex=ValueError,
        finalize=lambda: bpy.utils.unregister_class(TEST_UL_list),
    )

    bpy.utils.unregister_class(TestClassCollection)


def main():
    global lib_path
    global test_path
    with tempfile.TemporaryDirectory() as temp_dir:
        lib_path = os.path.join(temp_dir, "lib.blend")
        test_path = os.path.join(temp_dir, "test.blend")

        init()
        test_users_counting()
        test_linking()
        test_restrictions1()
        expect_exception_or_abort(
            fn=test_regressions,
            ex=AttributeError,
        )
        test_restrictions2()
        finalize()


if __name__ == "__main__":
    main()
