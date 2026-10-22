// Internal specialization of the established eight-unit/VSM receiver.
// Single-light retains its existing packing, including spatial R coordinates.
// Multiple lights preserve all eight spatial R lanes. A two-component private
// varying carries bump ST; the spare shadow_uv5 YZW carries projected eye.
// Together with sample clip ZW the interface remains within 80 components.
// The public/private wire layout remains unchanged.
pub(super) fn source() -> String { profile(false) }
pub(super) fn eight_source() -> String { profile(true) }
fn profile(eight: bool) -> String {
    let mut s = if eight { super::shadow_receiver::eight_map_multitexture_source() }
                else { super::shadow_receiver::four_map_multitexture_source() };
    s= format!("@group(0) @binding(25) var coin_bump_image:texture_2d<f32>;\n@group(0) @binding(26) var coin_bump_sampler:sampler;\n{s}");
    s=s.replace("output.material_slot = select(input.material_slot, u.flat_shadow_primary.x, u.flat_shadow_primary.y != 0u);", "output.material_slot = input.material_slot;");
    s=s.replacen("struct VertexOutput {", "struct VertexOutput {\n@location(20) coin_bump_coord:vec2<f32>,",1);
    let vs = s.find("fn vs_main(").unwrap();
    let end = vs + s[vs..].find("return output;").unwrap();
    s.insert_str(
        end,
        r#"
    let bm=materials[input.material_slot];
    output.coin_bump_coord=bm.ambient.xy;
    output.diffuse_color=vec4<f32>(bm.diffuse.rgb,1.0);
    output.shadow_vertex0=vec4<f32>(bm.emission.xyz,output.shadow_vertex0.w);
    output.shadow_vertex1=vec4<f32>(bm.params.yzw,output.shadow_vertex1.w);
    output.shadow_vertex2=vec4<f32>(bm.ambient.xyz,output.shadow_vertex2.w);
    output.shadow_vertex3=vec4<f32>(bm.ambient.w,0.0,0.0,output.shadow_vertex3.w);
    if (u.light_meta.x>1.5) {
      let basis_s=bm.emission.xyz;
      let basis_t=bm.params.xyz;
      let basis_n=vec3<f32>(bm.specular.w,bm.emission.w,bm.params.w);
      let inverse_direction=transpose(mat3x3<f32>(u.normal_matrix[0].xyz,
        u.normal_matrix[1].xyz,u.normal_matrix[2].xyz));
      let eye=coin_bump_normalize(inverse_direction*(-u.model_view[3].xyz)-input.position);
      let e=vec3<f32>(dot(basis_s,eye),dot(basis_t,eye),dot(basis_n,eye));
      output.diffuse_color=vec4<f32>(bm.diffuse.rgb,bm.ambient.w);
      output.normal_view=vec4<f32>(bm.specular.rgb,bm.diffuse.w);
      var l0=vec3<f32>(0.0);
      if (u.light_meta.x>0.5) {
        let source=u.lights[0];
        var object_light=inverse_direction*(-source.direction_cutoff.xyz);
        if(source.position_type.w>0.5) {
          object_light=inverse_direction*(source.position_type.xyz-u.model_view[3].xyz)-input.position;
        }
        let direction=coin_bump_normalize(object_light);
        l0=vec3<f32>(dot(basis_s,direction),dot(basis_t,direction),dot(basis_n,direction));
      }
      output.shadow_vertex0=vec4<f32>(l0,output.shadow_vertex0.w);
      var l1=vec3<f32>(0.0);
      if (u.light_meta.x>1.5) {
        let source=u.lights[1];
        var object_light=inverse_direction*(-source.direction_cutoff.xyz);
        if(source.position_type.w>0.5) {
          object_light=inverse_direction*(source.position_type.xyz-u.model_view[3].xyz)-input.position;
        }
        let direction=coin_bump_normalize(object_light);
        l1=vec3<f32>(dot(basis_s,direction),dot(basis_t,direction),dot(basis_n,direction));
      }
      output.shadow_vertex1=vec4<f32>(l1,output.shadow_vertex1.w);
      var l2=vec3<f32>(0.0);
      if (u.light_meta.x>2.5) {
        let source=u.lights[2];
        var object_light=inverse_direction*(-source.direction_cutoff.xyz);
        if(source.position_type.w>0.5) {
          object_light=inverse_direction*(source.position_type.xyz-u.model_view[3].xyz)-input.position;
        }
        let direction=coin_bump_normalize(object_light);
        l2=vec3<f32>(dot(basis_s,direction),dot(basis_t,direction),dot(basis_n,direction));
      }
      output.shadow_vertex2=vec4<f32>(l2,output.shadow_vertex2.w);
      var l3=vec3<f32>(0.0);
      if (u.light_meta.x>3.5) {
        let source=u.lights[3];
        var object_light=inverse_direction*(-source.direction_cutoff.xyz);
        if(source.position_type.w>0.5) {
          object_light=inverse_direction*(source.position_type.xyz-u.model_view[3].xyz)-input.position;
        }
        let direction=coin_bump_normalize(object_light);
        l3=vec3<f32>(dot(basis_s,direction),dot(basis_t,direction),dot(basis_n,direction));
      }
      output.shadow_vertex3=vec4<f32>(l3,output.shadow_vertex3.w);
      var l4=vec3<f32>(0.0);
      if (u.light_meta.x>4.5) {
        let source=u.lights[4];
        var object_light=inverse_direction*(-source.direction_cutoff.xyz);
        if(source.position_type.w>0.5) {
          object_light=inverse_direction*(source.position_type.xyz-u.model_view[3].xyz)-input.position;
        }
        let direction=coin_bump_normalize(object_light);
        l4=vec3<f32>(dot(basis_s,direction),dot(basis_t,direction),dot(basis_n,direction));
      }
      output.shadow_vertex4=vec4<f32>(l4,output.shadow_vertex4.w);
      var l5=vec3<f32>(0.0);
      if (u.light_meta.x>5.5) {
        let source=u.lights[5];
        var object_light=inverse_direction*(-source.direction_cutoff.xyz);
        if(source.position_type.w>0.5) {
          object_light=inverse_direction*(source.position_type.xyz-u.model_view[3].xyz)-input.position;
        }
        let direction=coin_bump_normalize(object_light);
        l5=vec3<f32>(dot(basis_s,direction),dot(basis_t,direction),dot(basis_n,direction));
      }
      output.shadow_vertex5=vec4<f32>(l5,output.shadow_vertex5.w);
      var l6=vec3<f32>(0.0);
      if (u.light_meta.x>6.5) {
        let source=u.lights[6];
        var object_light=inverse_direction*(-source.direction_cutoff.xyz);
        if(source.position_type.w>0.5) {
          object_light=inverse_direction*(source.position_type.xyz-u.model_view[3].xyz)-input.position;
        }
        let direction=coin_bump_normalize(object_light);
        l6=vec3<f32>(dot(basis_s,direction),dot(basis_t,direction),dot(basis_n,direction));
      }
      output.shadow_vertex6=vec4<f32>(l6,output.shadow_vertex6.w);
      var l7=vec3<f32>(0.0);
      if (u.light_meta.x>7.5) {
        let source=u.lights[7];
        var object_light=inverse_direction*(-source.direction_cutoff.xyz);
        if(source.position_type.w>0.5) {
          object_light=inverse_direction*(source.position_type.xyz-u.model_view[3].xyz)-input.position;
        }
        let direction=coin_bump_normalize(object_light);
        l7=vec3<f32>(dot(basis_s,direction),dot(basis_t,direction),dot(basis_n,direction));
      }
      output.shadow_vertex7=vec4<f32>(l7,output.shadow_vertex7.w);
      output.shadow_uv5=vec4<f32>(output.shadow_uv5.x,e);
    }
"#,
    );
    let start = s.find("var primary=input.diffuse_color;").unwrap();
    let end = start + s[start..].find("    var base_color = primary;").unwrap();
    s.replace_range(start..end,r#"
    var primary=vec4<f32>(0.0,0.0,0.0,1.0);
    var specularColor=vec3<f32>(0.0);
    if (u.light_meta.x<1.5) {
    let bm=materials[input.material_slot];
    let n=textureSample(coin_bump_image,coin_bump_sampler,
        input.shadow_vertex2.xy/input.shadow_vertex3.x).rgb*2.0-vec3<f32>(1.0);
    let l=coin_bump_normalize(input.shadow_vertex0.xyz);
    let h=coin_bump_normalize(input.shadow_vertex0.xyz+input.shadow_vertex1.xyz);
    var visibility=1.0;
    if(u.shadow_params.x>0.5) {
      visibility=vsm_shadow_factor(u.shadow_model_view_projection*vec4<f32>(input.position_model.xyz,1.0),
        (u.shadow_model_view*vec4<f32>(input.position_model.xyz,1.0)).xyz,
        input.position_view,u.shadow_params,u.shadow_meta,u.shadow_falloff,0);
    }
    primary=vec4<f32>(clamp(visibility*input.diffuse_color.rgb*clamp(dot(n,l),0.0,1.0),vec3<f32>(0.0),vec3<f32>(1.0)),1.0);
    specularColor=visibility*bm.specular.rgb*pow(clamp(dot(n,h),0.0,1.0),64.0*bm.params.x);
    } else {
      let n=textureSample(coin_bump_image,coin_bump_sampler,
        input.coin_bump_coord/input.diffuse_color.w).rgb*2.0-vec3<f32>(1.0);
      let e=input.shadow_uv5.yzw;
      var diffuse=vec3<f32>(0.0);
      for (var i=0u;i<8u;i+=1u) {
        if (f32(i)>=u.light_meta.x) {break;}
        var l=vec3<f32>(0.0);
        if(i==0u){l=input.shadow_vertex0.xyz;}
        if(i==1u){l=input.shadow_vertex1.xyz;}
        if(i==2u){l=input.shadow_vertex2.xyz;}
        if(i==3u){l=input.shadow_vertex3.xyz;}
        if(i==4u){l=input.shadow_vertex4.xyz;}
        if(i==5u){l=input.shadow_vertex5.xyz;}
        if(i==6u){l=input.shadow_vertex6.xyz;}
        if(i==7u){l=input.shadow_vertex7.xyz;}
        let visibility=coin_bump_visibility(i,input);
        diffuse+=visibility*input.diffuse_color.rgb*u.lights[i].color_intensity.rgb*
          u.lights[i].color_intensity.w*clamp(dot(n,coin_bump_normalize(l)),0.0,1.0);
        specularColor+=visibility*input.normal_view.rgb*
          pow(clamp(dot(n,coin_bump_normalize(l+e)),0.0,1.0),64.0*input.normal_view.w);
      }
      primary=vec4<f32>(clamp(diffuse,vec3<f32>(0.0),vec3<f32>(1.0)),1.0);
    }

"#);
    let mut visibility=String::from("fn coin_bump_visibility(index:u32,input:VertexOutput)->f32 {\n");
    visibility.push_str(r#"if(u.shadow_params.x>0.5 && index==u32(u.shadow_meta.y)){
      return vsm_shadow_factor(u.shadow_model_view_projection*vec4<f32>(input.position_model.xyz,1.0),
        (u.shadow_model_view*vec4<f32>(input.position_model.xyz,1.0)).xyz,input.position_view,
        u.shadow_params,u.shadow_meta,u.shadow_falloff,0);}
"#);
    visibility.push_str(r#"if(u.shadow_params_second.x>0.5 && index==u32(u.shadow_meta_second.y)){
      return vsm_shadow_factor(u.shadow_model_view_projection_second*vec4<f32>(input.position_model.xyz,1.0),
        (u.shadow_model_view_second*vec4<f32>(input.position_model.xyz,1.0)).xyz,input.position_view,
        u.shadow_params_second,u.shadow_meta_second,u.shadow_falloff_second,1);}
"#);
    visibility.push_str(r#"if(u.shadow_params_third.x>0.5 && index==u32(u.shadow_meta_third.y)){
      return vsm_shadow_factor(u.shadow_model_view_projection_third*vec4<f32>(input.position_view,1.0),
        (u.shadow_model_view_third*vec4<f32>(input.position_view,1.0)).xyz,input.position_view,
        u.shadow_params_third,u.shadow_meta_third,u.shadow_falloff_third,2);}
"#);
    visibility.push_str(r#"if(u.shadow_params_fourth.x>0.5 && index==u32(u.shadow_meta_fourth.y)){
      return vsm_shadow_factor(u.shadow_model_view_projection_fourth*vec4<f32>(input.position_view,1.0),
        (u.shadow_model_view_fourth*vec4<f32>(input.position_view,1.0)).xyz,input.position_view,
        u.shadow_params_fourth,u.shadow_meta_fourth,u.shadow_falloff_fourth,3);}
"#);
    if eight { for slot in 0..4 { let layer=slot+4; visibility+=&format!(r#"
      if(extra_shadows[{slot}].params.x>0.5 && index==u32(extra_shadows[{slot}].info.y)){{
        let receiver=extra_shadows[{slot}];
        return vsm_shadow_factor(receiver.view_to_clip*vec4<f32>(input.position_view,1.0),
          (receiver.view_to_light*vec4<f32>(input.position_view,1.0)).xyz,input.position_view,
          receiver.params,receiver.info,receiver.falloff,{layer});}}
"#); } }
    visibility+="return 1.0; }\n";
    s=s.replace("fn fragment_color(input: VertexOutput)", &format!("{}fn fragment_color(input: VertexOutput)",visibility));
    s=s.replace("fn fragment_color(input: VertexOutput)",r#"fn coin_bump_normalize(v:vec3<f32>)->vec3<f32> { let d=dot(v,v);if(d>0.0){return v*inverseSqrt(d);}return vec3<f32>(0.0); }
fn fragment_color(input: VertexOutput)"#);
    s = s.replace(
        "base_color.a), input.normal_view.w)",
        "1.0), input.normal_view.w)",
    );
    s
}
