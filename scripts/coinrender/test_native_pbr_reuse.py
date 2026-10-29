#!/usr/bin/env python3
"""Exercise native retained-PBR reuse admission, invalidation and failure/retry.

Run under X11/Xvfb with a scene/request from the AssemblyExample benchmark.
Pixel equivalence is qualified separately in the actual FreeCAD GUI campaign.
"""
import argparse
import ctypes as C
import json
import os
from pathlib import Path
import subprocess


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('worker','scene','request','environment','output'):
        parser.add_argument('--'+name,type=Path,required=True)
    args=parser.parse_args()
    args.output.mkdir(parents=True,exist_ok=False)
    x=C.CDLL('libX11.so.6')
    display_type=C.c_void_p; window_type=C.c_ulong
    for name,result,arguments in (
        ('XOpenDisplay',display_type,[C.c_char_p]),('XDefaultRootWindow',window_type,[display_type]),
        ('XCreateSimpleWindow',window_type,[display_type,window_type,C.c_int,C.c_int,C.c_uint,C.c_uint,C.c_uint,window_type,window_type]),
        ('XMapWindow',C.c_int,[display_type,window_type]),('XFlush',C.c_int,[display_type]),
        ('XResizeWindow',C.c_int,[display_type,window_type,C.c_uint,C.c_uint]),
        ('XDestroyWindow',C.c_int,[display_type,window_type]),('XCloseDisplay',C.c_int,[display_type])):
        function=getattr(x,name);function.restype=result;function.argtypes=arguments
    display=x.XOpenDisplay(None);assert display, 'X11 display unavailable'
    packet=args.request.read_text().split();fx=packet.index('FX')
    packet[fx+1:fx+3]=['1','1']
    w,h=map(int,packet[2:4])
    window=x.XCreateSimpleWindow(display,x.XDefaultRootWindow(display),0,0,w,h,0,0,0)
    x.XMapWindow(display,window);x.XFlush(display)
    records=[];process=None
    try:
        with (args.output/'worker.stderr').open('w') as error:
            process=subprocess.Popen([str(args.worker.resolve()),str(args.scene.resolve()),str(args.environment.resolve()),
                str(args.output.resolve()),'13',str(w),str(h),'--window',str(window),'--metrics'],
                stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=error,text=True)
            def send(stage,changed=None,valid=True):
                if changed:changed()
                packet[1]=str(len(records)+1)
                process.stdin.write(' '.join(packet)+'\n');process.stdin.flush()
                reply=json.loads(process.stdout.readline())
                assert (reply['status']=='ok')==valid, reply
                records.append(dict(stage=stage,**reply))
                return reply
            first=send('base');second=send('static')
            assert second['static_reuses']==1 and second['shaded_frames']==first['shaded_frames']==1
            third=send('exposure',lambda:packet.__setitem__(18,'1'))
            assert third['shaded_frames']==2 and third['static_reuses']==1
            fourth=send('exposure-static');assert fourth['static_reuses']==2
            for name,index,value in [('bias',fx+7,'.0002'),('radius',fx+4,'22'),('background',fx+9,'.3')]:
                old=records[-1];new=send(name,lambda i=index,v=value:packet.__setitem__(i,v))
                assert new['shaded_frames']==old['shaded_frames']+1 and new['static_reuses']==old['static_reuses']
                assert new['shadow_passes']==new['ao_depth_passes']==1
            serial=records[-1]['serial'];previous_shaded=records[-1]['shaded_frames']
            saved=packet[fx+8]
            rejected=send('budget-reject',lambda:packet.__setitem__(fx+8,'1'),False)
            assert rejected['serial']==serial
            retried=send('budget-retry',lambda:packet.__setitem__(fx+8,saved))
            assert retried['serial']==serial+1 and retried['shaded_frames']==previous_shaded+1
            old=send('retry-static');assert old['static_reuses']==3
            def resize():
                packet[2:4]=[str(w+40),str(h-20)]
                x.XResizeWindow(display,window,w+40,h-20);x.XFlush(display)
            new=send('resize',resize)
            assert new['shaded_frames']==old['shaded_frames']+1 and new['static_reuses']==old['static_reuses']
            old=send('resize-static');assert old['static_reuses']==4
            new=send('selection',lambda:packet.__setitem__(27,'1'))
            assert new['shaded_frames']==old['shaded_frames']+1
            old=new;new=send('translation',lambda:packet.__setitem__(19,str(float(packet[19])+35)))
            assert new['shaded_frames']==old['shaded_frames']+1 and new['captures']==old['captures'] and new['plans']==old['plans']
            last=send('last-static');assert last['static_reuses']==5
            assert all(r['pixel_readbacks']==r['cpu_pixel_bytes']==0 for r in records if r['status']=='ok')
            process.stdin.write('QUIT\n');process.stdin.flush();process.wait(timeout=10)
            assert process.returncode==0, 'Retirement after a queued static repaint failed'
        (args.output/'results.json').write_text(json.dumps(dict(status='PASS',records=records),indent=2)+'\n')
        print('PASS:',len(records),'native PBR reuse/admission/retirement checks')
    finally:
        if process and process.poll() is None:
            process.terminate();process.wait(timeout=10)
        # The XID outlives the worker and all queued presentation work.
        x.XDestroyWindow(display,window);x.XCloseDisplay(display)


if __name__=='__main__':main()
