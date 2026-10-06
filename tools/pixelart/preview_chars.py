import numpy as np
from characters import SPECS, sheet, N as S
from png import write_png, upscale
names = list(SPECS)
sheets = {n: sheet(SPECS[n]) for n in names}
# preview: per character, row of: down idle0, down walk1, down attack0..3, side walk1, side attack2, up idle0, up attack2, dead
picks = [(0,0),(1,1),(2,0),(2,1),(2,2),(2,3),(9,1),(10,2),(10,3),(4,0),(6,2),(12,0)]
bg = np.zeros((S*len(names), S*len(picks), 4), np.uint8); bg[...] = (120,160,90,255)
for i,n in enumerate(names):
    sh = sheets[n]
    for j,(r,c) in enumerate(picks):
        fr = sh[r*S:(r+1)*S, c*S:(c+1)*S].astype(float)
        a = fr[...,3:4]/255.0
        tile = bg[i*S:(i+1)*S, j*S:(j+1)*S].astype(float)
        bg[i*S:(i+1)*S, j*S:(j+1)*S] = (tile*(1-a) + fr*a).astype(np.uint8)
write_png('out/preview_chars.png', upscale(bg, 3))
print('ok', bg.shape)
