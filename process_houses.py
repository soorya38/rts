from PIL import Image
import glob
import os

images = [
    "/Users/sooryaakilesh/.gemini/antigravity/brain/c4988ffd-d320-44a6-b3a0-e201a075baf6/tamil_house_1_1776931151279.png",
    "/Users/sooryaakilesh/.gemini/antigravity/brain/c4988ffd-d320-44a6-b3a0-e201a075baf6/tamil_house_2_1776931168667.png",
    "/Users/sooryaakilesh/.gemini/antigravity/brain/c4988ffd-d320-44a6-b3a0-e201a075baf6/tamil_house_3_1776931187106.png",
    "/Users/sooryaakilesh/.gemini/antigravity/brain/c4988ffd-d320-44a6-b3a0-e201a075baf6/tamil_house_4_1776931204035.png"
]

def make_transparent(img_path, out_path):
    try:
        img = Image.open(img_path).convert("RGBA")
        datas = img.getdata()
        newData = []
        for item in datas:
            # check if pixel is white or very close to white
            if item[0] > 240 and item[1] > 240 and item[2] > 240:
                newData.append((255, 255, 255, 0))
            else:
                newData.append(item)
        img.putdata(newData)
        # Resize to a reasonable game asset size, e.g. 512x512 or 256x256
        # The generated images are usually 1024x1024
        img = img.resize((256, 256), Image.Resampling.LANCZOS)
        img.save(out_path, "PNG")
        print(f"Saved {out_path}")
    except Exception as e:
        print(f"Failed to process {img_path}: {e}")

out_dir = "/Users/sooryaakilesh/test/rts/assets/buildings"
if not os.path.exists(out_dir):
    os.makedirs(out_dir)

for i, path in enumerate(images):
    out_path = os.path.join(out_dir, f"tamil_house_{i}.png")
    make_transparent(path, out_path)

