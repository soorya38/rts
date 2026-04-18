import numpy as np
from PIL import Image

img = Image.open('assets/spritesheet2.png').convert("RGBA")
arr = np.array(img)
H, W = arr.shape[:2]

bg = np.array([4, 3, 8])
dist = np.sum(np.abs(arr[:, :, :3] - bg), axis=2)
content = dist > 120  # VERY strict
# restrict to Dark Age column 1! (X=170..340)
content[:, :150] = False
content[:, 350:] = False

# BFS
visited = np.zeros((H, W), dtype=bool)
visited[~content] = True

components = []

for y in range(H):
    for x in range(W):
        if not visited[y, x]:
            comp_q = [(x, y)]
            visited[y, x] = True
            min_x, max_x = x, x
            min_y, max_y = y, y
            
            head = 0
            while head < len(comp_q):
                cx, cy = comp_q[head]
                head += 1
                
                for dx, dy in [(-1,0), (1,0), (0,-1), (0,1), (-1,-1), (1,1), (-1,1), (1,-1)]:
                    nx, ny = cx + dx, cy + dy
                    if 0 <= nx < W and 0 <= ny < H:
                        if not visited[ny, nx]:
                            visited[ny, nx] = True
                            comp_q.append((nx, ny))
                            min_x = min(min_x, nx)
                            max_x = max(max_x, nx)
                            min_y = min(min_y, ny)
                            max_y = max(max_y, ny)
                            
            if max_x - min_x > 15 and max_y - min_y > 15:
                components.append((min_x, min_y, max_x, max_y))

print(f"Found {len(components)} components.")
# Sort visually
components.sort(key=lambda c: c[1])
for c in components[:26]:
    print(c)
