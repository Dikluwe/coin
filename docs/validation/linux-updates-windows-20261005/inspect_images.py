from pathlib import Path
from PIL import Image, ImageDraw
import json

root=Path('H:/Git/coin/build/linux-updates-windows-20261005')
summary=json.loads((root/'summary.json').read_text())
worst=max(summary['animation_checks'],key=lambda row:row['mae_rgb'])
prefix=('animation-' if worst['kind']=='animation' else 'palette-')
variant=worst['variant']
case=worst['case']
logical=worst['logical_frame']
def frame_path(name):
    run=next(row for row in summary['runs'] if row['kind']==worst['kind'] and row['variant']==name and row['sample']==case)
    for line in (root/run['log']).read_text().splitlines():
        if line.startswith('capture ') and 'logical_frame='+str(logical)+' ' in line:
            return Path(line.split('image=')[1].split()[0])
    raise RuntimeError('No capture')
files=[('CoinGL static',root/'static-coingl-after-1.ppm'),
       ('BGFX OpenGL static',root/'static-bgfx-opengl-after-1.ppm'),
       ('wgpu OpenGL static',root/'static-wgpu-opengl-after-1.ppm'),
       (f'CoinGL {case} frame {logical}',frame_path('coingl')),
       (f'{variant} {case} frame {logical}',frame_path(variant))]
sheet=Image.new('RGB',(1200,864),'white')
draw=ImageDraw.Draw(sheet)
for index,(label,path) in enumerate(files):
    x=(index%3)*400;y=(index//3)*432
    sheet.paste(Image.open(path).resize((400,400)),(x,y+32))
    draw.text((x+8,y+8),label,fill='black')
sheet.save(root/'image-inspection.png')
print('Worst image pair:',worst)
