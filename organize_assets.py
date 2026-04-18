import os
import shutil

src = "assets/extracted"

mappings = {
    "buildings/town_center.png": "sprite_r0_c0.png",
    "buildings/house.png": "sprite_r0_c1.png",
    "buildings/barracks.png": "sprite_r0_c2.png",
    "buildings/archery_range.png": "sprite_r0_c3.png",
    "buildings/stable.png": "sprite_r0_c4.png",
    "buildings/mill.png": "sprite_r0_c5.png",
    "buildings/lumber_camp.png": "sprite_r0_c6.png",
    "buildings/mining_camp.png": "sprite_r0_c7.png",
    "buildings/blacksmith.png": "sprite_r1_c0.png",
    "buildings/market.png": "sprite_r1_c1.png",
    "buildings/farm.png": "sprite_r1_c2.png",

    "units/villager_m.png": "sprite_r3_c0.png",
    "units/villager_f.png": "sprite_r3_c1.png",
    "units/scout.png": "sprite_r3_c2.png",
    "units/militia.png": "sprite_r3_c3.png",
    "units/man_at_arms.png": "sprite_r3_c4.png",
    "units/archer.png": "sprite_r3_c5.png",
    "units/knight.png": "sprite_r3_c6.png",

    "env/tree.png": "sprite_r4_c0.png",
    "env/gold_mine.png": "sprite_r4_c1.png",
    "env/stone_mine.png": "sprite_r4_c2.png",
    "env/berry_bush.png": "sprite_r4_c3.png",

    "ui/food.png": "sprite_r5_c0.png",
    "ui/wood.png": "sprite_r5_c1.png",
    "ui/gold.png": "sprite_r5_c2.png",
    "ui/stone.png": "sprite_r5_c3.png",
    "ui/population.png": "sprite_r5_c4.png"
}

for dest, source in mappings.items():
    src_path = os.path.join(src, source)
    dest_path = os.path.join("assets", dest)
    if os.path.exists(src_path):
        os.makedirs(os.path.dirname(dest_path), exist_ok=True)
        shutil.copy(src_path, dest_path)
        print(f"Copied {src_path} -> {dest_path}")
    else:
        print(f"Error: {src_path} not found")

