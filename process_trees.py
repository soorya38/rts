from PIL import Image
import os

files = {
    "tamil_tree_0": "/Users/sooryaakilesh/.gemini/antigravity/brain/c4988ffd-d320-44a6-b3a0-e201a075baf6/tamil_tree_coconut_1776932263395.png",
    "tamil_tree_1": "/Users/sooryaakilesh/.gemini/antigravity/brain/c4988ffd-d320-44a6-b3a0-e201a075baf6/tamil_tree_banyan_1776932284056.png",
    "tamil_tree_2": "/Users/sooryaakilesh/.gemini/antigravity/brain/c4988ffd-d320-44a6-b3a0-e201a075baf6/tamil_tree_banana_1776932299550.png",
    "tamil_tree_3": "/Users/sooryaakilesh/.gemini/antigravity/brain/c4988ffd-d320-44a6-b3a0-e201a075baf6/tamil_tree_mango_1776932340774.png"
}

def make_transparent(img_path, out_path):
    try:
        img = Image.open(img_path).convert("RGBA")
        datas = img.getdata()
        newData = []
        for item in datas:
            if item[0] > 240 and item[1] > 240 and item[2] > 240:
                newData.append((255, 255, 255, 0))
            else:
                newData.append(item)
        img.putdata(newData)
        # Resize to 256x256
        img = img.resize((256, 256), Image.Resampling.LANCZOS)
        img.save(out_path, "PNG")
        print(f"Saved {out_path}")
    except Exception as e:
        print(f"Failed to process {img_path}: {e}")

out_dir = "/Users/sooryaakilesh/test/rts/assets/buildings"
for name, path in files.items():
    out_path = os.path.join(out_dir, f"{name}.png")
    make_transparent(path, out_path)

