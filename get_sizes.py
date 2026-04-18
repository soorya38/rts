import numpy as np
from PIL import Image

def find_bands(mask_1d, min_size=10, gap_tol=6):
    regions = []
    in_region = False
    start = 0
    gap = 0
    n = len(mask_1d)
    for i in range(n):
        if mask_1d[i]:
            if not in_region:
                in_region = True
                start = i
            gap = 0
        else:
            if in_region:
                gap += 1
                if gap > gap_tol:
                    size = i - gap - start
                    if size >= min_size:
                        regions.append((start, i - gap))
                    in_region = False
                    gap = 0
    if in_region:
        size = n - start
        if size >= min_size:
            regions.append((start, n))
    return regions

img = Image.open('assets/spritesheet2.png').convert("RGBA")
arr = np.array(img)

# Try aggressive thresholding:
# Find rows based on just column 1 (x=170..340) and very bright pixels!
content = np.max(arr[:, :, :3], axis=2) > 160
row_mask = (np.sum(content[:, 170:340], axis=1) > 5).tolist()

rows = find_bands(row_mask, min_size=5, gap_tol=8)
print(f"Aggressive threshold found {len(rows)} rows.")
