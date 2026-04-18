import numpy as np
from PIL import Image
import os

img = Image.open('assets/spritesheet2.png').convert("RGBA")
arr = np.array(img)
H, W = arr.shape[:2]

bg = np.array([4, 3, 8])
dist = np.sum(np.abs(arr[:, :, :3] - bg), axis=2)
content = dist > 40

col1 = content[:, 170:340]
proj = np.sum(col1, axis=1)
smoothed = np.convolve(proj, np.ones(15)/15, mode='same')

minima = [80] # Start after top headers
for i in range(80, H-15):
    window = smoothed[max(0, i-15):min(H, i+15)]
    if smoothed[i] == np.min(window):
        if not minima or i - minima[-1] > 30:
            minima.append(i)

# Add bottom boundary
if H - minima[-1] > 30:
    minima.append(H - 20)

print(f"Found {len(minima)-1} regions.")

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

# We need exactly 26 regions.
if len(minima)-1 < len(ORDER):
    print("Not enough regions found!")
    
for i, path in enumerate(ORDER):
    if i >= len(minima)-1: break
    
    # Crop
    # Column 1 = x: 170 to 340
    y1, y2 = minima[i], minima[i+1]
    
    box = (170, y1, 340, y2)
    sprite = img.crop(box)
    
    # clear bg
    data = sprite.load()
    w_crop, h_crop = sprite.size
    for py in range(h_crop):
        for px in range(w_crop):
            r, g, b, a = data[px, py]
            distance = abs(r-bg[0])+abs(g-bg[1])+abs(b-bg[2])
            if distance < 40:
                data[px, py] = (0,0,0,0)
                
    os.makedirs(os.path.dirname(path), exist_ok=True)
    sprite.save(path)
    print(f"[{i}] {path}: {y1} to {y2} (h={y2-y1})")

