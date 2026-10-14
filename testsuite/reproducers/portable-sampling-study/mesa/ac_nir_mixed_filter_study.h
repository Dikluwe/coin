/* Isolated Renoir mixed nearest-min/linear-mag experiment. Not installed globally. */
#ifndef AC_NIR_MIXED_FILTER_STUDY_H
#define AC_NIR_MIXED_FILTER_STUDY_H
#include "nir_builder.h"
#include "sid.h"
#include <stdlib.h>

static nir_def *mixed_field(nir_builder *b, nir_def *word, uint32_t mask)
{
   return nir_ushr_imm(b, nir_iand_imm(b, word, mask), __builtin_ctz(mask));
}

static nir_tex_instr *mixed_clone(nir_builder *b, nir_tex_instr *old)
{
   nir_tex_instr *copy = nir_instr_as_tex(nir_instr_clone(b->shader, &old->instr));
   copy->instr.pass_flags = 1;
   return copy;
}

static nir_def *mixed_level(nir_builder *b, nir_tex_instr *old, nir_def *uv,
                            nir_def *base_size, nir_def *level)
{
   nir_def *size = nir_umax(b, nir_ushr(b, base_size, nir_f2u32(b, level)),
                           nir_imm_ivec2(b, 1, 1));
   nir_def *n = nir_u2f32(b, size);
   nir_def *center = nir_fdiv(b, nir_fadd_imm(b, nir_ffloor(b, nir_fmul(b, uv, n)), 0.5), n);
   nir_tex_instr *sample = mixed_clone(b, old);
   sample->op = nir_texop_txl;
   nir_builder_instr_insert(b, &sample->instr);
   int bias = nir_tex_instr_src_index(sample, nir_tex_src_bias);
   if (bias >= 0) nir_tex_instr_remove_src(sample, bias);
   int at = nir_tex_instr_src_index(sample, nir_tex_src_coord);
   nir_src_rewrite(&sample->src[at].src, center);
   at = nir_tex_instr_src_index(sample, nir_tex_src_lod);
   if (at >= 0)
      nir_src_rewrite(&sample->src[at].src, level);
   else
      nir_tex_instr_add_src(sample, nir_tex_src_lod, level);
   return &sample->def;
}

static nir_def *mixed_filter_instr(nir_builder *b, nir_instr *instr, void *unused)
{
   if (instr->type != nir_instr_type_tex || instr->pass_flags)
      return NULL;
   nir_tex_instr *tex = nir_instr_as_tex(instr);
   if ((tex->op != nir_texop_tex && tex->op != nir_texop_txl && tex->op != nir_texop_txb) ||
       tex->sampler_dim != GLSL_SAMPLER_DIM_2D || tex->is_array || tex->is_shadow ||
       tex->coord_components != 2 || tex->def.bit_size != 32 ||
       nir_alu_type_get_base_type(tex->dest_type) != nir_type_float)
      return NULL;
   int si = nir_tex_instr_src_index(tex, nir_tex_src_sampler_handle);
   int ti = nir_tex_instr_src_index(tex, nir_tex_src_texture_handle);
   int ci = nir_tex_instr_src_index(tex, nir_tex_src_coord);
   if (si < 0 || ti < 0 || ci < 0 || tex->src[si].src.ssa->num_components != 4 ||
       tex->src[ti].src.ssa->num_components != 8)
      return NULL;
   /* Keep offsets, explicit gradients, non-default bias and min-LOD out of this
    * first experiment. The qualification corpus uses normalized, full-chain 2D. */
   for (unsigned i = 0; i < tex->num_srcs; ++i)
      if (tex->src[i].src_type != nir_tex_src_sampler_handle &&
          tex->src[i].src_type != nir_tex_src_texture_handle &&
          tex->src[i].src_type != nir_tex_src_coord &&
          tex->src[i].src_type != nir_tex_src_lod &&
          tex->src[i].src_type != nir_tex_src_bias)
         return NULL;

   nir_def *sampler = tex->src[si].src.ssa;
   nir_def *image = tex->src[ti].src.ssa;
   nir_def *uv = tex->src[ci].src.ssa;
   nir_def *word0 = nir_channel(b, sampler, 0);
   nir_def *word1 = nir_channel(b, sampler, 1);
   nir_def *word2 = nir_channel(b, sampler, 2);
   const uint32_t filter_mask = S_008F38_XY_MIN_FILTER(3) | S_008F38_XY_MAG_FILTER(3) |
                                S_008F38_MIP_FILTER(3);
   const uint32_t filter_value = S_008F38_XY_MAG_FILTER(V_008F38_SQ_TEX_XY_FILTER_BILINEAR) |
                                 S_008F38_MIP_FILTER(V_008F38_SQ_TEX_Z_FILTER_LINEAR);
   nir_def *active = nir_ieq_imm(b, nir_iand_imm(b, word2, filter_mask), filter_value);
   active = nir_iand(b, active, nir_ieq_imm(b, nir_iand_imm(b, word0,
                        S_008F30_MAX_ANISO_RATIO(7) | S_008F30_FORCE_UNNORMALIZED(1)), 0));
   active = nir_iand(b, active, nir_ieq_imm(b, nir_iand_imm(b, word1, S_008F34_MIN_LOD_GFX6(0xfff)), 0));
   active = nir_iand(b, active, nir_ieq_imm(b, nir_iand_imm(b, word2, S_008F38_LOD_BIAS(0x3fff)), 0));
   /* Keep the original implicit sample outside the new divergent LOD branch.
    * This correctness-first prototype pays an extra native sample on minified
    * mixed filters; it is not a performance-qualified driver patch. */
   nir_tex_instr *native = mixed_clone(b, tex);
   nir_builder_instr_insert(b, &native->instr);
   nir_push_if(b, active);
   nir_def *width = nir_iadd_imm(b, mixed_field(b, nir_channel(b, image, 2), S_008F18_WIDTH(0x3fff)), 1);
   nir_def *height = nir_iadd_imm(b, mixed_field(b, nir_channel(b, image, 2), S_008F18_HEIGHT(0x3fff)), 1);
   nir_def *base = mixed_field(b, nir_channel(b, image, 3), S_008F1C_BASE_LEVEL(15));
   nir_def *last = mixed_field(b, nir_channel(b, image, 3), S_008F1C_LAST_LEVEL(15));
   nir_def *size = nir_umax(b, nir_ushr(b, nir_vec2(b, width, height), base), nir_imm_ivec2(b, 1, 1));
   nir_def *max_level = nir_u2f32(b, nir_isub(b, last, base));
   nir_def *sampler_max = nir_fmul_imm(b, nir_u2f32(b, mixed_field(b, word1, S_008F34_MAX_LOD_GFX6(0xfff))), 1.0 / 256.0);
   max_level = nir_fmin(b, max_level, sampler_max);

   nir_def *lod;
   int li = nir_tex_instr_src_index(tex, nir_tex_src_lod);
   if (li >= 0) {
      lod = tex->src[li].src.ssa;
   } else {
      nir_def *n = nir_u2f32(b, size);
      nir_def *gx = nir_fmul(b, nir_ddx_fine(b, uv), n);
      nir_def *gy = nir_fmul(b, nir_ddy_fine(b, uv), n);
      nir_def *x = nir_fsqrt(b, nir_fadd(b,
         nir_fmul(b, nir_channel(b, gx, 0), nir_channel(b, gx, 0)),
         nir_fmul(b, nir_channel(b, gx, 1), nir_channel(b, gx, 1))));
      nir_def *y = nir_fsqrt(b, nir_fadd(b,
         nir_fmul(b, nir_channel(b, gy, 0), nir_channel(b, gy, 0)),
         nir_fmul(b, nir_channel(b, gy, 1), nir_channel(b, gy, 1))));
      lod = nir_flog2(b, nir_fmax(b, nir_fmax(b, x, y), nir_imm_float(b, 0.000001)));
   }
   int bi = nir_tex_instr_src_index(tex, nir_tex_src_bias);
   if (bi >= 0) lod = nir_fadd(b, lod, tex->src[bi].src.ssa);
   nir_push_if(b, nir_fgt_imm(b, lod, 0.0));
   lod = nir_fmin(b, nir_fmax(b, lod, nir_imm_float(b, 0.0)), max_level);
   nir_def *lo = nir_ffloor(b, lod);
   nir_def *hi = nir_fmin(b, nir_fadd_imm(b, lo, 1.0), max_level);
   nir_def *a = mixed_level(b, tex, uv, size, lo);
   nir_def *c = mixed_level(b, tex, uv, size, hi);
   nir_def *portable = nir_fadd(b, a, nir_fmul(b, nir_fsub(b, c, a), nir_fsub(b, lod, lo)));
   nir_push_else(b, NULL);
   nir_pop_if(b, NULL);
   /* if_phi requires an else predecessor, with native defined in the dominator. */
   nir_def *min_or_mag = nir_if_phi(b, portable, &native->def);
   nir_push_else(b, NULL);
   nir_pop_if(b, NULL);
   nir_def *result = nir_if_phi(b, min_or_mag, &native->def);
   return result;
}

static bool ac_nir_mixed_filter_study(nir_shader *nir)
{
   if (!getenv("COIN_MESA_MIXED_FILTER_STUDY"))
      return false;
   nir_shader_clear_pass_flags(nir);
   return nir_shader_lower_instructions(nir, NULL, mixed_filter_instr, NULL);
}
#endif
