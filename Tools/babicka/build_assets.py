"""Builds the animation assets for the Babička plugin GUI.
  base.jpg              full background photo (1158 x 652)
  pitch_NN.jpg          crops of the pitch-arm region, low -> base -> high (optical-flow in-betweens)
  vol_NN.jpg            crops of the volume-arm region, low (quiet) -> base (loud)
  blink_NN.jpg          crops of the eye region, open -> closed
  mask_pitch/vol/blink.png   soft alpha masks for the crops
  regions.txt           crop rectangles (x y w h) for the C++ side
"""
import numpy as np, cv2, os
from PIL import Image, ImageFilter

SRC = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(SRC, 'out'); os.makedirs(OUT, exist_ok=True)
L = lambda n: np.asarray(Image.open(os.path.join(SRC, n + '.png')).convert('RGB'))
base, phi, plo, vlo, blink = (L(n) for n in ['base', 'p_high', 'p_low', 'v_low', 'blink'])
H, W = base.shape[:2]

def fb_flow(a, b):
    ga, gb = (cv2.cvtColor(x, cv2.COLOR_RGB2GRAY) for x in (a, b))
    f = cv2.calcOpticalFlowFarneback(ga, gb, None, 0.5, 6, 41, 8, 7, 1.5, cv2.OPTFLOW_FARNEBACK_GAUSSIAN)
    return cv2.GaussianBlur(f, (0, 0), 2)

gx, gy = np.meshgrid(np.arange(W, dtype=np.float32), np.arange(H, dtype=np.float32))
def warp(img, f, t):
    return cv2.remap(img, gx - f[..., 0] * t, gy - f[..., 1] * t, cv2.INTER_LINEAR, borderMode=cv2.BORDER_REFLECT)

def sequence(a, b, n):
    fab, fba = fb_flow(a, b), fb_flow(b, a)
    out = []
    for i in range(n):
        t = i / (n - 1)
        wa, wb = warp(a, -fba, t), warp(b, -fab, 1 - t)
        s = np.clip((t - 0.3) / 0.4, 0, 1); s = s * s * (3 - 2 * s)   # short dissolve in the middle
        out.append((wa.astype(np.float32) * (1 - s) + wb.astype(np.float32) * s).astype(np.uint8))
    return out

def diff_mask(frames, box, thr=0.10):
    d = np.zeros((H, W), np.float32)
    for f in frames:
        d = np.maximum(d, np.abs(f.astype(np.float32) - base.astype(np.float32)).max(axis=2) / 255)
    m = Image.fromarray(((d > thr) * 255).astype(np.uint8)).filter(ImageFilter.MedianFilter(5)).filter(ImageFilter.MaxFilter(25))
    bm = Image.new('L', (W, H), 0); bm.paste(255, box); bm = bm.filter(ImageFilter.GaussianBlur(6))
    m = np.asarray(m, np.float32) * np.asarray(bm, np.float32) / 255
    return np.asarray(Image.fromarray(m.astype(np.uint8)).filter(ImageFilter.GaussianBlur(10)))

def box_mask(box, feather):
    m = Image.new('L', (W, H), 0); m.paste(255, box)
    return np.asarray(m.filter(ImageFilter.GaussianBlur(feather)))

pitch = sequence(plo, base, 13)[:-1] + sequence(base, phi, 13)       # 25 frames
vol   = sequence(vlo, base, 13)                                        # 13 frames
blk   = [base] + sequence(base, blink, 5)[1:]                          # 5 frames

masks = {
    'pitch': diff_mask(pitch, (300, 55, 545, 338)),
    'vol':   diff_mask(vol, (690, 215, 845, 305)),
    'blink': box_mask((530, 140, 600, 188), 5),
}
regions = {}
for k, m in masks.items():
    ys, xs = np.nonzero(m > 2)
    x0, y0, x1, y1 = xs.min(), ys.min(), xs.max() + 1, ys.max() + 1
    regions[k] = (x0, y0, x1 - x0, y1 - y0)
    Image.fromarray(m[y0:y1, x0:x1]).save(os.path.join(OUT, f'mask_{k}.png'), optimize=True)

def save_seq(name, frames):
    x, y, w, h = regions[name]
    for i, f in enumerate(frames):
        Image.fromarray(f[y:y + h, x:x + w]).save(os.path.join(OUT, f'{name}_{i:02d}.jpg'), quality=90)

save_seq('pitch', pitch); save_seq('vol', vol); save_seq('blink', blk)
Image.fromarray(base).save(os.path.join(OUT, 'base.jpg'), quality=92)
with open(os.path.join(OUT, 'regions.txt'), 'w') as fh:
    for k, r in regions.items():
        fh.write(f'{k} {r[0]} {r[1]} {r[2]} {r[3]}\n')
print(regions, len(pitch), len(vol), len(blk))
