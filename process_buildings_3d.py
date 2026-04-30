from PIL import Image
import glob
import os

def voxelize_sprite(img_path, obj_path, max_size=256, voxel_depth=8):
    try:
        img = Image.open(img_path).convert("RGBA")
        
        # Scale down to max_size while keeping aspect ratio
        w, h = img.size
        if w > max_size or h > max_size:
            if w > h:
                new_w = max_size
                new_h = int(h * (max_size / w))
            else:
                new_h = max_size
                new_w = int(w * (max_size / h))
            img = img.resize((new_w, new_h), Image.Resampling.NEAREST)
        
        w, h = img.size
        pixels = img.load()
        # We generate a highly optimized single-quad billboard model.
        # Since the game now uses dynamic billboarding (always facing the camera), 
        # generating 250,000 voxel vertices is completely unnecessary and causes
        # a 16-bit index overflow (Segfault 11) in Raylib.
        # A single quad matches the exact dimensions and UVs, ensuring perfect clarity.
        with open(obj_path, 'w') as f:
            # MTL reference
            mtl_name = os.path.basename(img_path).replace('.png', '.mtl')
            f.write(f"mtllib {mtl_name}\n")
            
            # Vertices (x, y, z)
            # Bottom-left
            f.write("v -0.5 0.0 0.0\n")
            # Bottom-right
            f.write("v 0.5 0.0 0.0\n")
            # Top-right
            f.write("v 0.5 1.0 0.0\n")
            # Top-left
            f.write("v -0.5 1.0 0.0\n")
            
            # UV Coordinates (u, v)
            # Standard OBJ uses V=0 at bottom, V=1 at top.
            f.write("vt 0.0 0.0\n")
            f.write("vt 1.0 0.0\n")
            f.write("vt 1.0 1.0\n")
            f.write("vt 0.0 1.0\n")
            
            # Normals
            f.write("vn 0.0 0.0 1.0\n")
            
            # Faces
            f.write("usemtl mat0\n")
            f.write("s off\n")
            f.write("f 1/1/1 2/2/1 3/3/1\n")
            f.write("f 1/1/1 3/3/1 4/4/1\n")
            
        print(f"Generated Optimized Billboard: {obj_path}")

        # Generate MTL
        mtl_path = obj_path.replace('.obj', '.mtl')
        with open(mtl_path, 'w') as f:
            f.write("newmtl mat0\n")
            f.write("Ka 1.000 1.000 1.000\n")
            f.write("Kd 1.000 1.000 1.000\n")
            f.write(f"map_Kd {os.path.basename(img_path)}\n")
            
    except Exception as e:
        print(f"Failed to process {img_path}: {e}")

out_dir = "/Users/sooryaakilesh/test/rts/assets/buildings"
files = glob.glob(os.path.join(out_dir, "tamil_*.png"))

for path in files:
    if "sprite_sheet" in path or "variants" in path:
        continue
    obj_path = path.replace(".png", ".obj")
    voxelize_sprite(path, obj_path)
