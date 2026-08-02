#!/usr/bin/env python3
from PIL import Image
img = Image.open("assets/gui/bottombar.png").convert("RGB")
c = img.crop((280, 0, 460, 33)).resize((180 * 5, 33 * 5), Image.NEAREST)
c.save("tools/bb_right_zoom.png")
print("saved")
