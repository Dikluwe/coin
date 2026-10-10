import ctypes as c, ctypes.wintypes as w, json, pathlib
u=c.WinDLL('user32',use_last_error=True);s=c.WinDLL('shcore',use_last_error=True)
u.SetProcessDpiAwarenessContext.argtypes=[c.c_void_p]
u.SetProcessDpiAwarenessContext(c.c_void_p(-4))
rows=[];CB=c.WINFUNCTYPE(w.BOOL,w.HMONITOR,w.HDC,c.POINTER(w.RECT),w.LPARAM)
@CB
def collect(h,d,r,p):
    x=w.UINT();y=w.UINT();s.GetDpiForMonitor.argtypes=[w.HMONITOR,c.c_int,c.POINTER(w.UINT),c.POINTER(w.UINT)]
    code=s.GetDpiForMonitor(h,0,c.byref(x),c.byref(y))
    rows.append(dict(bounds=[r.contents.left,r.contents.top,r.contents.right,r.contents.bottom],effective_dpi=[x.value,y.value],hresult=code))
    return True
u.EnumDisplayMonitors(None,None,collect,0)
result=dict(monitors=rows,distinct_effective_dpi=len({tuple(r['effective_dpi']) for r in rows})>1)
pathlib.Path(__file__).with_name('monitors.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
print(json.dumps(result,indent=2))
