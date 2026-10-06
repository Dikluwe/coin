#include <Inventor/SoDB.h>
#include "Inventor/CoinRenderGeneratedImage.h"
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <vector>
int main() {
  SoDB::init();
  SoSFImage image;
  const unsigned char color[] = {80,140,200,128};
  {
    std::vector<unsigned char> input(3*5*4);
    for (size_t i=0;i<input.size();++i) input[i]=color[i%4];
    Gui::setGeneratedTextureImage(image,SbVec2s(3,5),4,input.data());
  }
  SbVec2s size; int components;
  const auto * bytes=image.getValue(size,components);
  if(size!=SbVec2s(4,8)||components!=4||!bytes) return 1;
  for(size_t i=0;i<4*8*4;++i)
    if(std::abs(int(bytes[i])-int(color[i%4]))>1) return 1;
  // Existing POT payloads must remain bit-identical, including transparent
  // colors; resampling/premultiplication would destroy that identity.
  std::vector<unsigned char> pot(4*8*4);
  for(size_t i=0;i<pot.size();++i) pot[i]=static_cast<unsigned char>(i*37);
  Gui::setGeneratedTextureImage(image,SbVec2s(4,8),4,pot.data());
  bytes=image.getValue(size,components);
  if(size!=SbVec2s(4,8)||!std::equal(pot.begin(),pot.end(),bytes)) return 1;
  std::vector<unsigned char> rgb(3*5*3,91);
  Gui::setGeneratedTextureImage(image,SbVec2s(3,5),3,rgb.data());
  bytes=image.getValue(size,components);
  if(size!=SbVec2s(3,5)||components!=3||!std::equal(rgb.begin(),rgb.end(),bytes)) return 1;
  Gui::setGeneratedTextureImage(image,SbVec2s(0,0),0,nullptr);
  image.getValue(size,components);
  if(size!=SbVec2s(0,0)) return 1;
  std::cout << "FreeCAD generated image: POT identity, NPOT RGBA alpha/color, ownership, format and empty controls passed\n";
}
