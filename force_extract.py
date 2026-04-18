import os
from PIL import Image

def slice_spritesheet(image_path):
    img = Image.open(image_path).convert("RGBA")
    
    # Grid parameters based on analysis:
    # 6 columns, width ~1024. W = 1024 / 6 = 170
    # 26 rows, height ~1536. H = (1536 - 80) / 26 = 56
    start_y = 80
    row_h = 56
    
    # We want Dark Age, which is column 1 (x starting at 170)
    start_x = 170
    col_w = 170
    
    ORDER = [
        "assets/buildings/town_center.png",
        "assets/buildings/house.png",
        "assets/buildings/barracks.png",
        "assets/buildings/archery_range.png",
        "assets/buildings/stable.png",
        "assets/buildings/mill.png",
        "assets/buildings/lumber_camp.png",
        "assets/buildings/mining_camp.png",
        "assets/buildings/blacksmith.png",
        "assets/buildings/market.png",
        "assets/buildings/farm.png",
        "assets/units/villager_m.png",
        "assets/units/scout.png",
        "assets/units/militia.png",
        "assets/units/man_at_arms.png",
        "assets/units/archer.png",
        "assets/env/tree.png",
        "assets/env/stone_mine.png",
        "assets/env/gold_mine.png",
        "assets/env/berry_bush.png",
        "assets/ui/food.png",
        "assets/ui/wood.png",
        "assets/ui/stone.png",
        "assets/ui/gold.png",
        "assets/ui/population.png",
        "assets/units/knight.png"
    ]
    
    for i, path in enumerate(ORDER):
        y = start_y + i * row_h
        box = (start_x, y, start_x + col_w, y + row_h)
        sprite = img.crop(box)
        
        # Transparent background conversion:
        # Dark background [4, 3, 8] roughly
        data = sprite.load()
        for py in range(row_h):
            for px in range(col_w):
                r, g, b, a = data[px, py]
                dist = abs(r-4) + abs(g-3) + abs(b-8)
                if dist < 40:
                    data[px, py] = (0, 0, 0, 0)
                    
        os.makedirs(os.path.dirname(path), exist_ok=True)
        sprite.save(path)
        print(f"Extracted {path}")

slice_spritesheet("assets/spritesheet2.png")
