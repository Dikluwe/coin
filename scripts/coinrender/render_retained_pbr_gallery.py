#!/usr/bin/env python3
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont
import struct
import argparse
parser=argparse.ArgumentParser(description='Create labeled previews from real BGFX gallery readbacks.')
parser.add_argument('--input',type=Path,required=True)
parser.add_argument('--output',type=Path,required=True)
args=parser.parse_args();root=args.input;out=args.output;out.mkdir(parents=True,exist_ok=True)
fontroot=Path('/usr/share/fonts/truetype/dejavu')
def font(n,bold=False): return ImageFont.truetype(str(fontroot/('DejaVuSans-Bold.ttf' if bold else 'DejaVuSans.ttf')),n)
def raw(scene,stops):return Image.frombytes('RGBA',(1200,840),(root/f'{scene}-{stops}.rgba').read_bytes()).convert('RGB')
for scene in ['materials','cad']:
 for stops in [-2,0,2]:raw(scene,stops).save(out/f'{scene}-{stops}.png')
canvas=Image.new('RGB',(1430,1040),'#080c12');d=ImageDraw.Draw(canvas)
d.text((32,25),'PBR · captura retida → BGFX/Vulkan',font=font(32,True),fill='#eef4ff')
d.text((32,78),'Luzes diretas · exposição 0 EV · sem iluminação de ambiente (IBL)',font=font(20),fill='#b9c8da')
canvas.paste(raw('materials',0),(210,155))
for x,label in zip([199,466,733,1001],['0,08','0,25','0,50','0,85']):
 d.text((210+x,124),'Rugosidade '+label,font=font(18),fill='#dce6f4',anchor='mm')
for y,title,sub in [(327,'Polímero azul','metallic = 0'),(575,'Metal dourado','metallic = 1'),(823,'Verniz vermelho','clearcoat = 1')]:
 d.text((22,y),title,font=font(20,True),fill='#eef4ff');d.text((22,y+34),sub,font=font(17),fill='#9eafc5')
d.text((32,1000),'Pixels e profundidade coincidem com o caminho atual e a branch PBR de origem.',font=font(18),fill='#9eafc5')
canvas.save(out/'materials-gallery.png')
# Crop only unused background, using the original depth coverage; one fixed crop for all exposures.
depth=struct.unpack('<'+'f'*(1200*840),(root/'cad-0.depth').read_bytes())
covered=[i for i,z in enumerate(depth) if z<1]
xs=[i%1200 for i in covered];ys=[i//1200 for i in covered]
box=(max(0,min(xs)-60),max(0,min(ys)-60),min(1200,max(xs)+61),min(840,max(ys)+61))
canvas=Image.new('RGB',(1440,520),'#080c12');d=ImageDraw.Draw(canvas)
d.text((26,20),'Mesma cena e câmera · só a exposição muda',font=font(29,True),fill='#eef4ff')
for j,stops in enumerate([-2,0,2]):
 img=raw('cad',stops).crop(box);img.thumbnail((464,330),Image.Resampling.LANCZOS)
 canvas.paste(img,(j*480+(480-img.width)//2,90+(330-img.height)//2))
 d.text((j*480+240,445),f'{stops:+d} EV',font=font(26,True),fill='#eef4ff',anchor='mm')
d.text((26,490),'Cobre · polímero azul · verniz vermelho · metal rugoso. Captura/plano/transporte: 1 / 1 / 1.',font=font(17),fill='#9eafc5')
canvas.save(out/'exposure-gallery.png')
print(out)
