"""Optical-flow morphing: generates real in-between frames between two pose photos,
so the arm actually travels instead of two ghost arms cross-fading."""
import cv2, numpy as np

def flow(a, b):
    ga, gb = (cv2.cvtColor(x, cv2.COLOR_RGB2GRAY) for x in (a, b))
    dis = cv2.DISOpticalFlow_create(cv2.DISOPTICAL_FLOW_PRESET_ULTRAFAST)
    dis.setFinestScale(0); dis.setPatchSize(12); dis.setPatchStride(4)
    dis.setGradientDescentIterations(40); dis.setVariationalRefinementIterations(10)
    dis.setVariationalRefinementAlpha(30.0)
    f = dis.calc(ga, gb, None)
    # smooth flow so the limb moves as one piece
    return cv2.GaussianBlur(f, (0, 0), 3)

def warp(img, f, t):
    h, w = img.shape[:2]
    gx, gy = np.meshgrid(np.arange(w, dtype=np.float32), np.arange(h, dtype=np.float32))
    return cv2.remap(img, gx - f[..., 0] * t, gy - f[..., 1] * t, cv2.INTER_LINEAR, borderMode=cv2.BORDER_REFLECT)

def inbetweens(a, b, n):
    """a, b uint8 RGB. returns n frames from a (t=0) to b (t=1)."""
    fab, fba = flow(a, b), flow(b, a)
    out = []
    for i in range(n):
        t = i / (n - 1)
        # backward warping approximation: sample A at x - t*F_ba(x)... use symmetric pair
        wa = warp(a, -fba, t)        # A moved toward B
        wb = warp(b, -fab, 1 - t)    # B moved back toward A
        s = t * t * (3 - 2 * t)
        out.append((wa.astype(np.float32) * (1 - s) + wb.astype(np.float32) * s).astype(np.uint8))
    return out
