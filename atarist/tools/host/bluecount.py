"""bluecount.py <dir>: blue stipple pixels (game pixels) outside the number in the top left corner, per Hatari screenshot"""
import sys, glob
from PIL import Image
tot = n = 0
for f in sorted(glob.glob(sys.argv[1] + '/e*.png')):
    im = Image.open(f).convert('RGB').crop((96, 56, 736, 460)); px = im.load(); out = 0
    for y in range(im.height):
        for x in range(im.width):
            if px[x, y] == (102, 136, 204) and not (x < 90 and y < 40): out += 1
    tot += out / 16; n += 1; print(f[-8:-4], round(out / 16, 1), end='  ')
print('\nframes', n, 'average blue game pixels per shown picture', round(tot / max(n, 1), 2))
