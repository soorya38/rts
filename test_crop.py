from PIL import Image
img = Image.open('assets/spritesheet2.png')
ys = [135,170,210,255,290,325,365,410,455,495,535,605,650,675,720,765,815,865,920,970,1040,1080,1140,1200,1260,1300,1380,1400]
import os
os.makedirs('assets/extracted/test/', exist_ok=True)
for i, y in enumerate(ys):
    crop = img.crop((105, max(0, y-30), 289, min(img.height, y+30)))
    crop.save(f'assets/extracted/test/{i}_{y}.png')
