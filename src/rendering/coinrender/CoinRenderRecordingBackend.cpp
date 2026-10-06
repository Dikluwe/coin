#include "rendering/coinrender/CoinRenderRecordingBackend.h"
#include "rendering/coinrender/CoinRenderComposition.h"

#include <sstream>
#include <iomanip>
#include <cmath>

namespace {

inline float normalizeFloat(float val) {
  if (std::abs(val) < 1e-6f) return 0.0f;
  return val;
}

std::string formatFloat(float val) {
  float n = normalizeFloat(val);
  std::ostringstream ss;
  ss.imbue(std::locale::classic());
  ss << std::fixed << std::setprecision(4) << n;
  return ss.str();
}

std::string formatMatrix(const SbMatrix & m) {
  std::ostringstream ss;
  ss.imbue(std::locale::classic());
  for (int r = 0; r < 4; ++r) {
    for (int c = 0; c < 4; ++c) {
      ss << formatFloat(m[r][c]) << " ";
    }
  }
  return ss.str();
}

const char * depthFunctionName(CoinRenderDepthFunction function) {
  switch (function) {
  case CoinRenderDepthFunction::NEVER: return "NEVER";
  case CoinRenderDepthFunction::ALWAYS: return "ALWAYS";
  case CoinRenderDepthFunction::LESS: return "LESS";
  case CoinRenderDepthFunction::LEQUAL: return "LEQUAL";
  case CoinRenderDepthFunction::EQUAL: return "EQUAL";
  case CoinRenderDepthFunction::GEQUAL: return "GEQUAL";
  case CoinRenderDepthFunction::GREATER: return "GREATER";
  case CoinRenderDepthFunction::NOTEQUAL: return "NOTEQUAL";
  }
  return "UNKNOWN";
}

} // namespace

CoinRenderRecordingBackend::CoinRenderRecordingBackend()
{
}

CoinRenderRecordingBackend::~CoinRenderRecordingBackend()
{
}

CoinRenderBackendStatus
CoinRenderRecordingBackend::getStatus() const
{
  return CoinRenderBackendStatus::SUCCESS;
}

CoinRenderBackendStatus
CoinRenderRecordingBackend::prepare(CoinRenderTargetP & /*target*/)
{
  return CoinRenderBackendStatus::SUCCESS;
}

CoinRenderSubmitResult
CoinRenderRecordingBackend::submit(const CoinRenderFramePlan & frame,
                               CoinRenderTargetP & /*target*/)
{
  this->lastRecordingLog = this->recordToString(frame);
  return CoinRenderSubmitResult(CoinRenderBackendStatus::SUCCESS, "", ++this->submissionSerial);
}

void
CoinRenderRecordingBackend::poll()
{
}

const std::string &
CoinRenderRecordingBackend::getLastError() const
{
  return this->lastError;
}

const std::string &
CoinRenderRecordingBackend::getLastRecordingLog() const
{
  return this->lastRecordingLog;
}

std::string
CoinRenderRecordingBackend::recordToString(const CoinRenderFramePlan & frame) const
{
  std::ostringstream out;
  out.imbue(std::locale::classic());

  out << "schemaVersion: 1.0.0\n";
  out << "clearColor: "
      << formatFloat(frame.clearColor[0]) << " "
      << formatFloat(frame.clearColor[1]) << " "
      << formatFloat(frame.clearColor[2]) << " "
      << formatFloat(frame.clearColor[3]) << "\n";

  out << "materials count: " << frame.materials.size() << "\n";
  for (size_t i = 0; i < frame.materials.size(); ++i) {
    const auto & m = frame.materials[i];
    out << "  material " << i << ":"
        << " amb=[" << formatFloat(m.ambient[0]) << "," << formatFloat(m.ambient[1]) << "," << formatFloat(m.ambient[2]) << "," << formatFloat(m.ambient[3]) << "]"
        << " diff=[" << formatFloat(m.diffuse[0]) << "," << formatFloat(m.diffuse[1]) << "," << formatFloat(m.diffuse[2]) << "," << formatFloat(m.diffuse[3]) << "]"
        << " spec=[" << formatFloat(m.specular[0]) << "," << formatFloat(m.specular[1]) << "," << formatFloat(m.specular[2]) << "," << formatFloat(m.specular[3]) << "]"
        << " emiss=[" << formatFloat(m.emission[0]) << "," << formatFloat(m.emission[1]) << "," << formatFloat(m.emission[2]) << "," << formatFloat(m.emission[3]) << "]"
        << " shin=" << formatFloat(m.shininess)
        << " transp=" << formatFloat(m.transparency) << "\n";
  }

  out << "lightingStates count: " << frame.lightingStates.size() << "\n";
  for (size_t i = 0; i < frame.lightingStates.size(); ++i) {
    const auto & ls = frame.lightingStates[i];
    out << "  lighting " << i << ": lights=" << ls.lights.size()
        << " ambient=" << formatFloat(ls.ambientIntensity)
        << " ambientColor=[" << formatFloat(ls.ambientColor[0]) << "," << formatFloat(ls.ambientColor[1]) << "," << formatFloat(ls.ambientColor[2]) << "]\n";
    for (size_t j = 0; j < ls.lights.size(); ++j) {
      const auto & l = ls.lights[j];
      const char * typeStr = (l.type == CoinRenderLightType::DIRECTIONAL ? "DIR" : (l.type == CoinRenderLightType::POINT ? "POINT" : "SPOT"));
      out << "    light " << j << ": type=" << typeStr
          << " col=[" << formatFloat(l.color[0]) << "," << formatFloat(l.color[1]) << "," << formatFloat(l.color[2]) << "]"
          << " int=" << formatFloat(l.intensity)
          << " dir=[" << formatFloat(l.direction[0]) << "," << formatFloat(l.direction[1]) << "," << formatFloat(l.direction[2]) << "]"
          << " pos=[" << formatFloat(l.position[0]) << "," << formatFloat(l.position[1]) << "," << formatFloat(l.position[2]) << "]"
          << " attenuation=[" << formatFloat(l.attenuation[0]) << "," << formatFloat(l.attenuation[1]) << "," << formatFloat(l.attenuation[2]) << "]"
          << " cutoff=" << formatFloat(l.cutOffAngle)
          << " dropoff=" << formatFloat(l.dropOffRate) << "\n";
    }
  }

  out << "cameras count: " << frame.cameras.size() << "\n";
  for (size_t i = 0; i < frame.cameras.size(); ++i) {
    const auto & c = frame.cameras[i];
    out << "  camera " << i << ":"
        << " isPersp=" << (c.isPerspective ? "1" : "0")
        << " near=" << formatFloat(c.nearDistance)
        << " far=" << formatFloat(c.farDistance)
        << " focal=" << formatFloat(c.focalDistance)
        << " aspect=" << formatFloat(c.aspectRatio) << "\n";
  }

  out << "viewports count: " << frame.viewports.size() << "\n";
  for (size_t i = 0; i < frame.viewports.size(); ++i) {
    const auto & vp = frame.viewports[i];
    out << "  viewport " << i << ": origin=[" << vp.x << "," << vp.y << "] size=[" << vp.width << "," << vp.height << "]\n";
  }

  out << "renderStates count: " << frame.renderStates.size() << "\n";
  for (size_t i = 0; i < frame.renderStates.size(); ++i) {
    const auto & rs = frame.renderStates[i];
    out << "  renderState " << i << ": mat=" << rs.materialSlot << " light=" << rs.lightingSlot << " lightModel=" << (rs.lightModel == CoinRenderLightModel::BASE_COLOR ? "BASE_COLOR" : "PHONG")
        << " cam=" << rs.cameraSlot << " vp=" << rs.viewportSlot
        << " fogMode=" << static_cast<uint32_t>(rs.fogMode)
        << " fogColor=[" << formatFloat(rs.fogColor[0]) << ","
        << formatFloat(rs.fogColor[1]) << "," << formatFloat(rs.fogColor[2]) << "]"
        << " fogRange=[" << formatFloat(rs.fogStart) << ","
        << formatFloat(rs.fogEnd) << "]"
        << " lineWidth=" << formatFloat(rs.lineWidth)
        << " pointSize=" << formatFloat(rs.pointSize)
        << " linePattern=" << rs.linePattern
        << " linePatternScale=" << rs.linePatternScaleFactor
        << " depthTest=" << (rs.depthTest ? "1" : "0")
        << " depthWrite=" << (rs.depthWrite ? "1" : "0")
        << " depthFunction=" << depthFunctionName(rs.depthFunction)
        << " depthRange=[" << formatFloat(rs.depthRange[0]) << ","
        << formatFloat(rs.depthRange[1]) << "]"
        << " polygonOffset=" << (rs.polygonOffsetEnabled ? "1" : "0")
        << " offsetFactor=" << formatFloat(rs.polygonOffsetFactor)
        << " offsetUnits=" << formatFloat(rs.polygonOffsetUnits)
        << " offsetStyles=" << rs.polygonOffsetStyles
        << " offsetPrimitiveStyle=" << rs.polygonOffsetPrimitiveStyle
        << " hasTex=" << (rs.hasTexture ? "1" : "0");
    if (rs.rasterPixels)
      out << " rasterPixels=1 rasterTransparent=" << (rs.rasterTransparent ? "1" : "0")
          << " rasterForceBlend=" << (rs.rasterForceBlend ? "1" : "0");
    if (rs.alphaTestFunction != CoinRenderAlphaTestFunction::NONE)
      out << " alphaTestFunction=" << static_cast<uint32_t>(rs.alphaTestFunction)
          << " alphaTestReference=" << formatFloat(rs.alphaTestReference);
    if (rs.polygonOffsetSlopeBias != 0)
      out << " offsetSlopeBias=" << formatFloat(rs.polygonOffsetSlopeBias);
    if (rs.hasTexture) {
      const char * textureModel = rs.textureModel == CoinRenderTextureModel::MODULATE ? "MODULATE" :
        rs.textureModel == CoinRenderTextureModel::REPLACE ? "REPLACE" :
        rs.textureModel == CoinRenderTextureModel::DECAL ? "DECAL" : "BLEND";
      out << " texSlot=" << rs.textureImageSlot << " sampSlot=" << rs.samplerSlot
          << " texModel=" << textureModel
          << " texBlend=[" << formatFloat(rs.textureBlendColor[0]) << ","
          << formatFloat(rs.textureBlendColor[1]) << ","
          << formatFloat(rs.textureBlendColor[2]) << "]";
    }
    if (!rs.clipPlanesWorld.empty()) {
      out << " clipPlanesWorld=" << rs.clipPlanesWorld.size();
      for (const SbPlane & plane : rs.clipPlanesWorld) {
        const SbVec3f & n = plane.getNormal();
        out << " [" << formatFloat(n[0]) << "," << formatFloat(n[1]) << ","
            << formatFloat(n[2]) << "," << formatFloat(-plane.getDistanceFromOrigin()) << "]";
      }
    }
    out << "\n"
        << "    model: " << formatMatrix(rs.model) << "\n"
        << "    view: " << formatMatrix(rs.view) << "\n"
        << "    projCoin: " << formatMatrix(rs.projectionCoin) << "\n";
    if (rs.hasTexture) {
      out << "    texMat: " << formatMatrix(rs.textureMatrix) << "\n";
    }
    if (rs.textureProjection == CoinRenderTextureProjection::DIRECT_ST)
      out << "    textureProjection: DIRECT_ST\n";
  }

  out << "textures count: " << frame.textures.size() << "\n";
  for (size_t i = 0; i < frame.textures.size(); ++i) {
    const auto & t = frame.textures[i];
    out << "  texture " << i << ": size=[" << t.width << "," << t.height << "] comp=" << t.components
        << " digest=" << t.contentDigest << "\n";
  }

  out << "samplers count: " << frame.samplers.size() << "\n";
  for (size_t i = 0; i < frame.samplers.size(); ++i) {
    const auto & s = frame.samplers[i];
    const char * wsStr = (s.wrapS == CoinRenderTextureWrap::REPEAT ? "REPEAT" : "CLAMP");
    const char * wtStr = (s.wrapT == CoinRenderTextureWrap::REPEAT ? "REPEAT" : "CLAMP");
    const char * filters[]={"NEAREST","LINEAR","NEAREST_MIPMAP_LINEAR","LINEAR_MIPMAP_LINEAR"};
    const char * fStr=static_cast<uint32_t>(s.filter)<4 ? filters[static_cast<uint32_t>(s.filter)] : "INVALID";
    out << "  sampler " << i << ": wrapS=" << wsStr << " wrapT=" << wtStr << " filter=" << fStr << "\n";
  }

  out << "vertices count: " << frame.vertices.size() << "\n";
  for (size_t i = 0; i < frame.vertices.size(); ++i) {
    const auto & v = frame.vertices[i];
    out << "  v " << i << ":"
        << " pos=[" << formatFloat(v.position[0]) << "," << formatFloat(v.position[1]) << "," << formatFloat(v.position[2]) << "]"
        << " norm=[" << formatFloat(v.normal[0]) << "," << formatFloat(v.normal[1]) << "," << formatFloat(v.normal[2]) << "]"
        << " uv=[" << formatFloat(v.texcoord[0]) << "," << formatFloat(v.texcoord[1]) << "]"
        << " matSlot=" << v.materialSlot;
    for (size_t unit = 0; unit < COIN_RENDER_MAX_TEXTURE_UNITS; ++unit)
      if (v.textureR[unit] != 0 || v.textureQ[unit] != 1)
        out << " rq" << unit << "=[" << formatFloat(v.textureR[unit]) << "," << formatFloat(v.textureQ[unit]) << "]";
    out << "\n";
  }

  out << "indices count: " << frame.indices.size() << "\n";
  out << "  indices: ";
  for (size_t i = 0; i < frame.indices.size(); ++i) {
    out << frame.indices[i] << (i + 1 < frame.indices.size() ? " " : "");
  }
  out << "\n";

  out << "draws count: " << frame.draws.size() << "\n";
  for (size_t i = 0; i < frame.draws.size(); ++i) {
    const auto & d = frame.draws[i];
    const char * topStr = (d.topology == CoinRenderPrimitiveTopology::TRIANGLE_LIST ? "TRIANGLES" : (d.topology == CoinRenderPrimitiveTopology::LINE_LIST ? "LINES" : "POINTS"));
    out << "  draw " << i << ": top=" << topStr
        << " rs=" << d.renderStateSlot
        << " fv=" << d.geometry.firstVertex << " vc=" << d.geometry.vertexCount
        << " fi=" << d.geometry.firstIndex << " ic=" << d.geometry.indexCount
        << " ordinal=" << d.frameNodeOrdinal
        << " layer=" << d.renderLayer
        << " clearDepthBefore=" << (d.clearDepthBefore ? 1 : 0) << "\n";
  }
  std::vector<CoinRenderCompositionItem> order;
  std::string compositionError;
  if (coin_render_composition_order(frame, order, compositionError)) {
    if (!order.empty()) {
      out << "composition: transparency\n";
      for (size_t i = 0; i < order.size(); ++i) {
        out << "  submit draw " << order[i].drawIndex
            << " pass=" << (order[i].blend ? "BLEND" : "OPAQUE");
        {
          const CoinRenderDrawPacket & draw = frame.draws[order[i].drawIndex];
          const CoinRenderRenderStateSnapshot & rs = frame.renderStates[draw.renderStateSlot];
          CoinRenderCompositionItem::TransparencyStrategy strategy;
          const char * mapping = NULL;
          if (coin_render_transparency_strategy(rs.transparencyType, strategy, mapping)) {
            out << " strategy=" << mapping;
          }
        }
        out << " deferred=" << order[i].deferred
            << " additive=" << order[i].additive
            << " sortTriangles=" << order[i].sortTriangles
            << " depthTest=" << order[i].depthTest
            << " depthWrite=" << order[i].depthWrite
            << " depthFunction=" << static_cast<uint32_t>(order[i].depthFunction)
            << "\n";
      }
    }
  }

  return out.str();
}
