import os
from PIL import Image

def slice_spritesheet(image_path, output_dir):
    if not os.path.exists(output_dir):
        os.makedirs(output_dir)
        
    img = Image.open(image_path).convert("RGBA")
    width, height = img.size
    
    # define rows based on visual layout of the image
    # The image has 4 building rows, 1 unit row, 1 env row, 1 icon row
    # Let's dynamically find empty rows (all white)
    pixels = img.load()
    
    row_empty = []
    for y in range(height):
        is_empty = True
        for x in range(width):
            r, g, b, a = pixels[x, y]
            if a > 10 and not (r > 240 and g > 240 and b > 240):
                is_empty = False
                break
        row_empty.append(is_empty)
        
    col_empty = []
    for x in range(width):
        is_empty = True
        for y in range(height):
            r, g, b, a = pixels[x, y]
            if a > 10 and not (r > 240 and g > 240 and b > 240):
                is_empty = False
                break
        col_empty.append(is_empty)
        
    # Find row regions
    row_regions = []
    in_region = False
    start_y = 0
    for y in range(height):
        if not row_empty[y] and not in_region:
            in_region = True
            start_y = y
        elif row_empty[y] and in_region:
            in_region = False
            if y - start_y > 10:
                row_regions.append((start_y, y))
    if in_region:
        row_regions.append((start_y, height))
        
    print(f"Found {len(row_regions)} rows.")
    
    count = 0
    for i, (sy, ey) in enumerate(row_regions):
        # find col regions within this row
        col_regs = []
        in_col = False
        start_x = 0
        for x in range(width):
            is_empty = True
            for y in range(sy, ey):
                r, g, b, a = pixels[x, y]
                if a > 10 and not (r > 240 and g > 240 and b > 240):
                    is_empty = False
                    break
            if not is_empty and not in_col:
                in_col = True
                start_x = x
            elif is_empty and in_col:
                in_col = False
                if x - start_x > 10:
                    col_regs.append((start_x, x))
        if in_col:
            col_regs.append((start_x, width))
            
        print(f"Row {i} has {len(col_regs)} columns.")
        
        for j, (sx, ex) in enumerate(col_regs):
            # crop and remove white bg
            cropped = img.crop((sx, sy, ex, ey))
            c_pixels = cropped.load()
            c_w, c_h = cropped.size
            for cy in range(c_h):
                for cx in range(c_w):
                    r, g, b, a = c_pixels[cx, cy]
                    if r > 240 and g > 240 and b > 240:
                        c_pixels[cx, cy] = (255, 255, 255, 0)
            
            cropped.save(f"{output_dir}/sprite_r{i}_c{j}.png")
            count += 1
            
    print(f"Extracted {count} sprites in total.")

slice_spritesheet("assets/spritesheet.png", "assets/extracted")
