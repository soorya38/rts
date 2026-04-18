import numpy as np
from PIL import Image

img = Image.open('test_tc.png').convert("RGBA")
H, W = img.size[1], img.size[0]

arr = np.array(img)[:,:,:3]
bg = np.array([4, 3, 8])
dist = np.sum(np.abs(arr - bg), axis=2)

chars = " .:-=+*#%@"
for y in range(0, H, 2):  # stride 2 to somewhat match char aspect ratio
    line = ""
    for x in range(W):
        d = dist[y, x]
        if d < 10:
            line += " "
        else:
            idx = int(d / 765 * len(chars))
            if idx >= len(chars): idx = len(chars)-1
            line += chars[idx]
    print(line)
