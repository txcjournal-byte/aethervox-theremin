import sys, numpy as np, soundfile as sf
from kokoro import KPipeline
p = KPipeline(lang_code='a')
voices = sys.argv[1].split(',')
for line in open('web/video/vo/lines.txt'):
    line=line.strip()
    if not line: continue
    k, t = line.split('|',1)
    for v in voices:
        parts=[a for _,_,a in p(t, voice=v, speed=0.9)]
        sf.write(f'out/{v}_{k}.wav', np.concatenate(parts), 24000)
print('done')
