#include "rendering/coinrender/CoinRenderFramePlan.h"
#include "rendering/coinrender/CoinRenderShaderModuleCore.h"
#include <Inventor/nodes/SoShaderProgram.h>
#include <Inventor/nodes/SoShaderObject.h>
#include <Inventor/nodes/SoShaderParameter.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <iomanip>
#include <limits>
#include <locale>
#include <map>
#include <sstream>

namespace {
unsigned width(CoinRenderShaderValueType t) { return static_cast<unsigned>(t); }
bool validType(CoinRenderShaderValueType t) { return width(t)>=1 && width(t)<=4; }
std::string name(const SbString &s) { return std::string(s.getString(), size_t(s.getLength())); }
unsigned arity(CoinRenderShaderOp op) {
  switch(op) {
  case CoinRenderShaderOp::ADD: case CoinRenderShaderOp::SUBTRACT:
  case CoinRenderShaderOp::MULTIPLY: case CoinRenderShaderOp::DIVIDE:
  case CoinRenderShaderOp::MINIMUM: case CoinRenderShaderOp::MAXIMUM:
  case CoinRenderShaderOp::DOT: return 2;
  case CoinRenderShaderOp::CLAMP: return 3;
  case CoinRenderShaderOp::SAMPLE_2D: return 1;
  default: return 0;
  }
}
struct Known {
  std::array<float,4> value = {{0,0,0,0}};
  std::array<bool,4> known = {{false,false,false,false}};
};
}

bool coin_render_shader_ir(const CoinRenderShaderModule &module,
                            CoinRenderShaderIr &output, std::string &error)
{
  error.clear();
  const auto fail=[&](const std::string &message) { error="Portable shader module: "+message;return false; };
  if(module.version!=1) return fail("unsupported version");
  if(module.expressions.empty() || module.expressions.size()>256)
    return fail("expression count must be in 1..256");
  if(module.output>=module.expressions.size() ||
     module.expressions[module.output].type!=CoinRenderShaderValueType::FLOAT4)
    return fail("output must name a FLOAT4 expression");
  CoinRenderShaderIr ir;
  ir.alphaMayVary=module.alphaMayVary!=FALSE;
  std::map<std::string,std::pair<CoinRenderShaderValueType,uint32_t>> uniforms;
  for(const auto &uniform:module.uniforms) {
    if(!validType(uniform.type) || uniform.name.getLength()==0)
      return fail("invalid uniform name or type");
    if(ir.uniformFloats>1024-width(uniform.type)) return fail("uniform signature exceeds 4 KiB");
    const auto inserted=uniforms.emplace(name(uniform.name),std::make_pair(uniform.type,ir.uniformFloats));
    if(!inserted.second) return fail("duplicate uniform declaration");
    ir.uniformOffsets.push_back(ir.uniformFloats);
    ir.uniformFloats+=width(uniform.type);
  }
  std::vector<uint8_t> marks(module.expressions.size(),0);
  std::vector<uint32_t> remap(module.expressions.size(),UINT32_MAX),depth(module.expressions.size(),0);
  std::vector<Known> known;
  unsigned samples=0;
  std::function<bool(uint32_t)> visit;
  visit=[&](uint32_t index) {
    if(index>=module.expressions.size()) return fail("input index is out of range");
    if(marks[index]==1) return fail("cyclic expression graph");
    if(marks[index]==2) return true;
    marks[index]=1;
    const auto &source=module.expressions[index];
    if(!validType(source.type) || static_cast<uint32_t>(source.op)>static_cast<uint32_t>(CoinRenderShaderOp::SAMPLE_2D))
      return fail("invalid operation or value type at expression "+std::to_string(index));
    CoinRenderShaderIrNode node;
    node.op=source.op;node.type=source.type;
    const unsigned n=width(source.type),count=arity(source.op);
    depth[index]=1;
    for(unsigned input=0;input<count;++input) {
      if(!visit(source.inputs[input])) return false;
      node.inputs[input]=remap[source.inputs[input]];
      depth[index]=std::max(depth[index],depth[source.inputs[input]]+1);
    }
    if(depth[index]>32) return fail("expression depth exceeds 32");
    const auto inputType=[&](unsigned input){return module.expressions[source.inputs[input]].type;};
    const auto broadcast=[&](unsigned input){return inputType(input)==source.type || inputType(input)==CoinRenderShaderValueType::FLOAT1;};
    Known result;
    switch(source.op) {
    case CoinRenderShaderOp::CONSTANT:
      for(unsigned c=0;c<n;++c) {
        if(!std::isfinite(source.value[c])) return fail("nonfinite constant");
        node.value[c]=source.value[c];result.value[c]=source.value[c];result.known[c]=true;
      }
      break;
    case CoinRenderShaderOp::PRIMARY_COLOR:
      if(n!=4) return fail("PRIMARY_COLOR must be FLOAT4");
      break;
    case CoinRenderShaderOp::TEXCOORD:
      if(n!=4 || source.textureUnit>=8) return fail("invalid TEXCOORD type or unit");
      node.resource=source.textureUnit;
      ir.coordinateMask |= 1u << source.textureUnit;
      break;
    case CoinRenderShaderOp::UNIFORM: {
      const auto found=uniforms.find(name(source.uniformName));
      if(found==uniforms.end() || found->second.first!=source.type)
        return fail("uniform expression does not match its signature");
      node.resource=found->second.second;
      break;
    }
    case CoinRenderShaderOp::SAMPLE_2D:
      if(n!=4 || inputType(0)!=CoinRenderShaderValueType::FLOAT4 || source.textureUnit>=8)
        return fail("invalid SAMPLE_2D type, coordinate or unit");
      if(++samples>8) return fail("more than eight SAMPLE_2D operations");
      node.resource=source.textureUnit;ir.textureMask|=1u<<source.textureUnit;
      break;
    case CoinRenderShaderOp::DOT:
      if(n!=1 || width(inputType(0))<2 || inputType(0)!=inputType(1))
        return fail("DOT requires equal vector inputs and FLOAT1 output");
      result.known[0]=true;
      for(unsigned c=0;c<width(inputType(0));++c) {
        result.known[0]=result.known[0] && known[node.inputs[0]].known[c] && known[node.inputs[1]].known[c];
        result.value[0]+=known[node.inputs[0]].value[c]*known[node.inputs[1]].value[c];
      }
      break;
    case CoinRenderShaderOp::CLAMP:
      if(inputType(0)!=source.type || !broadcast(1) || !broadcast(2))
        return fail("CLAMP input types do not match output");
      break;
    case CoinRenderShaderOp::DIVIDE: {
      const auto &denominator=module.expressions[source.inputs[1]];
      if(inputType(0)!=source.type || inputType(1)!=CoinRenderShaderValueType::FLOAT1 ||
         (denominator.op!=CoinRenderShaderOp::CONSTANT && denominator.op!=CoinRenderShaderOp::UNIFORM))
        return fail("DIVIDE denominator must be a scalar CONSTANT or UNIFORM");
      if(denominator.op==CoinRenderShaderOp::CONSTANT && denominator.value[0]==0)
        return fail("zero DIVIDE denominator");
      break;
    }
    default:
      if(!broadcast(0) || !broadcast(1) ||
         (inputType(0)==CoinRenderShaderValueType::FLOAT1 && inputType(1)==CoinRenderShaderValueType::FLOAT1 && n!=1))
        return fail("binary operation has incompatible types");
      break;
    }
    if(count && source.op!=CoinRenderShaderOp::SAMPLE_2D && source.op!=CoinRenderShaderOp::DOT) {
      for(unsigned c=0;c<n;++c) {
        float v[3]={};bool k[3]={};
        for(unsigned j=0;j<count;++j) {
          const unsigned component=width(inputType(j))==1?0:c;
          v[j]=known[node.inputs[j]].value[component];k[j]=known[node.inputs[j]].known[component];
        }
        result.known[c]=k[0]&&k[1]&&(count<3||k[2]);
        if(source.op==CoinRenderShaderOp::CLAMP && k[1] && k[2]) {
          if(v[1]>=v[2]) {result.known[c]=true;result.value[c]=v[2];continue;}
        }
        if(!result.known[c]) continue;
        switch(source.op) {
        case CoinRenderShaderOp::ADD: result.value[c]=v[0]+v[1];break;
        case CoinRenderShaderOp::SUBTRACT: result.value[c]=v[0]-v[1];break;
        case CoinRenderShaderOp::MULTIPLY: result.value[c]=v[0]*v[1];break;
        case CoinRenderShaderOp::DIVIDE: result.value[c]=v[0]/v[1];break;
        case CoinRenderShaderOp::MINIMUM: result.value[c]=std::min(v[0],v[1]);break;
        case CoinRenderShaderOp::MAXIMUM: result.value[c]=std::max(v[0],v[1]);break;
        case CoinRenderShaderOp::CLAMP: result.value[c]=std::min(std::max(v[0],v[1]),v[2]);break;
        default: break;
        }
      }
    }
    for(unsigned c=0;c<n;++c) if(result.known[c] && !std::isfinite(result.value[c]))
      return fail("constant operation overflows at expression "+std::to_string(index));
    remap[index]=uint32_t(ir.nodes.size());ir.nodes.push_back(node);known.push_back(result);marks[index]=2;
    return true;
  };
  // Visit all statements, including unreachable ones. Forward references are
  // ordinary edges; they neither change semantics nor bypass validation.
  for(uint32_t index=0;index<module.expressions.size();++index) if(!visit(index)) return false;
  ir.output=remap[module.output];
  if(!ir.alphaMayVary && (!known[ir.output].known[3] || known[ir.output].value[3]!=1.0f))
    return fail("alphaMayVary FALSE requires structural alpha one");
  output=std::move(ir);return true;
}

SbBool coin_render_validate_shader_module(const CoinRenderShaderModule &module, SbString &error)
{
  CoinRenderShaderIr ir;std::string diagnostic;
  const bool valid=coin_render_shader_ir(module,ir,diagnostic);
  error=diagnostic.c_str();return valid?TRUE:FALSE;
}

namespace {
std::string typeName(unsigned n,bool wgsl) {
  return n==1?(wgsl?"f32":"float"):(wgsl?"vec"+std::to_string(n)+"<f32>":"vec"+std::to_string(n));
}
std::string literal(float value,bool wgsl) {
  uint32_t bits;std::memcpy(&bits,&value,sizeof(bits));
  std::ostringstream out;out.imbue(std::locale::classic());
  out<<(wgsl?"bitcast<f32>(0x":"uintBitsToFloat(0x")<<std::hex<<bits<<"u)";
  return out.str();
}
}

std::string coin_render_shader_function(const CoinRenderShaderIr &ir,bool wgsl)
{
  std::ostringstream out;out.imbue(std::locale::classic());
  out<<(wgsl?"fn coinPortableModule(primary:vec4<f32>":"vec4 coinPortableModule(vec4 primary");
  for(unsigned unit=0;unit<8;++unit)
    out<<(wgsl?",tc":",vec4 tc")<<unit<<(wgsl?":vec4<f32>":"");
  out<<(wgsl?")->vec4<f32>{\n":"){\n");
  for(size_t index=0;index<ir.nodes.size();++index) {
    const auto &node=ir.nodes[index];const unsigned n=width(node.type);
    const std::string type=typeName(n,wgsl);
    const auto arg=[&](unsigned input) {
      const auto id=node.inputs[input];std::string text="e"+std::to_string(id);
      if(width(ir.nodes[id].type)==1 && n>1) text=type+"("+text+")";
      return text;
    };
    out<<(wgsl?"let ":type+" ")<<"e"<<index<<(wgsl?":"+type:"")<<"=";
    switch(node.op) {
    case CoinRenderShaderOp::CONSTANT:
      if(n>1) out<<type<<"(";
      for(unsigned c=0;c<n;++c) {if(c)out<<",";out<<literal(node.value[c],wgsl);}
      if(n>1)out<<")";
      break;
    case CoinRenderShaderOp::PRIMARY_COLOR: out<<"primary";break;
    case CoinRenderShaderOp::TEXCOORD: out<<"tc"<<node.resource;break;
    case CoinRenderShaderOp::UNIFORM:
      if(n>1)out<<type<<"(";
      for(unsigned c=0;c<n;++c) {
        if(c)out<<",";const uint32_t offset=node.resource+c;
        out<<"coinPortableUniform("<<offset<<(wgsl?"u)":")");
      }
      if(n>1)out<<")";
      break;
    case CoinRenderShaderOp::SAMPLE_2D:
      out<<"coinPortableSample"<<node.resource<<"(e"<<node.inputs[0]<<")";break;
    case CoinRenderShaderOp::ADD: out<<arg(0)<<"+"<<arg(1);break;
    case CoinRenderShaderOp::SUBTRACT: out<<arg(0)<<"-"<<arg(1);break;
    case CoinRenderShaderOp::MULTIPLY: out<<arg(0)<<"*"<<arg(1);break;
    case CoinRenderShaderOp::DIVIDE: out<<arg(0)<<"/e"<<node.inputs[1];break;
    case CoinRenderShaderOp::MINIMUM: out<<"min("<<arg(0)<<","<<arg(1)<<")";break;
    case CoinRenderShaderOp::MAXIMUM: out<<"max("<<arg(0)<<","<<arg(1)<<")";break;
    case CoinRenderShaderOp::CLAMP: out<<"min(max("<<arg(0)<<","<<arg(1)<<"),"<<arg(2)<<")";break;
    case CoinRenderShaderOp::DOT: out<<"dot(e"<<node.inputs[0]<<",e"<<node.inputs[1]<<")";break;
    }
    out<<";\n";
  }
  out<<"return e"<<ir.output<<";\n}\n";return out.str();
}


bool coin_render_shader_active(const SoShaderProgram *program, bool &active, std::string &error)
{
  if (!program) { error = "Portable shader program is absent"; return false; }
  bool enabled = false;
  const auto &objects = program->shaderObject;
  for (int index = 0; index < objects.getNum(); ++index) {
    const SoNode *node = objects[index];
    if (!node || !node->isOfType(SoShaderObject::getClassTypeId())) {
      error = "Portable shader contains a non-shader object";
      return false;
    }
    enabled = enabled || static_cast<const SoShaderObject *>(node)->isActive.getValue() != FALSE;
  }
  active = enabled;
  error.clear();
  return true;
}

bool coin_render_shader_active(const CoinRenderShaderRegistration &registration,
                              bool &active, std::string &error)
{
  return coin_render_shader_active(registration.program.get(), active, error);
}

bool coin_render_shader_parameters(const CoinRenderShaderRegistration &registration,
                                   std::vector<float> &output, std::string &error)
{
  CoinRenderShaderIr ir;
  if(!coin_render_shader_ir(registration.module,ir,error)) return false;
  if(!registration.program) {error="Portable shader parameter source is absent";return false;}
  std::map<std::string,std::pair<CoinRenderShaderValueType,std::array<float,4>>> values;
  const auto &objects=registration.program->shaderObject;
  for(int i=0;i<objects.getNum();++i) {
    const SoNode *node=objects[i];
    if(!node || !node->isOfType(SoShaderObject::getClassTypeId())) {
      error="Portable shader contains a non-shader object";return false;
    }
    const auto *object=static_cast<const SoShaderObject *>(node);
    if(!object->isActive.getValue()) continue;
    for(int j=0;j<object->parameter.getNum();++j) {
      const SoNode *parameter=object->parameter[j];
      if(!parameter || !parameter->isOfType(SoShaderParameter::getClassTypeId())) {
        error="Portable shader contains an invalid parameter";return false;
      }
      const auto *base=static_cast<const SoShaderParameter *>(parameter);
      CoinRenderShaderValueType type;
      std::array<float,4> value={{0,0,0,0}};
      if(parameter->getTypeId()==SoShaderParameter1f::getClassTypeId()) {
        type=CoinRenderShaderValueType::FLOAT1;
        value[0]=static_cast<const SoShaderParameter1f *>(parameter)->value.getValue();
      } else if(parameter->getTypeId()==SoShaderParameter2f::getClassTypeId()) {
        type=CoinRenderShaderValueType::FLOAT2;
        const auto &v=static_cast<const SoShaderParameter2f *>(parameter)->value.getValue();
        for(unsigned c=0;c<2;++c)value[c]=v[c];
      } else if(parameter->getTypeId()==SoShaderParameter3f::getClassTypeId()) {
        type=CoinRenderShaderValueType::FLOAT3;
        const auto &v=static_cast<const SoShaderParameter3f *>(parameter)->value.getValue();
        for(unsigned c=0;c<3;++c)value[c]=v[c];
      } else if(parameter->getTypeId()==SoShaderParameter4f::getClassTypeId()) {
        type=CoinRenderShaderValueType::FLOAT4;
        const auto &v=static_cast<const SoShaderParameter4f *>(parameter)->value.getValue();
        for(unsigned c=0;c<4;++c)value[c]=v[c];
      } else {error="Portable shader parameter type is unsupported";return false;}
      for(unsigned c=0;c<width(type);++c)if(!std::isfinite(value[c])) {
        error="Portable shader parameter is nonfinite";return false;
      }
      if(base->name.getValue().getLength()==0 || !values.emplace(name(base->name.getValue()),std::make_pair(type,value)).second) {
        error="Portable shader parameter name is empty or duplicated";return false;
      }
    }
  }
  if(values.size()!=registration.module.uniforms.size()) {
    error="Portable shader parameter set differs from its uniform signature";return false;
  }
  std::vector<float> packed;packed.reserve(ir.uniformFloats);
  for(const auto &uniform:registration.module.uniforms) {
    const auto found=values.find(name(uniform.name));
    if(found==values.end() || found->second.first!=uniform.type) {
      error="Portable shader parameter is missing or has the wrong type";return false;
    }
    for(unsigned c=0;c<width(uniform.type);++c)packed.push_back(found->second.second[c]);
  }
  for(const auto &node:ir.nodes)if(node.op==CoinRenderShaderOp::DIVIDE) {
    const auto &denominator=ir.nodes[node.inputs[1]];
    if(denominator.op==CoinRenderShaderOp::UNIFORM && packed[denominator.resource]==0) {
      error="Portable shader uniform DIVIDE denominator is zero";return false;
    }
  }
  output=std::move(packed);error.clear();return true;
}


std::string coin_render_shader_key(const CoinRenderShaderModule &module, const CoinRenderShaderIr &ir)
{
  std::string key;
  const auto word = [&](uint32_t value) {
    for (unsigned shift = 0; shift < 32; shift += 8) key.push_back(char(value >> shift));
  };
  word(module.version);
  word(static_cast<uint32_t>(module.uniforms.size()));
  for (const auto &uniform : module.uniforms) {
    word(static_cast<uint32_t>(uniform.type));
    word(static_cast<uint32_t>(uniform.name.getLength()));
    key.append(uniform.name.getString(), static_cast<size_t>(uniform.name.getLength()));
  }
  word(static_cast<uint32_t>(ir.nodes.size()));
  word(ir.output);
  word(ir.uniformFloats);
  word(ir.textureMask);
  word(ir.coordinateMask);
  word(ir.alphaMayVary ? 1u : 0u);
  for (const auto &node : ir.nodes) {
    word(static_cast<uint32_t>(node.op));
    word(static_cast<uint32_t>(node.type));
    for (unsigned input = 0; input < arity(node.op); ++input) word(node.inputs[input]);
    if (node.op == CoinRenderShaderOp::CONSTANT)
      for (unsigned component = 0; component < width(node.type); ++component) {
        uint32_t bits;
        std::memcpy(&bits, &node.value[component], sizeof(bits));
        word(bits);
      }
    if (node.op == CoinRenderShaderOp::UNIFORM || node.op == CoinRenderShaderOp::TEXCOORD ||
        node.op == CoinRenderShaderOp::SAMPLE_2D) word(node.resource);
  }
  return key;
}

bool coin_render_shader_instance_valid(const CoinRenderShaderInstance &instance, std::string &error)
{
  CoinRenderShaderIr expected;
  if (!coin_render_shader_ir(instance.module, expected, error)) return false;
  if (instance.ir.nodes.size() > 256) {
    error = "Portable shader IR expression count is invalid";
    return false;
  }
  for (const auto &node : instance.ir.nodes)
    if (!validType(node.type) || static_cast<uint32_t>(node.op) > static_cast<uint32_t>(CoinRenderShaderOp::SAMPLE_2D)) {
      error = "Portable shader IR type or operation is invalid";
      return false;
    }
  if (coin_render_shader_key(instance.module, expected) != coin_render_shader_key(instance.module, instance.ir) ||
      expected.uniformOffsets != instance.ir.uniformOffsets ||
      instance.parameters.size() != expected.uniformFloats) {
    error = "Portable shader IR or parameter payload differs from its signature";
    return false;
  }
  for (float parameter : instance.parameters) if (!std::isfinite(parameter)) {
    error = "Portable shader parameter payload is nonfinite";
    return false;
  }
  for (const auto &node : expected.nodes) if (node.op == CoinRenderShaderOp::DIVIDE) {
    const auto &denominator = expected.nodes[node.inputs[1]];
    const float value = denominator.op == CoinRenderShaderOp::CONSTANT ? denominator.value[0] :
      instance.parameters[denominator.resource];
    if (value == 0.0f) { error = "Portable shader DIVIDE denominator is zero"; return false; }
  }
  return true;
}

bool coin_render_shader_numeric(const CoinRenderShaderInstance &instance,
  const CoinRenderShaderRange &primary, const std::array<CoinRenderShaderRange,8> &coordinates,
  const std::array<CoinRenderShaderRange,8> &samples, std::string &error, bool projective)
{
  // Convex interpolation keeps every input component within these bounds.
  // Bounds use double only to detect overflow of an individual f32 operation;
  // no bound is substituted for a value sent to a shader.
  const double maximum = static_cast<double>(std::numeric_limits<float>::max());
  const auto finite = [&](double value) { return std::isfinite(value) && std::abs(value) <= maximum; };
  std::vector<CoinRenderShaderRange> values;
  values.reserve(instance.ir.nodes.size());
  for (size_t index = 0; index < instance.ir.nodes.size(); ++index) {
    const auto &node = instance.ir.nodes[index];
    const unsigned count = width(node.type);
    CoinRenderShaderRange result;
    const auto bound = [&](unsigned input, unsigned component, bool upper) {
      const uint32_t operand = node.inputs[input];
      const unsigned c = width(instance.ir.nodes[operand].type) == 1 ? 0 : component;
      return upper ? values[operand].upper[c] : values[operand].lower[c];
    };
    if (node.op == CoinRenderShaderOp::PRIMARY_COLOR) result = primary;
    else if (node.op == CoinRenderShaderOp::TEXCOORD) result = coordinates[node.resource];
    else if (node.op == CoinRenderShaderOp::CONSTANT || node.op == CoinRenderShaderOp::UNIFORM) {
      for (unsigned c = 0; c < count; ++c) {
        const float value = node.op == CoinRenderShaderOp::CONSTANT ? node.value[c] : instance.parameters[node.resource + c];
        result.lower[c] = result.upper[c] = value;
      }
    } else if (node.op == CoinRenderShaderOp::SAMPLE_2D) {
      const auto &coordinate = values[node.inputs[0]];
      if (projective && coordinate.lower[3] <= 0 && coordinate.upper[3] >= 0) {
        error = "Portable shader SAMPLE_2D coordinate Q can be zero";
        return false;
      }
      if (projective && std::min(std::abs(coordinate.lower[3]), std::abs(coordinate.upper[3])) <
          std::numeric_limits<float>::min()) {
        error = "Portable shader projective Q exceeds finite sampling precision";
        return false;
      }
      for (unsigned c = 0; projective && c < 2; ++c) {
        for (double numerator : {coordinate.lower[c], coordinate.upper[c]})
          for (double denominator : {coordinate.lower[3], coordinate.upper[3]})
            if (!finite(numerator / denominator)) {
              error = "Portable shader projected coordinate can overflow";
              return false;
            }
      }
      // Admitted 2D resources have normalized channels; sampling/color-space
      // conversion remains owned by the existing resource and sampler paths.
      result = samples[node.resource];
    } else if (node.op == CoinRenderShaderOp::DOT) {
      double lower = 0, upper = 0;
      for (unsigned c = 0; c < width(instance.ir.nodes[node.inputs[0]].type); ++c) {
        const double products[] = {bound(0,c,false)*bound(1,c,false), bound(0,c,false)*bound(1,c,true),
          bound(0,c,true)*bound(1,c,false), bound(0,c,true)*bound(1,c,true)};
        const double lo = *std::min_element(products, products + 4);
        const double hi = *std::max_element(products, products + 4);
        lower += lo; upper += hi;
        if (!finite(lo) || !finite(hi) || !finite(lower) || !finite(upper)) {
          error = "Portable shader DOT operation can overflow";
          return false;
        }
      }
      result.lower[0] = lower; result.upper[0] = upper;
    } else for (unsigned c = 0; c < count; ++c) {
      const double a = bound(0,c,false), b = bound(0,c,true);
      const double lo = bound(1,c,false), hi = bound(1,c,true);
      switch (node.op) {
      case CoinRenderShaderOp::ADD: result.lower[c] = a + lo; result.upper[c] = b + hi; break;
      case CoinRenderShaderOp::SUBTRACT: result.lower[c] = a - hi; result.upper[c] = b - lo; break;
      case CoinRenderShaderOp::MULTIPLY: case CoinRenderShaderOp::DIVIDE: {
        if (node.op == CoinRenderShaderOp::DIVIDE && lo <= 0 && hi >= 0) {
          error = "Portable shader DIVIDE denominator can be zero";
          return false;
        }
        const bool divide = node.op == CoinRenderShaderOp::DIVIDE;
        const double candidates[] = {divide ? a / lo : a * lo, divide ? a / hi : a * hi,
          divide ? b / lo : b * lo, divide ? b / hi : b * hi};
        result.lower[c] = *std::min_element(candidates, candidates + 4);
        result.upper[c] = *std::max_element(candidates, candidates + 4);
        break;
      }
      case CoinRenderShaderOp::MINIMUM: result.lower[c] = std::min(a,lo); result.upper[c] = std::min(b,hi); break;
      case CoinRenderShaderOp::MAXIMUM: result.lower[c] = std::max(a,lo); result.upper[c] = std::max(b,hi); break;
      case CoinRenderShaderOp::CLAMP:
        result.lower[c] = std::min(std::max(a,lo), bound(2,c,false));
        result.upper[c] = std::min(std::max(b,hi), bound(2,c,true));
        break;
      default: error = "Portable shader numeric operation is invalid"; return false;
      }
    }
    for (unsigned c = 0; c < count; ++c) if (!finite(result.lower[c]) || !finite(result.upper[c]) || result.lower[c] > result.upper[c]) {
      error = "Portable shader operation " + std::to_string(index) + " can produce a nonfinite f32";
      return false;
    }
    // Enclose f32 rounding of each intermediate before using its bounds in
    // a later operation. Exact representable endpoints remain unchanged.
    for (unsigned c = 0; c < count; ++c) {
      const float lo = static_cast<float>(result.lower[c]);
      const float hi = static_cast<float>(result.upper[c]);
      result.lower[c] = static_cast<double>(lo) > result.lower[c] ?
        std::nextafter(lo, -std::numeric_limits<float>::infinity()) : lo;
      result.upper[c] = static_cast<double>(hi) < result.upper[c] ?
        std::nextafter(hi, std::numeric_limits<float>::infinity()) : hi;
    }
    values.push_back(result);
  }
  error.clear();
  return true;
}


bool coin_render_shader_decode(const uint8_t *bytes, size_t length, CoinRenderShaderModule &module,
                              CoinRenderShaderIr &ir, std::string &error)
{
  // This decoder is the private typed transport adapter, not a source parser.
  // Its result goes back through the same authorial validator and exact key.
  size_t cursor = 0;
  const auto fail = [&]() { error = "Malformed portable shader Core signature"; return false; };
  const auto word = [&](uint32_t &value) {
    if (!bytes || cursor > length || length - cursor < 4) return false;
    value = 0;
    for (unsigned shift = 0; shift < 32; shift += 8) value |= uint32_t(bytes[cursor++]) << shift;
    return true;
  };
  CoinRenderShaderModule decoded;
  uint32_t count;
  if (!word(decoded.version) || !word(count) || count > 1024) return fail();
  std::map<uint32_t, SbString> uniformNames;
  uint32_t offset = 0;
  for (uint32_t i = 0; i < count; ++i) {
    uint32_t type, size;
    if (!word(type) || type < 1 || type > 4 || !word(size) || !size ||
        size > uint32_t(std::numeric_limits<int>::max()) || cursor > length || size > length - cursor ||
        offset > 1024 - type) return fail();
    CoinRenderShaderUniform uniform;
    uniform.type = static_cast<CoinRenderShaderValueType>(type);
    std::string text(reinterpret_cast<const char *>(bytes + cursor), size);
    uniform.name = SbString(text.c_str(), 0, static_cast<int>(size) - 1);
    cursor += size;
    uniformNames.emplace(offset, uniform.name);
    decoded.uniforms.push_back(uniform);
    offset += type;
  }
  uint32_t floats, samples, coordinates, alpha;
  if (!word(count) || !count || count > 256 || !word(decoded.output) || !word(floats) ||
      floats != offset || !word(samples) || samples > 255 || !word(coordinates) || coordinates > 255 ||
      !word(alpha) || alpha > 1) return fail();
  decoded.alphaMayVary = alpha ? TRUE : FALSE;
  for (uint32_t i = 0; i < count; ++i) {
    CoinRenderShaderExpression expression;
    uint32_t op, type;
    if (!word(op) || op > static_cast<uint32_t>(CoinRenderShaderOp::SAMPLE_2D) ||
        !word(type) || type < 1 || type > 4) return fail();
    expression.op = static_cast<CoinRenderShaderOp>(op);
    expression.type = static_cast<CoinRenderShaderValueType>(type);
    for (unsigned input = 0; input < arity(expression.op); ++input)
      if (!word(expression.inputs[input]) || expression.inputs[input] >= i) return fail();
    if (expression.op == CoinRenderShaderOp::CONSTANT)
      for (unsigned c = 0; c < type; ++c) {
        uint32_t bits;
        if (!word(bits)) return fail();
        std::memcpy(&expression.value[c], &bits, sizeof(bits));
      }
    if (expression.op == CoinRenderShaderOp::UNIFORM) {
      uint32_t resource;
      if (!word(resource)) return fail();
      const auto found = uniformNames.find(resource);
      if (found == uniformNames.end()) return fail();
      expression.uniformName = found->second;
    } else if (expression.op == CoinRenderShaderOp::TEXCOORD || expression.op == CoinRenderShaderOp::SAMPLE_2D) {
      if (!word(expression.textureUnit)) return fail();
    }
    decoded.expressions.push_back(expression);
  }
  if (cursor != length) return fail();
  CoinRenderShaderIr canonical;
  if (!coin_render_shader_ir(decoded, canonical, error)) return false;
  const std::string key = coin_render_shader_key(decoded, canonical);
  if (key.size() != length || std::memcmp(key.data(), bytes, length)) return fail();
  module = std::move(decoded);
  ir = std::move(canonical);
  error.clear();
  return true;
}

bool coin_render_shader_execution_profile(const CoinRenderFramePlan &frame, std::string &error)
{
  if (!frame.hasPortableShaderDraws()) return true;
  std::vector<bool> bump(frame.vertices.size(), false);
  for (const auto &coordinate : frame.bumpCoords)
    if (coordinate.imageSlot && coordinate.vertexIndex < bump.size()) bump[coordinate.vertexIndex] = true;
  for (const auto &draw : frame.draws) {
    if (draw.renderStateSlot >= frame.renderStates.size() ||
        !frame.renderStates[draw.renderStateSlot].portableShaderSlot) continue;
    const auto &geometry = draw.geometry;
    if (geometry.firstIndex > frame.indices.size() || geometry.indexCount > frame.indices.size() - geometry.firstIndex) continue;
    for (size_t index = geometry.firstIndex; index < size_t(geometry.firstIndex) + geometry.indexCount; ++index)
      if (frame.indices[index] < bump.size() && bump[frame.indices[index]]) {
        error = "Portable shader modules do not yet adapt active bump receiver attributes";
        return false;
      }
  }
  return true;
}
