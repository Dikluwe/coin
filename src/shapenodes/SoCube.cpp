/**************************************************************************\
 * Copyright (c) Kongsberg Oil & Gas Technologies AS
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 * 
 * Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 * 
 * Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 * 
 * Neither the name of the copyright holder nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
\**************************************************************************/

// Note: the class documentation for the basic primitive shapes
// SoSphere, SoCylinder, SoCone and SoCube have many common, or at
// least close to common, paragraphs. If you make any changes, check
// those other shapes too, to see if your updates / fixes should be
// migrated. <mortene@sim.no>.
/*!
  \class SoCube SoCube.h Inventor/nodes/SoCube.h
  \brief The SoCube class is for rendering cubes.

  \ingroup coin_nodes

  Insert a cube shape into the scene graph. The cube is rendered with
  the current material, texture and draw style settings (if any,
  otherwise the default settings are used).

  (Strictly speaking, as you can have different width, height and
  depth values for the "cube", instances of this class actually
  represents \e boxes.)

  The SoCube node class is provided as a convenient abstraction for
  the application programmer to use "complex" shapes of this type
  without having to do the calculation and book-keeping of the polygon
  sides and other low level programming herself.

  <b>FILE FORMAT/DEFAULTS:</b>
  \code
    Cube {
        width 2
        height 2
        depth 2
    }
  \endcode

  \sa SoCylinder, SoSphere, SoCone
*/

#include <Inventor/nodes/SoCube.h>
#include <Inventor/actions/SoIRRenderAction.h>
#include "elements/SoPBRMaterialElement.h"
#include <Inventor/elements/SoBumpMapElement.h>
#include "rendering/SoRenderIRP.h"
#include "coindefs.h"

#include <Inventor/SbPlane.h>
#include <Inventor/SoPickedPoint.h>
#include <Inventor/actions/SoGLRenderAction.h>
#include <Inventor/actions/SoGetPrimitiveCountAction.h>
#include <Inventor/actions/SoRayPickAction.h>
#include <Inventor/bundles/SoMaterialBundle.h>
#include <Inventor/details/SoCubeDetail.h>
#include <Inventor/elements/SoDrawStyleElement.h>
#include <Inventor/elements/SoGLMultiTextureEnabledElement.h>
#include <Inventor/elements/SoMaterialBindingElement.h>
#include <Inventor/elements/SoMultiTextureCoordinateElement.h>
#include <Inventor/misc/SoState.h>

#include "nodes/SoSubNodeP.h"
#include "rendering/SoGL.h"
#include "misc/SoGenerate.h"
#include "misc/SoPick.h"

/*!
  \var SoSFFloat SoCube::width
  X-axis dimension of cube, defaults to 2.0.
*/
/*!
  \var SoSFFloat SoCube::height
  Y-axis dimension of cube, defaults to 2.0.
*/
/*!
  \var SoSFFloat SoCube::depth
  Z-axis dimension of cube, defaults to 2.0.
*/


// *************************************************************************

SO_NODE_SOURCE(SoCube);

/*!
  Constructor.
*/
SoCube::SoCube(void)
{
  SO_NODE_INTERNAL_CONSTRUCTOR(SoCube);

  SO_NODE_ADD_FIELD(width, (2.0f));
  SO_NODE_ADD_FIELD(height, (2.0f));
  SO_NODE_ADD_FIELD(depth, (2.0f));
}

/*!
  Destructor.
*/
SoCube::~SoCube()
{
}

/*!
  \copydetails SoNode::initClass(void)
*/
void
SoCube::initClass(void)
{
  SO_NODE_INTERNAL_INIT_CLASS(SoCube, SO_FROM_INVENTOR_1|SoNode::VRML1);
}

// Doc in parent.
void
SoCube::GLRender(SoGLRenderAction * action)
{
  if (!this->shouldGLRender(action)) return;
  SoState * state = action->getState();

  SoMaterialBindingElement::Binding binding =
    SoMaterialBindingElement::get(state);

  SbBool materialPerPart =
    (binding == SoMaterialBindingElement::PER_PART ||
     binding == SoMaterialBindingElement::PER_PART_INDEXED ||
     binding == SoMaterialBindingElement::PER_FACE ||
     binding == SoMaterialBindingElement::PER_FACE_INDEXED);

  SbBool doTextures = SoGLMultiTextureEnabledElement::get(state, 0);

  SoMaterialBundle mb(action);
  mb.sendFirst();

  SbBool sendNormals = !mb.isColorOnly() ||
    (SoMultiTextureCoordinateElement::getType(state) == SoMultiTextureCoordinateElement::FUNCTION);

  unsigned int flags = 0;
  if (materialPerPart) flags |= SOGL_MATERIAL_PER_PART;
  if (doTextures) {
    switch (SoMultiTextureEnabledElement::getMode(state, 0)) {
    default:
      flags |= SOGL_NEED_TEXCOORDS;
      break;
    case SoMultiTextureEnabledElement::CUBEMAP:
      flags |= SOGL_NEED_CUBEMAPTEXCOORDS;
      break;
    case SoMultiTextureEnabledElement::TEXTURE3D:
      flags |= SOGL_NEED_3DTEXCOORDS;
      break;
    }
  }
  if (sendNormals) flags |= SOGL_NEED_NORMALS;

  sogl_render_cube(width.getValue(),
                   height.getValue(),
                   depth.getValue(),
                   &mb,
                   flags, state);
}

// Doc in parent.
void
SoCube::generatePrimitives(SoAction * action)
{
  SoMaterialBindingElement::Binding binding =
    SoMaterialBindingElement::get(action->getState());

  SbBool materialPerPart =
    (binding == SoMaterialBindingElement::PER_PART ||
     binding == SoMaterialBindingElement::PER_PART_INDEXED ||
     binding == SoMaterialBindingElement::PER_FACE ||
     binding == SoMaterialBindingElement::PER_FACE_INDEXED);

  unsigned int flags = 0;
  if (materialPerPart) flags |= SOGEN_MATERIAL_PER_PART;
  sogen_generate_cube(this->width.getValue(),
                      this->height.getValue(),
                      this->depth.getValue(),
                      flags,
                      this,
                      action);
}

// Doc in parent.
void
SoCube::computeBBox(SoAction * COIN_UNUSED_ARG(action), SbBox3f & box, SbVec3f & center)
{
  center.setValue(0.0f, 0.0f, 0.0f);
  float w, h, d;
  this->getHalfSize(w, h, d);

  // Allow negative values.
  if (w < 0.0f) w = -w;
  if (h < 0.0f) h = -h;
  if (d < 0.0f) d = -d;

  box.setBounds(-w, -h, -d, w, h, d);
}

// Doc in parent.
void
SoCube::rayPick(SoRayPickAction * action)
{
  if (!shouldRayPick(action)) return;

  SoMaterialBindingElement::Binding binding =
    SoMaterialBindingElement::get(action->getState());

  SbBool materialPerPart =
    (binding == SoMaterialBindingElement::PER_PART ||
     binding == SoMaterialBindingElement::PER_PART_INDEXED);

  sopick_pick_cube(this->width.getValue(),
                   this->height.getValue(),
                   this->depth.getValue(),
                   materialPerPart ? SOPICK_MATERIAL_PER_PART : 0,
                   this, action);
}

// Convenience function for finding half the size of the box.
void
SoCube::getHalfSize(float & w, float & h, float & d)
{
  w = (this->width.isIgnored() ? 1.0f :
       this->width.getValue() * 0.5f);
  h = (this->height.isIgnored() ? 1.0f :
       this->height.getValue() * 0.5f);
  d = (this->depth.isIgnored() ? 1.0f :
       this->depth.getValue() * 0.5f);
}

// Doc from parent.
void
SoCube::getPrimitiveCount(SoGetPrimitiveCountAction * action)
{
  if (!this->shouldPrimitiveCount(action)) return;

  action->addNumTriangles(12);
}

SbBool SoCube::generateRetainedPrimitives(SoIRRenderAction * action)
{
  const auto * pbr=SoPBRMaterialElement::get(action->getState());
  if(pbr && pbr->model==SoRenderMaterialModel::PBR &&
     (pbr->material.anisotropy>0 || pbr->material.baseColorTextureUnit>=0 || pbr->material.ormTextureUnit>=0 || pbr->material.emissiveTextureUnit>=0 ||
      SoBumpMapElement::getSource(action->getState())))return FALSE;

  if (SoMaterialBindingElement::get(action->getState()) != SoMaterialBindingElement::OVERALL)
    return FALSE;
  // Match generate_cube's face corners, normals and UVs, retaining its triangle
  // order while indexing the four distinct vertices of each face. A million
  // instances must not run 36 vertex invocations where 24 suffice.
  const uint64_t key = reinterpret_cast<uintptr_t>(this);
  const uint64_t revision = this->getNodeId();
  SoGeometryDesc geometry;
  const auto handle = action->findGeometrySource(key, revision);
  const auto * resource = action->getDrawList().getGeometryResource(handle);
  if (resource) geometry = resource->geometry;
  else {
    static const int corners[24] = {0,1,3,2, 5,4,6,7, 1,5,7,3,
                                    4,0,2,6, 4,5,1,0, 2,3,7,6};
    static const float normals[6][3] = {{0,0,1},{0,0,-1},{-1,0,0},
                                       {1,0,0},{0,1,0},{0,-1,0}};
    static const float uv[4][2] = {{1,1},{0,1},{0,0},{1,0}};
    static const uint32_t triangles[6] = {0,1,2,0,2,3};
    const float half[3] = {width.getValue()*.5f,height.getValue()*.5f,depth.getValue()*.5f};
    auto * positions = static_cast<float *>(action->allocateGeometryStorage(24*3*sizeof(float)));
    auto * vertexNormals = static_cast<float *>(action->allocateGeometryStorage(24*3*sizeof(float)));
    auto * texcoords = static_cast<float *>(action->allocateGeometryStorage(24*4*sizeof(float)));
    auto * indices = static_cast<uint32_t *>(action->allocateGeometryStorage(36*sizeof(uint32_t)));
    for (int face=0; face<6; ++face) {
      for (int v=0; v<4; ++v) {
        const int i=face*4+v, corner=corners[i];
        for (int axis=0; axis<3; ++axis) {
          positions[i*3+axis]=(corner & (1<<axis)) ? -half[axis] : half[axis];
          vertexNormals[i*3+axis]=normals[face][axis];
        }
        texcoords[i*4]=uv[v][0]; texcoords[i*4+1]=uv[v][1];
        texcoords[i*4+2]=0; texcoords[i*4+3]=1;
      }
      for (int t=0; t<6; ++t) indices[face*6+t]=face*4+triangles[t];
    }
    geometry.vertexCount=geometry.normalCount=24; geometry.indexCount=36;
    geometry.positions=positions; geometry.normals=vertexNormals;
    geometry.texcoords=texcoords; geometry.indices=indices;
    geometry.vertexStride=3*sizeof(float); geometry.texcoordStride=4*sizeof(float);
    geometry.cacheKey=key; geometry.revision=revision;
  }
  SoRenderCommand command;
  command.geometry=geometry;
  SoRenderIR::fillCommandStateFromAction(action,command,0);
  SoRenderIR::finalizeCommand(command);
  if (resource) command.pick.useResourceElementRanges=true;
  else for (int face=0; face<6; ++face) {
    SoRenderElementRange range; range.type=SO_PICK_FACE; range.elementIndex=face;
    range.drawStart=face*6; range.drawCount=6;
    command.pick.elementRanges.push_back(range);
  }
  action->applyRenderStage(command); command.userData=this;
  action->addCommand(std::move(command));
  return TRUE;
}
