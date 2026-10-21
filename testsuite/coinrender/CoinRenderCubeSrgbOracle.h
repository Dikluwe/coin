#ifndef COIN_RENDER_CUBE_SRGB_ORACLE_H
#define COIN_RENDER_CUBE_SRGB_ORACLE_H
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>
namespace cube_srgb_oracle {
using Pixel=std::array<double,4>;
using Face=std::vector<uint8_t>;
using Faces=std::array<Face,6>;
struct Level { unsigned size; Faces faces; };
inline double decode(double code) { double v=code/255.;return v<=.04045?v/12.92:std::pow((v+.055)/1.055,2.4); }
inline uint8_t encode(double light) { return uint8_t(std::lround(255*(light<=.0031308?12.92*light:1.055*std::pow(light,1./2.4)-.055))); }
inline Faces authored(unsigned n,bool opaque=false) {
 Faces f;for(unsigned face=0;face<6;++face){f[face].resize(size_t(n)*n*4);for(unsigned y=0;y<n;++y)for(unsigned x=0;x<n;++x){size_t p=(size_t(y)*n+x)*4;f[face][p]=uint8_t((7+face*29+x*37+y*19)%256);f[face][p+1]=uint8_t((10+face*11+x*13+y*61)%256);f[face][p+2]=uint8_t((11+face*43+x*71+y*7)%256);f[face][p+3]=opaque?255:uint8_t((1+face*17+x*47+y*31)%256);}}return f;
}
inline Faces expanded(Faces f,unsigned components) {
 for(auto&face:f)for(size_t p=0;p<face.size();p+=4){if(components<=2)face[p+1]=face[p+2]=face[p];if(components==1||components==3)face[p+3]=255;}return f;
}
inline std::vector<Level> chain(unsigned n,Faces f,bool mipmapped=true,bool srgb=true) {
 std::vector<Level> result{{n,std::move(f)}};
 while(mipmapped&&n>1){const auto&last=result.back();unsigned m=n/2;Faces next;for(unsigned face=0;face<6;++face){next[face].resize(size_t(m)*m*4);for(unsigned y=0;y<m;++y)for(unsigned x=0;x<m;++x)for(unsigned c=0;c<4;++c){double sum=0;for(unsigned dy=0;dy<2;++dy)for(unsigned dx=0;dx<2;++dx){auto v=last.faces[face][((size_t(2*y+dy)*n+2*x+dx)*4)+c];sum+=srgb&&c<3?decode(v):double(v);}next[face][(size_t(y)*m+x)*4+c]=srgb&&c<3?encode(sum/4):uint8_t(std::floor((sum+2)/4));}}result.push_back({m,std::move(next)});n=m;}return result;
}
inline std::vector<uint8_t> atlas(const std::vector<Level>&levels,unsigned&width,unsigned&height) {
 width=levels.front().size*2;size_t texels=0;for(const auto&l:levels)texels+=size_t(6)*l.size*l.size;height=unsigned((texels+width-1)/width);std::vector<uint8_t> bytes(size_t(width)*height*4,0);size_t offset=0;for(const auto&l:levels)for(const auto&f:l.faces){std::copy(f.begin(),f.end(),bytes.begin()+offset);offset+=f.size();}return bytes;
}
// An explicit face basis table keeps coordinate expectations independent of Core.
struct Projection { unsigned face; double u,v; };
inline Projection project(std::array<double,3> ray,bool floatCoordinates=false) {
 // Public Core receives SbVec3f and returns SbVec2f. Model their two
 // rounding boundaries explicitly, while retaining independent face algebra.
 if(floatCoordinates)for(auto&v:ray)v=double(float(v));
 unsigned axis=0;for(unsigned a=1;a<3;++a)if(std::abs(ray[a])>std::abs(ray[axis]))axis=a;
 unsigned face=2*axis+(ray[axis]<0);double major=std::abs(ray[axis]);
 const int sAxis[]={2,2,0,0,0,0},sSign[]={-1,1,1,1,1,-1},tAxis[]={1,1,2,2,1,1},tSign[]={-1,-1,1,-1,-1,-1};
 double u=.5+.5*sSign[face]*ray[sAxis[face]]/major,v=.5+.5*tSign[face]*ray[tAxis[face]]/major;
 if(floatCoordinates){u=double(float(u));v=double(float(v));}
 return {face,u,v};
}
inline Pixel sample(const std::vector<Level>&levels,std::array<double,3> ray,int filter=0,double lod=0,bool srgb=true,bool floatCoordinates=false) {
 const auto projected=project(ray,floatCoordinates);unsigned face=projected.face;double u=projected.u,v=projected.v;
 auto levelSample=[&](unsigned index,bool linear){const auto&l=levels[index];auto fetch=[&](int x,int y,unsigned c){x=std::max(0,std::min(int(l.size)-1,x));y=std::max(0,std::min(int(l.size)-1,y));auto b=l.faces[face][(size_t(y)*l.size+x)*4+c];return srgb&&c<3?decode(b):double(b)/255.;};Pixel p{};if(!linear){for(unsigned c=0;c<4;++c)p[c]=fetch(int(std::floor(u*l.size)),int(std::floor(v*l.size)),c);}else{double x=u*l.size-.5,y=v*l.size-.5;int ix=int(std::floor(x)),iy=int(std::floor(y));double fx=x-ix,fy=y-iy;for(unsigned c=0;c<4;++c)p[c]=(1-fy)*((1-fx)*fetch(ix,iy,c)+fx*fetch(ix+1,iy,c))+fy*((1-fx)*fetch(ix,iy+1,c)+fx*fetch(ix+1,iy+1,c));}return p;};
 bool mips=filter>=2;bool linear=filter==1||filter==3||(filter==2&&lod<=0);if(!mips)return levelSample(0,linear);double l=std::max(0.,std::min(double(levels.size()-1),lod));unsigned a=unsigned(std::floor(l)),b=std::min(unsigned(levels.size()-1),a+1);auto x=levelSample(a,linear),y=levelSample(b,linear);for(unsigned c=0;c<4;++c)x[c]+=(y[c]-x[c])*(l-a);return x;
}
}
#endif
