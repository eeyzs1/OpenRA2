# Blender batch renderer for OpenRA2 / RA2-style isometric unit frames.
# Usage:
#   blender --background --python tools/asset_pipeline/blender/ra2_iso_render.py -- \
#       --out workdir/renders --canvas 24 30 --supersample 2 --anim stand --frames 1
#
# Facing order matches art.ini / OpenRA2: facing 0 = East on screen, then CCW 45°.
# Camera: isometric-ish (ortho), light from upper-left (RA2 bake feel).

from __future__ import annotations

import argparse
import math
import os
import sys


def parse_args(argv):
    # Blender passes args after "--"
    if "--" in argv:
        argv = argv[argv.index("--") + 1:]
    else:
        argv = []
    p = argparse.ArgumentParser(description="RA2 isometric unit renderer")
    p.add_argument("--out", required=True, help="Output directory")
    p.add_argument("--canvas", nargs=2, type=int, default=[24, 30], metavar=("W", "H"))
    p.add_argument("--supersample", type=int, default=2, choices=[1, 2, 4])
    p.add_argument("--anim", default="stand", help="Animation folder name under out/")
    p.add_argument("--frames", type=int, default=1, help="Frames per facing")
    p.add_argument("--facings", type=int, default=8)
    p.add_argument("--demo", action="store_true", help="Create a simple capsule mesh if scene empty")
    return p.parse_args(argv)


def setup_scene(demo: bool):
    import bpy
    # Clear mesh objects if demo
    if demo:
        bpy.ops.object.select_all(action="SELECT")
        bpy.ops.object.delete(use_global=False)
        bpy.ops.mesh.primitive_cylinder_add(radius=0.35, depth=1.2, location=(0, 0, 0.6))
        body = bpy.context.active_object
        body.name = "UnitBody"
        # Remap-red material on upper half approx via emit
        mat = bpy.data.materials.new("RemapRed")
        mat.use_nodes = True
        nodes = mat.node_tree.nodes
        nodes.clear()
        out = nodes.new("ShaderNodeOutputMaterial")
        bsdf = nodes.new("ShaderNodeBsdfPrincipled")
        bsdf.inputs["Base Color"].default_value = (1.0, 0.0, 0.0, 1.0)
        mat.node_tree.links.new(bsdf.outputs["BSDF"], out.inputs["Surface"])
        body.data.materials.append(mat)
        bpy.ops.mesh.primitive_uv_sphere_add(radius=0.28, location=(0, 0, 1.35))
        head = bpy.context.active_object
        head.name = "UnitHead"
        mat2 = bpy.data.materials.new("Olive")
        mat2.use_nodes = True
        n2 = mat2.node_tree.nodes
        n2.clear()
        o2 = n2.new("ShaderNodeOutputMaterial")
        b2 = n2.new("ShaderNodeBsdfPrincipled")
        b2.inputs["Base Color"].default_value = (0.25, 0.40, 0.22, 1.0)
        mat2.node_tree.links.new(b2.outputs["BSDF"], o2.inputs["Surface"])
        head.data.materials.append(mat2)

    # Camera — iso
    cam_data = bpy.data.cameras.new("RA2Cam")
    cam_data.type = "ORTHO"
    cam_data.ortho_scale = 3.2
    cam = bpy.data.objects.new("RA2Cam", cam_data)
    bpy.context.scene.collection.objects.link(cam)
    bpy.context.scene.camera = cam
    # Classic RTS iso: rotate 45° around Z, elevate ~35.264° (arctan(1/sqrt(2)))
    elev = math.radians(35.264)
    az = math.radians(45.0)
    dist = 8.0
    cam.location = (
        dist * math.cos(elev) * math.cos(az),
        -dist * math.cos(elev) * math.sin(az),
        dist * math.sin(elev),
    )
    # Point at origin
    direction = -cam.location
    cam.rotation_euler = direction.to_track_quat("-Z", "Y").to_euler()

    # Light
    light_data = bpy.data.lights.new("RA2Key", type="SUN")
    light_data.energy = 3.0
    light = bpy.data.objects.new("RA2Key", light_data)
    bpy.context.scene.collection.objects.link(light)
    light.rotation_euler = (math.radians(45), math.radians(15), math.radians(-30))

    scene = bpy.context.scene
    scene.render.engine = "BLENDER_EEVEE"
    scene.render.film_transparent = True
    scene.render.image_settings.file_format = "PNG"
    scene.render.image_settings.color_mode = "RGBA"
    return cam


def render_all(args):
    import bpy
    from mathutils import Vector

    cam = setup_scene(args.demo)
    scene = bpy.context.scene
    cw, ch = args.canvas
    ss = args.supersample
    scene.render.resolution_x = cw * ss
    scene.render.resolution_y = ch * ss
    scene.render.resolution_percentage = 100

    # Find root to rotate (all meshes)
    roots = [o for o in bpy.data.objects if o.type == "MESH"]
    if not roots:
        raise RuntimeError("No mesh objects to render; pass --demo or open a .blend with meshes")

    out_root = os.path.join(args.out, args.anim)
    os.makedirs(out_root, exist_ok=True)

    # Facing 0 = East: model faces +X initially; rotate around Z by facing*45° CCW
    for facing in range(args.facings):
        angle = math.radians(facing * 45.0)
        for obj in roots:
            obj.rotation_euler[2] = angle
        for frame_i in range(args.frames):
            scene.frame_set(frame_i + 1)
            path = os.path.join(out_root, f"{facing}_{frame_i:02d}.png")
            scene.render.filepath = path
            bpy.ops.render.render(write_still=True)
            # Downsample with nearest if supersampled
            if ss > 1:
                try:
                    from PIL import Image
                    im = Image.open(path).convert("RGBA")
                    im = im.resize((cw, ch), Image.NEAREST)
                    im.save(path)
                except Exception:
                    pass  # leave hi-res if Pillow missing inside Blender
            print("wrote", path)


def main():
    args = parse_args(sys.argv)
    render_all(args)


if __name__ == "__main__":
    main()
