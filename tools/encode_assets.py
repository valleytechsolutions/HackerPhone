"""Compile the owner's original PNGs into fitted RGB565 firmware assets."""
from pathlib import Path
from PIL import Image,ImageOps
root=Path(__file__).resolve().parents[1]
lines=['// Generated from unchanged owner-supplied PNGs by tools/encode_assets.py.','#pragma once','#include <stdint.h>']
for name,file,size in [('bwm','bwm-boot-original.png',(66,74)),('vts','vts-wallpaper-original.png',(78,88))]:
    source=Image.open(root/'assets'/file).convert('RGBA')
    fitted=ImageOps.contain(source,size,Image.Resampling.LANCZOS)
    background=Image.new('RGBA',fitted.size,(0,0,0,255));background.alpha_composite(fitted)
    rgb=background.convert('RGB');w,h=rgb.size
    lines.append(f'constexpr unsigned {name}Width={w}, {name}Height={h};')
    values=[((r>>3)<<11)|((g>>2)<<5)|(b>>3) for r,g,b in rgb.getdata()]
    lines.append(f'static const uint16_t {name}Pixels[{len(values)}] = {{')
    lines.extend(','.join(f'0x{x:04x}' for x in values[i:i+16])+',' for i in range(0,len(values),16))
    lines.append('};')
(root/'firmware/include/logo_assets.h').write_text('\n'.join(lines)+'\n')
print('Compiled both logos from unchanged source PNGs')
