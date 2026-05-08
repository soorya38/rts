from PIL import Image, ImageDraw, ImageEnhance, ImageFilter
import random
import os


OUT = 512
SCALE = 4
SIZE = OUT
random.seed(19)


SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))


def sc(v):
    return int(round(v * SCALE))


def pts(points):
    return [(sc(x), sc(y)) for x, y in points]


def rgba(color, alpha=255):
    return (color[0], color[1], color[2], alpha)


def load_asset(name):
    path = os.path.join(SCRIPT_DIR, "assets", "buildings", f"{name}.png")
    return Image.open(path).convert("RGBA")


ASSETS = {
    "castle": load_asset("tamil_castle"),
    "tower": load_asset("tamil_watch_tower"),
    "house": load_asset("tamil_house_0"),
    "market": load_asset("tamil_market"),
    "tc": load_asset("tamil_town_center"),
}


def crop_asset(name, box):
    crop = ASSETS[name].crop(box)
    bbox = crop.getbbox()
    if bbox:
        crop = crop.crop(bbox)
    return crop


def flatten_texture(src):
    pixels = [p for p in src.getdata() if p[3] > 80]
    if pixels:
        avg = tuple(sum(p[i] for p in pixels) // len(pixels) for i in range(3))
    else:
        avg = (110, 90, 70)
    bg = Image.new("RGBA", src.size, rgba(avg, 255))
    bg.alpha_composite(src)
    return bg


TEXTURES = {
    "brick": crop_asset("castle", (45, 260, 455, 370)),
    "redstone": crop_asset("castle", (25, 215, 475, 405)),
    "granite": crop_asset("tower", (120, 190, 390, 430)),
    "granite_top": crop_asset("tower", (120, 165, 390, 265)),
    "wood": crop_asset("house", (45, 70, 210, 220)),
    "thatch": crop_asset("market", (110, 45, 390, 180)),
    "terracotta": crop_asset("house", (55, 25, 225, 105)),
    "darkwood": crop_asset("tc", (230, 590, 720, 840)),
}


def texture_fill(size, source, tint=(255, 255, 255), brightness=1.0, contrast=1.0):
    src = flatten_texture(source)
    if src.width < size[0] or src.height < size[1]:
        scale = max(size[0] / src.width, size[1] / src.height) * 1.15
        src = src.resize((max(1, int(src.width * scale)), max(1, int(src.height * scale))), Image.Resampling.BICUBIC)
    ox = random.randint(0, max(0, src.width - size[0]))
    oy = random.randint(0, max(0, src.height - size[1]))
    tex = src.crop((ox, oy, ox + size[0], oy + size[1])).convert("RGBA")
    tex = tex.filter(ImageFilter.GaussianBlur(1.15))
    tex = ImageEnhance.Brightness(tex).enhance(brightness)
    tex = ImageEnhance.Contrast(tex).enhance(contrast * 0.82)

    overlay = Image.new("RGBA", size, rgba(tint, 45))
    tex.alpha_composite(overlay)
    return tex


def masked_texture(img, polygon, texture, tint=(255, 255, 255), brightness=1.0, contrast=1.0, alpha=255):
    mask = Image.new("L", (SIZE, SIZE), 0)
    d = ImageDraw.Draw(mask)
    d.polygon(pts(polygon), fill=alpha)
    mask = mask.filter(ImageFilter.GaussianBlur(0.45))
    bbox = mask.getbbox()
    if not bbox:
        return

    tex_pixels = texture.load()
    tw = texture.width
    th = texture.height

    base = tuple(max(0, min(255, int(c * brightness))) for c in tint)
    layer = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
    lp = layer.load()
    mp = mask.load()
    height = max(1, bbox[3] - bbox[1])
    for y in range(bbox[1], bbox[3]):
        # Soft top-to-bottom shading
        shade = 1.12 - 0.24 * ((y - bbox[1]) / height)
        for x in range(bbox[0], bbox[2]):
            a = mp[x, y]
            if not a:
                continue
            
            # Tiled texture coordinate lookup
            tx = x % tw
            ty = y % th
            tex_px = tex_pixels[tx, ty]
            
            # Use RGB channel multiplier with base color and shade
            r_val = (tex_px[0] / 255.0) * base[0] * shade
            g_val = (tex_px[1] / 255.0) * base[1] * shade
            b_val = (tex_px[2] / 255.0) * base[2] * shade
            
            grain = random.randint(-12, 12)
            fine = random.randint(-4, 4)
            lp[x, y] = (
                max(0, min(255, int(r_val) + grain + fine)),
                max(0, min(255, int(g_val) + grain + fine)),
                max(0, min(255, int(b_val) + grain + fine)),
                a,
            )

    layer = layer.filter(ImageFilter.GaussianBlur(0.35))
    img.alpha_composite(layer)


def flat_poly(img, polygon, color, alpha=255):
    layer = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    d.polygon(pts(polygon), fill=rgba(color, alpha))
    layer = layer.filter(ImageFilter.GaussianBlur(0.25))
    img.alpha_composite(layer)


def line(draw, points, color, width=1, alpha=200):
    draw.line(pts(points), fill=rgba(color, 255), width=sc(width), joint="curve")


def shadow(img, cx=64, cy=96, w=96, h=32):
    layer = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    d.polygon(pts([(cx, cy - h / 2), (cx + w / 2, cy), (cx, cy + h / 2), (cx - w / 2, cy)]), fill=(20, 14, 8, 85))
    layer = layer.filter(ImageFilter.GaussianBlur(sc(2.0)))
    img.alpha_composite(layer)


def wall_body(img, material, top_tint, left_tint, right_tint, height=105, top_y=56):
    top = [(22, top_y), (64, top_y - 19), (106, top_y), (64, top_y + 21)]
    left = [(22, top_y), (64, top_y + 21), (64, height), (22, height - 22)]
    right = [(106, top_y), (64, top_y + 21), (64, height), (106, height - 22)]
    masked_texture(img, top, material, top_tint, 1.12, 1.05)
    masked_texture(img, left, material, left_tint, 0.88, 1.08)
    masked_texture(img, right, material, right_tint, 0.72, 1.1)
    d = ImageDraw.Draw(img)
    line(d, [(22, top_y), (64, top_y - 19), (106, top_y), (64, top_y + 21), (22, top_y)], (120, 94, 70), 1, 255)
    line(d, [(22, height - 22), (64, height), (106, height - 22)], (37, 31, 27), 2, 255)


def add_block_courses(img, y0=61, y1=97, color=(40, 34, 31), alpha=80):
    d = ImageDraw.Draw(img)
    for y in range(y0, y1, 9):
        line(d, [(27, y), (64, y + 18), (101, y)], color, 1, alpha)
    for x in [38, 52, 66, 80, 94]:
        line(d, [(x, y0 - 5), (x - 14, y0 + 2)], color, 1, alpha - 10)
        line(d, [(x + 8, 77), (x - 8, 86)], color, 1, alpha - 10)


def add_coping_blocks(img, texture, y=50, tint=(255, 255, 255), count=7):
    for i in range(count):
        x = 26 + i * (78 / max(1, count - 1))
        block = [(x - 5, y + 7), (x + 5, y + 7), (x + 5, y - 5), (x - 5, y - 5)]
        cap = [(x - 5, y - 5), (x, y - 10), (x + 5, y - 5)]
        masked_texture(img, block, texture, tint, 1.02, 1.05)
        masked_texture(img, cap, texture, tint, 1.12, 1.04)
        
        # Exquisite micro-detailing: deep borders and highlight edges
        d = ImageDraw.Draw(img)
        line(d, [(x - 5, y + 7), (x - 5, y - 5)], (35, 32, 30), 1, 140) # Deep left shadow border
        line(d, [(x - 5, y - 5), (x, y - 10), (x + 5, y - 5)], (245, 235, 215), 1, 160) # Top cap bright highlight


def add_roof_coping(img, texture, y=50):
    # Low terracotta coping like the roof vocabulary in the house/castle sprites.
    masked_texture(img, [(25, y + 7), (64, y - 11), (103, y + 7), (98, y + 13), (64, y - 2), (30, y + 13)],
                   texture, (246, 216, 183), 1.0, 1.08)
    d = ImageDraw.Draw(img)
    for x in [34, 48, 62, 76, 90]:
        line(d, [(x - 6, y + 4), (x, y - 4), (x + 6, y + 4)], (88, 42, 29), 1, 95)
        # Bright highlights on the crest of individual terracotta tiles
        line(d, [(x - 3, y + 1), (x, y - 4), (x + 3, y + 1)], (255, 195, 165), 1, 130)


def add_plaster_stripes(img, alpha=90):
    # Real Tamil temple compound walls often carry red/white limewash. Keep it weathered and secondary.
    for i, x in enumerate([29, 40, 51, 62, 73, 84, 95]):
        color = (196, 184, 157) if i % 2 == 0 else (116, 49, 41)
        flat_poly(img, [(x - 4, 63), (x + 4, 67), (x + 4, 94), (x - 4, 90)], color, alpha)


def add_weathering(img):
    d = ImageDraw.Draw(img)
    for _ in range(34):
        x = random.randint(sc(22), sc(106))
        y = random.randint(sc(55), sc(104))
        length = random.randint(sc(3), sc(13))
        col = random.choice([(28, 22, 17), (238, 211, 165), (92, 68, 45)])
        d.line([(x, y), (x + random.randint(-4, 4), y + length)], fill=rgba(col, random.randint(18, 50)), width=random.randint(1, 3))


def draw_dark_age():
    img = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
    shadow(img, 64, 96, 94, 30)
    wall_body(img, TEXTURES["darkwood"], (167, 129, 82), (89, 65, 43), (56, 45, 34), height=103, top_y=65)
    d = ImageDraw.Draw(img)
    for x in [28, 39, 50, 61, 72, 83, 94, 105]:
        masked_texture(img, [(x - 3, 86), (x + 4, 88), (x + 4, 43), (x - 2, 40)], TEXTURES["wood"], (169, 128, 81), 0.92, 1.1)
        masked_texture(img, [(x - 3, 40), (x + 1, 31), (x + 6, 42)], TEXTURES["wood"], (190, 143, 83), 1.0, 1.05)
    for y in [58, 70, 80]:
        line(d, [(27, y), (101, y + 22)], (90, 58, 33), 3, 165)
    # A few palmyra leaves, muted like environmental detail rather than an icon.
    for x in [39, 64, 90]:
        flat_poly(img, [(x - 8, 34), (x, 29), (x + 3, 37), (x - 4, 39)], (44, 96, 50), 210)
        flat_poly(img, [(x, 32), (x + 10, 36), (x + 1, 41)], (35, 76, 42), 200)
    add_weathering(img)
    return img


def draw_feudal():
    img = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
    shadow(img)
    wall_body(img, TEXTURES["brick"], (180, 114, 76), (118, 64, 45), (82, 52, 43), height=106, top_y=60)
    add_plaster_stripes(img, 48)
    add_roof_coping(img, TEXTURES["terracotta"], 52)
    add_block_courses(img, 65, 100, (68, 38, 30), 65)
    add_weathering(img)
    return img


def draw_castle():
    img = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
    shadow(img, 64, 96, 96, 32)
    wall_body(img, TEXTURES["granite"], (142, 135, 119), (82, 80, 74), (59, 62, 61), height=108, top_y=58)
    add_coping_blocks(img, TEXTURES["granite_top"], 50, (184, 174, 145), 7)
    add_block_courses(img, 63, 101, (34, 35, 34), 95)
    d = ImageDraw.Draw(img)
    # Shallow carved lotus medallions with 3D carving relief shadows
    for x in [50, 78]:
        d.ellipse((sc(x - 4), sc(73), sc(x + 4), sc(79)), fill=(45, 42, 38, 55)) # Inner shadow
        d.ellipse((sc(x - 5), sc(72), sc(x + 5), sc(80)), outline=(215, 202, 180, 130), width=sc(1)) # Outer highlight
        line(d, [(x, 71), (x, 81)], (195, 182, 154), 1, 110)
        line(d, [(x - 5, 76), (x + 5, 76)], (195, 182, 154), 1, 110)
        line(d, [(x - 3, 73), (x + 3, 79)], (195, 182, 154), 0.8, 90)
        line(d, [(x - 3, 79), (x + 3, 73)], (195, 182, 154), 0.8, 90)
    add_weathering(img)
    return img


def draw_imperial():
    img = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
    shadow(img, 64, 96, 100, 34)
    wall_body(img, TEXTURES["redstone"], (176, 120, 79), (114, 67, 49), (80, 55, 47), height=109, top_y=59)
    add_plaster_stripes(img, 40)
    add_coping_blocks(img, TEXTURES["granite_top"], 50, (198, 180, 138), 6)
    # Small realistic shrine/gopuram fragment in the middle, pulled from the castle palette, not a flat symbol.
    masked_texture(img, [(53, 55), (56, 39), (60, 39), (62, 30), (64, 25), (66, 30), (69, 39), (73, 39), (76, 55)],
                   TEXTURES["redstone"], (213, 151, 93), 1.0, 1.05)
    masked_texture(img, [(58, 55), (60, 43), (64, 34), (68, 43), (70, 55)],
                   TEXTURES["terracotta"], (233, 186, 119), 1.0, 1.05)
    
    # Kalasam (golden spire finial) on top of the shrine at (64, 21)
    flat_poly(img, [(63, 25), (65, 25), (65, 21), (63, 21)], (242, 191, 66), 230) # Base stem
    flat_poly(img, [(62, 21), (66, 21), (65, 17), (63, 17)], (250, 215, 90), 255) # Golden pot/dome
    flat_poly(img, [(64, 17), (64, 14)], (255, 235, 130), 255) # Spire tip

    add_block_courses(img, 64, 102, (66, 38, 31), 70)
    add_weathering(img)
    return img


DRAWERS = [draw_dark_age, draw_feudal, draw_castle, draw_imperial]


for age, drawer in enumerate(DRAWERS):
    sprite = drawer()
    sprite = sprite.filter(ImageFilter.GaussianBlur(0.25))
    sprite = sprite.filter(ImageFilter.UnsharpMask(radius=0.75, percent=55, threshold=5))
    save_path = os.path.join(SCRIPT_DIR, "assets", "buildings", f"wall_age_{age}.png")
    sprite.save(save_path)
