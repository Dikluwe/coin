import pathlib
r=pathlib.Path(__file__).parent
backup=r/'visual-initial-source';backup.mkdir(exist_ok=True)
for f in ['main.cpp','CMakeLists.txt']:(backup/f).write_bytes((r/'visual-src'/f).read_bytes())
s=(r/'visual-src/main.cpp').read_text().replace('#include <windows.h>','#include <windows.h>\n#include <cstdlib>')
s=s.replace('if(work!="city-40000"&&work!="terrain-million"&&work!="solids")','if(work!="city-40000"&&work!="city-million"&&work!="terrain-million"&&work!="solids")')
s=s.replace('work=="city-40000"?140:', '(work=="city-40000"||work=="city-million")?155:')
s=s.replace('if(work=="city-40000") {', '''if(work=="city-million") {
SoMaterial*colors[8];SoCube* shapes[8];for(int k=0;k<8;++k){colors[k]=new SoMaterial;colors[k]->diffuseColor=palette[k];shapes[k]=new SoCube;shapes[k]->width=.085f;shapes[k]->depth=.085f;shapes[k]->height=.4f+k*.5f;}
for(int z=0;z<1000;++z)for(int x=0;x<1000;++x){unsigned hash=(unsigned(x)*73856093u)^(unsigned(z)*19349663u);int k=hash%8;auto*sep=new SoSeparator;auto*t=new SoTranslation;t->translation=SbVec3f((x-499.5f)*.12f,(.4f+k*.5f)*.5f,(z-499.5f)*.12f);sep->addChild(t);sep->addChild(colors[(x/39+z/57)%8]);sep->addChild(shapes[k]);root->addChild(sep);}
triangles=12000000;std::cout<<"fixture buildings=1000000 triangles=12000000 million_instances=1 shared_shape_nodes=8 lighting=directional palette=8\\n";
} else if(work=="city-40000") {''')
s=s.replace('if(!oracle("initial"))return 1;', 'bool initialOK=oracle("initial");')
s=s.replace('if(ok)ok=oracle("final");','if(ok)ok=oracle("final")&&initialOK;')
s=s.replace('view(.65f+.35f*float(i+warmup+1)/float(frames+warmup));', 'if(!std::getenv("COIN_VISUAL_STATIC_CAMERA"))view(.65f+.35f*float(i+warmup+1)/float(frames+warmup));')
s=s.replace('<<"scene nodes="<<cubes.size()<<" triangles="<<triangles<<" textured=0\\n";', '<<"scene cube_instances="<<(work=="city-million"?1000000:cubes.size())<<" counted_triangles="<<triangles<<" primitive_triangle_count_known="<<(work!="solids")<<" textured="<<(work=="solids")<<" camera_static="<<(std::getenv("COIN_VISUAL_STATIC_CAMERA")!=nullptr)<<"\\n";')
(r/'visual-src/main.cpp').write_text(s)
s=(r/'run_visual.py').read_text().replace("env['COIN_BGFX_TRACE_GL_ADAPTER']='1';cmd=", "env['COIN_BGFX_TRACE_GL_ADAPTER']='1';\n    if len(sys.argv)>6 and sys.argv[6]=='static':env['COIN_VISUAL_STATIC_CAMERA']='1'\n    cmd=")
(r/'run_visual.py').write_text(s)
