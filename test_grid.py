from PIL import Image
import numpy as np

img = Image.open('assets/spritesheet2.png').convert("RGBA")
# For row 0 (Town Center) in Dark Age (col 1)
# Column 1 offset: 170. Width 170.
# Row 0 offset: 80. Height 56.
tc_crop = img.crop((170, 80, 170+170, 80+56))
tc_crop.save('test_tc.png')

# Let's also check if 80 is the right start
print("Saved test_tc.png. Check it.")
