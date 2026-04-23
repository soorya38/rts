from PIL import Image
import os

files = {
    "tamil_market": "/Users/sooryaakilesh/.gemini/antigravity/brain/c4988ffd-d320-44a6-b3a0-e201a075baf6/tamil_market_1776931547414.png",
    "tamil_monastery": "/Users/sooryaakilesh/.gemini/antigravity/brain/c4988ffd-d320-44a6-b3a0-e201a075baf6/tamil_monastery_1776931564786.png",
    "tamil_watch_tower": "/Users/sooryaakilesh/.gemini/antigravity/brain/c4988ffd-d320-44a6-b3a0-e201a075baf6/tamil_watch_tower_1776931583529.png",
    "tamil_blacksmith": "/Users/sooryaakilesh/.gemini/antigravity/brain/c4988ffd-d320-44a6-b3a0-e201a075baf6/tamil_blacksmith_1776931628741.png"
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
        # Use 512x512 because these buildings are larger than houses (e.g. 3x3 or 4x4 tiles)
        img = img.resize((512, 512), Image.Resampling.LANCZOS)
        img.save(out_path, "PNG")
        print(f"Saved {out_path}")
    except Exception as e:
        print(f"Failed to process {img_path}: {e}")

out_dir = "/Users/sooryaakilesh/test/rts/assets/buildings"
for name, path in files.items():
    out_path = os.path.join(out_dir, f"{name}.png")
    make_transparent(path, out_path)

