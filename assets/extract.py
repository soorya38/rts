import sys
try:
    from PIL import Image
except ImportError:
    import subprocess
    subprocess.check_call([sys.executable, "-m", "pip", "install", "Pillow"])
    from PIL import Image

import os

def extract_sprites(image_path, output_dir):
    if not os.path.exists(output_dir):
        os.makedirs(output_dir)
        
    img = Image.open(image_path).convert("RGBA")
    data = img.load()
    width, height = img.size
    
    # Simple connected component finding to group non-white pixels
    visited = set()
    components = []
    
    def is_bg(x, y):
        r, g, b, a = data[x, y]
        # White background or transparent
        return a == 0 or (r > 240 and g > 240 and b > 240)
        
    for y in range(height):
        for x in range(width):
            if not is_bg(x, y) and (x, y) not in visited:
                # BFS to find full component
                comp = []
                q = [(x, y)]
                visited.add((x, y))
                min_x, max_x = x, x
                min_y, max_y = y, y
                
                head = 0
                while head < len(q):
                    cx, cy = q[head]
                    head += 1
                    comp.append((cx, cy))
                    min_x = min(min_x, cx)
                    max_x = max(max_x, cx)
                    min_y = min(min_y, cy)
                    max_y = max(max_y, cy)
                    
                    for dx, dy in [(-1,0), (1,0), (0,-1), (0,1), (-1,-1), (1,1), (-1,1), (1,-1)]:
                        nx, ny = cx + dx, cy + dy
                        if 0 <= nx < width and 0 <= ny < height:
                            if not is_bg(nx, ny) and (nx, ny) not in visited:
                                visited.add((nx, ny))
                                q.append((nx, ny))
                                
                # Add horizontal/vertical tolerance to merge close components
                components.append({'bbox': [min_x, min_y, max_x, max_y]})
                
    # Merge overlapping or close bounding boxes
    def merge(c1, c2, threshold=15):
        b1 = c1['bbox']
        b2 = c2['bbox']
        # check distance between horizontal and vertical axes
        dx = max(0, max(b1[0], b2[0]) - min(b1[2], b2[2]))
        dy = max(0, max(b1[1], b2[1]) - min(b1[3], b2[3]))
        if dx < threshold and dy < threshold:
            return {'bbox': [min(b1[0], b2[0]), min(b1[1], b2[1]), max(b1[2], b2[2]), max(b1[3], b2[3])]}
        return None

    merged = True
    while merged:
        merged = False
        new_components = []
        skip = set()
        for i in range(len(components)):
            if i in skip: continue
            current = components[i]
            for j in range(i+1, len(components)):
                if j in skip: continue
                m = merge(current, components[j])
                if m:
                    current = m
                    skip.add(j)
                    merged = True
            new_components.append(current)
        components = new_components

    # Filter out small noise
    components = [c for c in components if (c['bbox'][2]-c['bbox'][0]) > 20 and (c['bbox'][3]-c['bbox'][1]) > 20]
    
    # Sort topologically: by Y then X
    # Group by rough Y
    rows = []
    current_row = []
    components.sort(key=lambda c: c['bbox'][1])
    
    if not components:
        print("No components found!")
        return
        
    last_y = components[0]['bbox'][1]
    for c in components:
        if c['bbox'][1] - last_y > 40: # new row
            current_row.sort(key=lambda x: x['bbox'][0])
            rows.append(current_row)
            current_row = [c]
        else:
            current_row.append(c)
        last_y = c['bbox'][1]
    if current_row:
        current_row.sort(key=lambda x: x['bbox'][0])
        rows.append(current_row)
        
    idx = 0
    for r_idx, row in enumerate(rows):
        for c_idx, c in enumerate(row):
            b = c['bbox']
            # pad slightly
            pad = 5
            b[0] = max(0, b[0]-pad)
            b[1] = max(0, b[1]-pad)
            b[2] = min(width-1, b[2]+pad)
            b[3] = min(height-1, b[3]+pad)
            
            cropped = img.crop((b[0], b[1], b[2]+1, b[3]+1))
            
            # Make white transparent
            cdata = cropped.load()
            cw, ch = cropped.size
            for cy in range(ch):
                for cx in range(cw):
                    cr, cg, cb, ca = cdata[cx, cy]
                    if ca > 0 and cr > 240 and cg > 240 and cb > 240:
                        cdata[cx, cy] = (255, 255, 255, 0)
            
            cropped.save(f"{output_dir}/sprite_{r_idx}_{c_idx}.png")
            idx += 1
    print(f"Extracted {idx} sprites to {output_dir}")

extract_sprites("assets/spritesheet.png", "assets/extracted")
