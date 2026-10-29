#!/usr/bin/env python3
"""Compose labeled examples from real retained IBL readbacks; no synthetic shading."""
import argparse
from pathlib import Path
import struct
from PIL import Image,ImageDraw,ImageFont
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--studio',type=Path,required=True)
p.add_argument('--asymmetric',type=Path,required=True)
p.add_argument('--output',type=Path,required=True)
a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
def font(size,bold=False):return ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans'+('-Bold' if bold else '')+'.ttf',size)
def raw(root,scene,state):return Image.frombytes('RGBA',(1200,840),(root/f'{scene}-{state}.rgba').read_bytes()).convert('RGB')
for name,root in [('studio',a.studio),('asymmetric',a.asymmetric)]:
 for scene in ['materials','cad']:
  for state in ['base','rotated','dim','off','exposure']:raw(root,scene,state).save(a.output/f'{name}-{scene}-{state}.png')
canvas=Image.new('RGB',(1430,1040),'#080c12');d=ImageDraw.Draw(canvas)
d.text((32,25),'PBR + IBL · captura retida → BGFX/Vulkan',font=font(32,True),fill='#eef4ff')
d.text((32,78),'Ambiente HDR studio · intensidade 1 · exposição 0 EV · sem luzes diretas',font=font(20),fill='#b9c8da')
canvas.paste(raw(a.studio,'materials','base'),(210,155))
for x,label in zip([199,466,733,1001],['0,08','0,25','0,50','0,85']):
 d.text((210+x,124),'Rugosidade '+label,font=font(18),fill='#dce6f4',anchor='mm')
for y,title,sub in [(327,'Polímero azul','metallic = 0'),(575,'Metal dourado','metallic = 1'),(823,'Verniz vermelho','clearcoat = 1')]:
 d.text((22,y),title,font=font(20,True),fill='#eef4ff');d.text((22,y+34),sub,font=font(17),fill='#9eafc5')
d.text((32,1000),'1 captura · 1 plano · 1 transporte · 1 upload do ambiente. Cor/profundidade iguais aos controles.',font=font(18),fill='#9eafc5')
canvas.save(a.output/'ibl-materials-gallery.png')
depth=struct.unpack('<'+'f'*(1200*840),(a.studio/'cad-base.depth').read_bytes())
covered=[i for i,z in enumerate(depth) if z<1];xs=[i%1200 for i in covered];ys=[i//1200 for i in covered]
box=(max(0,min(xs)-50),max(0,min(ys)-50),min(1200,max(xs)+51),min(840,max(ys)+51))
canvas=Image.new('RGB',(1440,540),'#080c12');d=ImageDraw.Draw(canvas)
d.text((26,20),'Mesma cena e câmera · ambiente e rotação variam',font=font(28,True),fill='#eef4ff')
d.text((26,68),'Somente IBL global · exposição 0 EV · sem sombras ou oclusão local',font=font(20),fill='#b9c8da')
for j,(root,state,title) in enumerate([(a.studio,'base','Studio'),(a.asymmetric,'base','Assimétrico'),(a.asymmetric,'rotated','Assimétrico · +90°')]):
 img=raw(root,'cad',state).crop(box);img.thumbnail((464,300),Image.Resampling.LANCZOS)
 canvas.paste(img,(j*480+(480-img.width)//2,125+(300-img.height)//2))
 d.text((j*480+240,465),title,font=font(24,True),fill='#eef4ff',anchor='mm')
d.text((26,510),'Cubemap GGX + irradiância SH9 + LUT. Rotação e intensidade reutilizam captura, plano e texturas.',font=font(17),fill='#9eafc5')
canvas.save(a.output/'ibl-environments-gallery.png')
print(a.output)
