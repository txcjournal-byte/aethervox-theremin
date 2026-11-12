"""Prototype of the grandma animation compositor (same math goes into the JUCE editor).
Builds soft region masks, then blends pose frames by pitch / volume / blink and adds
subtle breathing. Renders a demo video and exports masks for the plugin."""
import numpy as np, subprocess, math
from PIL import Image, ImageFilter

W, H = 1158, 652
L = lambda n: np.asarray(Image.open(n + '.png').convert('RGB'), np.float32) / 255.0
base, phi, plo, vlo, blink = (L(n) for n in ['base', 'p_high', 'p_low', 'v_low', 'blink'])

def soft_mask(x0, y0, x1, y1, feather):
    m = Image.new('L', (W, H), 0)
    m.paste(255, (x0, y0, x1, y1))
    m = m.filter(ImageFilter.GaussianBlur(feather))
    return m

def diff_mask(frames, box, feather=10, thr=0.10):
    """Pixels that actually differ between pose frames, limited to box, dilated + blurred."""
    d = np.zeros((H, W), np.float32)
    for f in frames:
        d = np.maximum(d, np.abs(f - base).max(axis=2))
    m = (d > thr).astype(np.uint8) * 255
    im = Image.fromarray(m).filter(ImageFilter.MedianFilter(5)).filter(ImageFilter.MaxFilter(21))
    box_m = soft_mask(*box, 6)
    im = Image.fromarray((np.asarray(im, np.float32) * np.asarray(box_m, np.float32) / 255).astype(np.uint8))
    return im.filter(ImageFilter.GaussianBlur(feather))

mP = diff_mask([phi, plo], (225, 10, 525, 318))
mV = diff_mask([vlo], (640, 170, 900, 305))
mB = soft_mask(518, 75, 602, 120, 6)
for n, m in [('mask_pitch', mP), ('mask_vol', mV), ('mask_blink', mB)]:
    m.save(n + '.png')
mP, mV, mB = (np.asarray(m, np.float32)[..., None] / 255.0 for m in (mP, mV, mB))

def compose(pitch, vol, blinkAmt):
    # pitch 0..1 : low -> base -> high
    if pitch < 0.5:
        a = pitch * 2; pf = plo * (1 - a) + base * a
    else:
        a = (pitch - 0.5) * 2; pf = base * (1 - a) + phi * a
    img = base * (1 - mP) + pf * mP
    vf = vlo * (1 - vol) + base * vol            # vol 1 = hand high (loud)
    img = img * (1 - mV) + vf * mV
    bf = img * (1 - blinkAmt) + blink * blinkAmt
    img = img * (1 - mB) + bf * mB
    return img

if __name__ == '__main__':
    fps, secs = 30, 8
    p = subprocess.Popen(['ffmpeg', '-y', '-loglevel', 'error', '-f', 'rawvideo', '-pix_fmt', 'rgb24',
                          '-s', f'{W}x{H}', '-r', str(fps), '-i', '-', '-c:v', 'libx264', '-preset', 'veryfast',
                          '-pix_fmt', 'yuv420p', '-crf', '20', 'demo.mp4'], stdin=subprocess.PIPE)
    for i in range(fps * secs):
        t = i / fps
        pitch = 0.5 + 0.5 * math.sin(t * 1.3)
        vol = 0.5 + 0.5 * math.sin(t * 0.9 + 1.0)
        bl = max(0.0, 1 - abs((t % 3.2) - 1.6) / 0.12)
        p.stdin.write((np.clip(compose(pitch, vol, bl), 0, 1) * 255).astype(np.uint8).tobytes())
    p.stdin.close(); p.wait()
    Image.fromarray((compose(0.5, 1, 0) * 255).astype(np.uint8)).save('/tmp/c_mid.png')
    Image.fromarray((compose(0.25, 0.5, 0) * 255).astype(np.uint8)).save('/tmp/c_q.png')
