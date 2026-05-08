from PIL import Image, ImageDraw


SIZE = 128


def iso_poly(cx, cy, w, h):
    return [
        (cx, cy - h / 2),
        (cx + w / 2, cy),
        (cx, cy + h / 2),
        (cx - w / 2, cy),
    ]


def shadow(draw, cx=64, cy=91, w=92, h=34):
    draw.polygon(iso_poly(cx, cy, w, h), fill=(18, 15, 12, 70))


def draw_panel(draw, top, left, right, front, shade, light):
    draw.polygon([(24, 61), (64, 42), (104, 61), (64, 81)], fill=top)
    draw.polygon([(24, 61), (64, 81), (64, 105), (24, 85)], fill=left)
    draw.polygon([(104, 61), (64, 81), (64, 105), (104, 85)], fill=right)
    draw.line([(24, 61), (64, 42), (104, 61), (64, 81), (24, 61)], fill=light, width=2)
    draw.line([(24, 85), (64, 105), (104, 85)], fill=shade, width=3)


def draw_kolam_band(draw, y, color=(245, 238, 210, 230)):
    draw.arc((35, y - 5, 49, y + 9), 180, 360, fill=color, width=2)
    draw.arc((49, y - 5, 63, y + 9), 180, 360, fill=color, width=2)
    draw.arc((63, y - 5, 77, y + 9), 180, 360, fill=color, width=2)
    draw.arc((77, y - 5, 91, y + 9), 180, 360, fill=color, width=2)


def draw_dark_age_wall(draw):
    shadow(draw)
    draw.polygon([(25, 68), (64, 51), (103, 68), (64, 88)], fill=(126, 84, 39, 255))
    draw.polygon([(25, 68), (64, 88), (64, 98), (25, 78)], fill=(78, 53, 33, 255))
    draw.polygon([(103, 68), (64, 88), (64, 98), (103, 78)], fill=(58, 43, 30, 255))

    for x in [31, 44, 57, 70, 83, 96]:
        draw.polygon([(x, 82), (x + 6, 85), (x + 6, 44), (x, 40)], fill=(91, 57, 27, 255))
        draw.polygon([(x + 6, 85), (x + 11, 82), (x + 11, 44), (x + 6, 44)], fill=(48, 35, 24, 255))
        draw.polygon([(x, 40), (x + 5, 28), (x + 11, 44), (x + 6, 44)], fill=(166, 112, 46, 255))
        draw.line([(x + 2, 51), (x + 2, 75)], fill=(145, 96, 48, 255), width=2)

    draw.line([(31, 61), (101, 82)], fill=(187, 130, 63, 255), width=4)
    draw.line([(31, 70), (101, 91)], fill=(96, 61, 32, 255), width=4)
    for x in [40, 66, 92]:
        draw.ellipse((x - 5, 31, x + 8, 39), fill=(45, 111, 55, 255))
        draw.polygon([(x - 2, 35), (x + 11, 42), (x + 1, 44)], fill=(36, 91, 45, 255))


def draw_feudal_wall(draw):
    shadow(draw)
    draw_panel(
        draw,
        top=(205, 113, 54, 255),
        left=(147, 72, 45, 255),
        right=(103, 57, 43, 255),
        front=(0, 0, 0, 0),
        shade=(73, 42, 35, 190),
        light=(238, 170, 91, 230),
    )
    for y in [62, 72, 82]:
        draw.line([(29, y), (64, y + 17), (99, y)], fill=(83, 43, 34, 170), width=2)
    for x in [42, 62, 82]:
        draw.line([(x, 53), (x - 16, 61)], fill=(103, 51, 37, 160), width=2)
        draw.line([(x + 8, 72), (x - 10, 82)], fill=(77, 42, 35, 150), width=2)

    for x in [31, 51, 71, 91]:
        draw.rectangle((x, 39, x + 12, 57), fill=(166, 82, 48, 255))
        draw.polygon([(x, 39), (x + 6, 30), (x + 12, 39)], fill=(224, 139, 66, 255))
        draw.rectangle((x + 3, 45, x + 9, 55), fill=(116, 61, 41, 255))

    draw.line([(29, 60), (64, 76), (99, 60)], fill=(247, 236, 195, 235), width=3)
    draw_kolam_band(draw, 69)
    draw.ellipse((59, 52, 69, 61), fill=(236, 203, 102, 255))


def draw_castle_wall(draw):
    shadow(draw)
    draw_panel(
        draw,
        top=(172, 167, 146, 255),
        left=(104, 101, 91, 255),
        right=(73, 76, 74, 255),
        front=(0, 0, 0, 0),
        shade=(47, 50, 50, 170),
        light=(220, 213, 180, 230),
    )
    for x in [29, 44, 59, 74, 89]:
        draw.rectangle((x, 38, x + 10, 55), fill=(130, 128, 113, 255))
        draw.polygon([(x, 38), (x + 5, 31), (x + 10, 38)], fill=(190, 181, 145, 255))
        draw.line([(x + 2, 41), (x + 8, 41)], fill=(226, 219, 183, 255), width=2)

    for y in [64, 75, 86]:
        draw.line([(29, y), (64, y + 17), (99, y)], fill=(47, 49, 49, 160), width=2)
    for x in [42, 64, 86]:
        draw.line([(x, 53), (x - 16, 61)], fill=(59, 60, 56, 160), width=2)
        draw.line([(x, 74), (x - 18, 84)], fill=(45, 46, 44, 150), width=2)

    draw.line([(29, 60), (64, 76), (99, 60)], fill=(241, 232, 188, 230), width=3)
    for x in [49, 64, 79]:
        draw.ellipse((x - 4, 62, x + 4, 70), outline=(231, 219, 172, 230), width=2)
        draw.line([(x, 64), (x, 74)], fill=(231, 219, 172, 230), width=2)
    draw.polygon([(57, 50), (64, 44), (71, 50), (64, 56)], fill=(66, 119, 113, 255))


def draw_imperial_wall(draw):
    shadow(draw, cy=92, w=96, h=36)
    draw_panel(
        draw,
        top=(222, 215, 185, 255),
        left=(132, 135, 128, 255),
        right=(89, 99, 99, 255),
        front=(0, 0, 0, 0),
        shade=(49, 58, 59, 170),
        light=(252, 242, 202, 235),
    )
    for x in [25, 41, 57, 73, 89]:
        draw.rectangle((x, 36, x + 13, 57), fill=(177, 175, 154, 255))
        draw.polygon([(x - 1, 36), (x + 6, 26), (x + 14, 36)], fill=(238, 216, 148, 255))
        draw.rectangle((x + 4, 44, x + 9, 55), fill=(95, 104, 102, 255))

    draw.polygon([(53, 31), (64, 20), (75, 31), (73, 50), (55, 50)], fill=(185, 74, 52, 255))
    draw.polygon([(56, 31), (64, 24), (72, 31), (70, 45), (58, 45)], fill=(228, 162, 70, 255))
    draw.rectangle((60, 38, 68, 53), fill=(75, 113, 118, 255))

    draw.line([(27, 60), (64, 78), (101, 60)], fill=(249, 225, 142, 245), width=4)
    draw.line([(30, 68), (64, 85), (98, 68)], fill=(57, 67, 68, 170), width=2)
    for x in [42, 64, 86]:
        draw.line([(x, 52), (x - 17, 61)], fill=(75, 84, 82, 160), width=2)
        draw.line([(x, 76), (x - 18, 87)], fill=(56, 64, 64, 150), width=2)

    draw.ellipse((55, 61, 73, 75), fill=(226, 177, 72, 255))
    draw.polygon([(60, 65), (68, 65), (66, 70), (70, 72), (62, 72)], fill=(99, 44, 34, 255))
    draw.line([(43, 64), (84, 64)], fill=(247, 239, 199, 230), width=2)


DRAWERS = [
    draw_dark_age_wall,
    draw_feudal_wall,
    draw_castle_wall,
    draw_imperial_wall,
]


for age, drawer in enumerate(DRAWERS):
    img = Image.new("RGBA", (SIZE, SIZE), (255, 255, 255, 0))
    drawer(ImageDraw.Draw(img))
    img.save(f"assets/buildings/wall_age_{age}.png")
