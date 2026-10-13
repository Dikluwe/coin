import subprocess,os,json
from pathlib import Path
from PIL import Image,ImageDraw
r=Path('/tmp/coin-shadow-cad-s6');env=os.environ.copy();env.update(DISPLAY=':0',VK_DRIVER_FILES='/usr/share/vulkan/icd.d/radeon_icd.json',COIN_BGFX_RENDERER='vulkan',COIN_RENDER_TRACE_PHASES='1')
cases=[('flange','vsm',True,False,1),('flange','pcf',True,False,1),('flange','pcss',True,False,1),('assembly','pcss',False,False,2),('housing','pcf',True,True,1)]
commands=[]
for fixture,filter,offscreen,cut,frames in cases:
 name=fixture+'-'+filter+('-cut' if cut else '')+('-window' if not offscreen else '')
 args=['/tmp/coin-pbr-bgfx/bin/coin_render_viewer','--fixture',fixture,'--view','iso-perspective','--pbr','--shadows',filter,'--cad-tools','--width','800','--height','600','--frames',str(frames),'--capture-prefix',str(r/name),'--environment',str(r/'studio.cibl')]
 if offscreen:args+=['--offscreen','--capture-depth','--validate-depth']
 if cut:args+=['--cut']
 with (r/(name+'.log')).open('w') as out:subprocess.run(args,env=env,stdout=out,stderr=subprocess.STDOUT,check=True)
 commands.append(args)
 meta=json.loads((r/(name+'-000.json')).read_text());assert meta['shadows'] and meta['shadow_filter']=={'vsm':0,'pcf':1,'pcss':2}[filter] and meta['selected_name']
 if offscreen:assert meta['depth_oracle_probes']>100
 Image.open(r/(name+'-000.ppm')).save(r/(name+'-000.png'))
 if frames>1:assert (r/(name+'-000.ppm')).read_bytes()==(r/(name+'-001.ppm')).read_bytes()
(r/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
canvas=Image.new('RGB',(1200,626),'#20252b');draw=ImageDraw.Draw(canvas)
for index,name in enumerate(['flange-pcss','assembly-pcss-window','housing-pcf-cut']):
 with Image.open(r/(name+'-000.png')) as im:canvas.paste(im.resize((400,300),Image.Resampling.LANCZOS),(index*400,26))
 draw.text((index*400+10,8),name,fill='white')
for index,name in enumerate(['flange-vsm','flange-pcf','flange-pcss']):
 with Image.open(r/(name+'-000.png')) as im:canvas.paste(im.resize((400,300),Image.Resampling.LANCZOS),(index*400,326))
canvas.save(r/'cad-gallery.png')
print(json.dumps({'cad_cases':len(cases),'all_renders_success':True,'depth_oracle':'passed for all offscreen captures','native_window_replay':'identical'}))
