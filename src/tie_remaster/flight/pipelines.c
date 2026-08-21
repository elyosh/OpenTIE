#include "aeron/log.h"
#include "tie_remaster/flight/renderer_internal.h"
#include "tie_remaster/gpu_debug.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ===== Shader loader ================================================== */

AeronShader* TieFlightRenderer_CompileShader(const char* basename, AeronShaderStage stage,
											 uint32_t num_samplers, uint32_t num_uniform_buffers,
											 uint32_t num_storage_buffers) {
	/* Application shaders are cooked into Aeron's single shader root (see
	 * TIE_SHADER_BINARY_DIR); Aeron_CreateShader resolves <basename>.msl
	 * there alongside Aeron's own. */
	AeronShader* sh = Aeron_CreateShader(&(AeronShaderDesc) {
		.name = basename,
		.stage = stage,
		.sampler_count = num_samplers,
		.uniform_buffer_count = num_uniform_buffers,
		.storage_buffer_count = num_storage_buffers,
	});
	if (!sh)
		Aeron_LogError("tie.flight", "shader load failed: %s", basename);
	return sh;
}

/* ===== RT + texture + buffer creation ================================ */

AeronRenderTarget* TieFlightRenderer_CreateColorRt(AeronTextureFormat fmt, int w, int h) {
	return Aeron_CreateRenderTarget(&(AeronRenderTargetDesc) {
		.width = w,
		.height = h,
		.format = fmt,
		.sample_count = AERON_SAMPLE_COUNT_1,
	});
}

AeronDepthTarget* TieFlightRenderer_CreateDepthRt(int w, int h) {
	/* `sampled` lets the SSAO pass read depth between the flight geometry
	 * sub-pass and the cockpit overlay sub-pass. */
	return Aeron_CreateDepthTarget(&(AeronDepthTargetDesc) {
		.width = w,
		.height = h,
		.format = AERON_TEXTURE_FORMAT_D32_FLOAT,
		.sample_count = AERON_SAMPLE_COUNT_1,
		.sampled = 1,
	});
}

/* Generic 2D sampled texture for LUTs / palettes. `bpp` is unused at
 * GPU-create time; the upload helper carries it separately. */
AeronTexture* TieFlightRenderer_CreateDataTex(AeronTextureFormat fmt, int w, int h, int bpp) {
	(void)bpp;
	return Aeron_CreateTexture(&(AeronTextureDesc) {
		.width = w,
		.height = h,
		.mip_count = 1,
		.format = fmt,
		.usage = AERON_TEXTURE_USAGE_SAMPLED | AERON_TEXTURE_USAGE_TRANSFER_DST,
	});
}

bool TieFlightRenderer_UploadToTexture(AeronCommandBuffer* cmd, AeronTexture* tex, int w, int h, int bpp,
									   const void* data) {
	/* Raw path: bytes are already in the texture's own format. */
	return Aeron_UploadTextureDataCmd(cmd, &(AeronTextureUploadDesc) {
											   .texture = tex,
											   .width = w,
											   .height = h,
											   .raw_data = data,
											   .raw_size = (uint32_t)(w * h * bpp),
										   }) != 0;
}

AeronBuffer* TieFlightRenderer_CreateBuffer(uint32_t usage, uint32_t size) {
	return Aeron_CreateBuffer(&(AeronBufferDesc) {
		.usage = usage,
		.size = size,
		.memory_usage = AERON_MEMORY_USAGE_GPU_ONLY,
	});
}

bool TieFlightRenderer_GrowBuffer(AeronBuffer** buf, uint32_t* cap, uint32_t need, uint32_t usage,
								  const char* dbg_name) {
	(void)dbg_name;
	if (need <= *cap)
		return true;
	if (*buf)
		Aeron_DestroyBuffer(*buf);
	uint32_t newcap = (*cap ? *cap * 2 : 4096);
	while (newcap < need)
		newcap *= 2;
	*buf = TieFlightRenderer_CreateBuffer(usage, newcap);
	if (!*buf) {
		*cap = 0;
		return false;
	}
	*cap = newcap;
	return true;
}

bool TieFlightRenderer_UploadToBuffer(AeronCommandBuffer* cmd, AeronBuffer* buf, const void* data,
									  uint32_t size) {
	return Aeron_UploadBufferDataCmd(cmd, buf, 0, data, size) != 0;
}

/* ===== Pipeline creation ============================================= */

/* Shared blend-state factories. Almost all flight color targets are
 * plain replace (no blend) with a per-target write mask. */
AeronBlendStateDesc TieFlightRenderer_BlendOpaque(uint8_t write_mask) {
	return (AeronBlendStateDesc) {
		.enabled = 0,
		.color_write_mask_enable = 1,
		.color_write_mask = write_mask,
	};
}

/* Premultiplied-alpha "over" blend (src + dst*(1-srcA)). Used by the
 * present pipeline to cross-fade the tonemapped overlay. */
AeronBlendStateDesc TieFlightRenderer_BlendPmaOver(void) {
	return (AeronBlendStateDesc) {
		.enabled = 1,
		.src_color = AERON_BLEND_ONE,
		.dst_color = AERON_BLEND_ONE_MINUS_SRC_ALPHA,
		.color_op = AERON_BLEND_OP_ADD,
		.src_alpha = AERON_BLEND_ONE,
		.dst_alpha = AERON_BLEND_ONE_MINUS_SRC_ALPHA,
		.alpha_op = AERON_BLEND_OP_ADD,
	};
}

/* ---- Table-driven pipeline construction ----
 * The flight pipelines differ only in vertex layout, primitive, depth
 * config, and their color-target list. A TieFlightPipelineDescription captures those;
 * TieFlightPipelines_CreateGraphicsPipeline() assembles the AeronGraphicsPipelineDesc. */

typedef enum { VTX_NONE = 0, VTX_MESH, VTX_LINE } VertexLayout;

typedef struct {
	AeronTextureFormat format;
	uint8_t write_mask; /* color_write_mask; ignored if pma */
	bool pma;           /* premultiplied-alpha "over" blend */
} TieFlightPipelineTargetDescription;

typedef struct {
	VertexLayout vtx;
	AeronPrimitiveType prim;
	bool has_depth; /* D32 depth attachment present */
	bool depth_test;
	bool depth_write;
	AeronCompareOp depth_compare; /* used only when depth_test */
	TieFlightPipelineTargetDescription targets[2];
	uint32_t num_targets;
	AeronSampleCount sample_count;
} TieFlightPipelineDescription;

static uint32_t TieFlightRenderer_MeshVertexInput(AeronVertexAttributeDesc attrs[12],
												  AeronVertexBufferLayoutDesc* vbd);
static uint32_t TieFlightRenderer_LineVertexInput(AeronVertexAttributeDesc attrs[9],
												  AeronVertexBufferLayoutDesc* vbd);

static AeronGraphicsPipeline*
TieFlightPipelines_CreateGraphicsPipeline(AeronShader* vs, AeronShader* ps,
										  const TieFlightPipelineDescription* d) {
	AeronVertexAttributeDesc attrs[12];
	AeronVertexBufferLayoutDesc vbd = { 0 };
	uint32_t num_attrs = 0;
	switch (d->vtx) {
		case VTX_MESH:
			num_attrs = TieFlightRenderer_MeshVertexInput(attrs, &vbd);
			break;
		case VTX_LINE:
			num_attrs = TieFlightRenderer_LineVertexInput(attrs, &vbd);
			break;
		case VTX_NONE:
		default:
			break;
	}
	const bool has_vtx = (d->vtx != VTX_NONE);

	AeronColorTargetStateDesc cts[2] = { 0 };
	for (uint32_t i = 0; i < d->num_targets; ++i) {
		cts[i].format = d->targets[i].format;
		cts[i].blend = d->targets[i].pma ? TieFlightRenderer_BlendPmaOver()
										 : TieFlightRenderer_BlendOpaque(d->targets[i].write_mask);
	}

	return Aeron_CreateGraphicsPipeline(&(AeronGraphicsPipelineDesc) {
		.vertex_shader = vs,
		.fragment_shader = ps,
		.primitive_type = d->prim,
		.cull_mode = AERON_CULL_NONE,
		.vertex_buffers = has_vtx ? &vbd : NULL,
		.vertex_buffer_count = has_vtx ? 1u : 0u,
		.attributes = has_vtx ? attrs : NULL,
		.attribute_count = num_attrs,
		.sample_count = d->sample_count,
		.depth_format = d->has_depth ? AERON_TEXTURE_FORMAT_D32_FLOAT : AERON_TEXTURE_FORMAT_UNKNOWN,
		.depth =
			(AeronDepthStateDesc) {
				.depth_test = d->depth_test ? 1 : 0,
				.depth_write = d->depth_write ? 1 : 0,
				.compare = d->depth_compare,
			},
		.color_target_count = d->num_targets,
		.color_targets = cts,
	});
}

/* TieFlightVertex layout (12 attributes) mirroring flight_mesh.vert.hlsl's
 * VSIn. Locations match the HLSL semantic-to-location map. */
static uint32_t TieFlightRenderer_MeshVertexInput(AeronVertexAttributeDesc attrs[12],
												  AeronVertexBufferLayoutDesc* vbd) {
	attrs[0] = (AeronVertexAttributeDesc) { .location = 0,
											.buffer_slot = 0,
											.format = AERON_VERTEX_FORMAT_FLOAT3,
											.offset = (uint32_t)offsetof(TieFlightVertex, pos) };
	attrs[1] = (AeronVertexAttributeDesc) { .location = 1,
											.buffer_slot = 0,
											.format = AERON_VERTEX_FORMAT_FLOAT3,
											.offset = (uint32_t)offsetof(TieFlightVertex, normal) };
	attrs[2] = (AeronVertexAttributeDesc) { .location = 2,
											.buffer_slot = 0,
											.format = AERON_VERTEX_FORMAT_FLOAT3,
											.offset = (uint32_t)offsetof(TieFlightVertex, vertex_normal) };
	attrs[3] = (AeronVertexAttributeDesc) { .location = 3,
											.buffer_slot = 0,
											.format = AERON_VERTEX_FORMAT_FLOAT,
											.offset = (uint32_t)offsetof(TieFlightVertex, color) };
	attrs[4] = (AeronVertexAttributeDesc) { .location = 4,
											.buffer_slot = 0,
											.format = AERON_VERTEX_FORMAT_FLOAT,
											.offset = (uint32_t)offsetof(TieFlightVertex, material_id) };
	attrs[5] = (AeronVertexAttributeDesc) { .location = 5,
											.buffer_slot = 0,
											.format = AERON_VERTEX_FORMAT_FLOAT,
											.offset = (uint32_t)offsetof(TieFlightVertex, mesh_index) };
	attrs[6] = (AeronVertexAttributeDesc) { .location = 6,
											.buffer_slot = 0,
											.format = AERON_VERTEX_FORMAT_FLOAT,
											.offset = (uint32_t)offsetof(TieFlightVertex, face_flags) };
	attrs[7] = (AeronVertexAttributeDesc) { .location = 7,
											.buffer_slot = 0,
											.format = AERON_VERTEX_FORMAT_FLOAT,
											.offset = (uint32_t)offsetof(TieFlightVertex, face_u) };
	attrs[8] = (AeronVertexAttributeDesc) { .location = 8,
											.buffer_slot = 0,
											.format = AERON_VERTEX_FORMAT_FLOAT,
											.offset = (uint32_t)offsetof(TieFlightVertex, face_v) };
	attrs[9] = (AeronVertexAttributeDesc) { .location = 9,
											.buffer_slot = 0,
											.format = AERON_VERTEX_FORMAT_FLOAT,
											.offset = (uint32_t)offsetof(TieFlightVertex, decal_offset) };
	attrs[10] = (AeronVertexAttributeDesc) { .location = 10,
											 .buffer_slot = 0,
											 .format = AERON_VERTEX_FORMAT_FLOAT,
											 .offset = (uint32_t)offsetof(TieFlightVertex, decal_count) };
	*vbd = (AeronVertexBufferLayoutDesc) {
		.slot = 0,
		.stride = (uint32_t)sizeof(TieFlightVertex),
		.per_instance = 0,
	};
	return 11;
}

/* Mesh pipeline for the scene's forward color pass. Reversed-Z:
 * GREATER_OR_EQUAL test + write on. cull NONE — ShipModelData winding combined with
 * craft_to_world's Y-reflect and the projection's Y-flip lands visible
 * front faces CCW; the FS shades both sides via SV_IsFrontFace. */
AeronGraphicsPipeline* TieFlightRenderer_CreateMeshPipeline(AeronShader* vs, AeronShader* ps,
															AeronTextureFormat rt_fmt,
															AeronSampleCount sample_count) {
	TieFlightPipelineDescription d = {
		.vtx = VTX_MESH,
		.prim = AERON_PRIMITIVE_TRIANGLES,
		.has_depth = true,
		.depth_test = true,
		.depth_write = true,
		.depth_compare = AERON_COMPARE_GREATER_EQUAL,
		.targets = { { .format = rt_fmt, .write_mask = 0xF } },
		.num_targets = 1,
		.sample_count = sample_count,
	};
	return TieFlightPipelines_CreateGraphicsPipeline(vs, ps, &d);
}

/* TieFlightLineVertex layout (9 attributes), shared by the line and bolt
 * pipelines. Keep in sync with flight_line.vert.hlsl. */
static uint32_t TieFlightRenderer_LineVertexInput(AeronVertexAttributeDesc attrs[9],
												  AeronVertexBufferLayoutDesc* vbd) {
	attrs[0] = (AeronVertexAttributeDesc) { .location = 0,
											.buffer_slot = 0,
											.format = AERON_VERTEX_FORMAT_FLOAT3,
											.offset = (uint32_t)offsetof(TieFlightLineVertex, pos_a) };
	attrs[1] = (AeronVertexAttributeDesc) { .location = 1,
											.buffer_slot = 0,
											.format = AERON_VERTEX_FORMAT_FLOAT3,
											.offset = (uint32_t)offsetof(TieFlightLineVertex, pos_b) };
	attrs[2] = (AeronVertexAttributeDesc) { .location = 2,
											.buffer_slot = 0,
											.format = AERON_VERTEX_FORMAT_FLOAT3,
											.offset = (uint32_t)offsetof(TieFlightLineVertex, normal) };
	attrs[3] = (AeronVertexAttributeDesc) { .location = 3,
											.buffer_slot = 0,
											.format = AERON_VERTEX_FORMAT_FLOAT,
											.offset = (uint32_t)offsetof(TieFlightLineVertex, endpoint) };
	attrs[4] = (AeronVertexAttributeDesc) { .location = 4,
											.buffer_slot = 0,
											.format = AERON_VERTEX_FORMAT_FLOAT,
											.offset = (uint32_t)offsetof(TieFlightLineVertex, side) };
	attrs[5] = (AeronVertexAttributeDesc) { .location = 5,
											.buffer_slot = 0,
											.format = AERON_VERTEX_FORMAT_FLOAT,
											.offset = (uint32_t)offsetof(TieFlightLineVertex, color) };
	attrs[6] = (AeronVertexAttributeDesc) { .location = 6,
											.buffer_slot = 0,
											.format = AERON_VERTEX_FORMAT_FLOAT,
											.offset = (uint32_t)offsetof(TieFlightLineVertex, material_id) };
	attrs[7] =
		(AeronVertexAttributeDesc) { .location = 7,
									 .buffer_slot = 0,
									 .format = AERON_VERTEX_FORMAT_FLOAT,
									 .offset = (uint32_t)offsetof(TieFlightLineVertex, thickness_base) };
	attrs[8] = (AeronVertexAttributeDesc) { .location = 8,
											.buffer_slot = 0,
											.format = AERON_VERTEX_FORMAT_FLOAT,
											.offset = (uint32_t)offsetof(TieFlightLineVertex, mesh_index) };
	*vbd = (AeronVertexBufferLayoutDesc) {
		.slot = 0,
		.stride = (uint32_t)sizeof(TieFlightLineVertex),
		.per_instance = 0,
	};
	return 9;
}

/* Line pipeline — TieFlightLineVertex layout, TRIANGLELIST, no cull. */
AeronGraphicsPipeline* TieFlightRenderer_CreateLinePipeline(AeronShader* vs, AeronShader* ps,
															AeronTextureFormat rt_fmt,
															AeronSampleCount sample_count) {
	TieFlightPipelineDescription d = {
		.vtx = VTX_LINE,
		.prim = AERON_PRIMITIVE_TRIANGLES,
		.has_depth = true,
		.depth_test = true,
		.depth_write = true,
		.depth_compare = AERON_COMPARE_GREATER_EQUAL,
		.targets = { { .format = rt_fmt, .write_mask = 0xF } },
		.num_targets = 1,
		.sample_count = sample_count,
	};
	return TieFlightPipelines_CreateGraphicsPipeline(vs, ps, &d);
}

/* Bolt-specific line pipeline. Identical to the line pipeline except
 * depth write is off — bolts stay occluded by ships in front but their
 * own 5–7 edges resolve purely by IBO order. */
AeronGraphicsPipeline* TieFlightRenderer_CreateBoltLinePipeline(AeronShader* vs, AeronShader* ps,
																AeronTextureFormat rt_fmt,
																AeronSampleCount sample_count) {
	TieFlightPipelineDescription d = {
		.vtx = VTX_LINE,
		.prim = AERON_PRIMITIVE_TRIANGLES,
		.has_depth = true,
		.depth_test = true,
		.depth_write = false,
		.depth_compare = AERON_COMPARE_GREATER_EQUAL,
		.targets = { { .format = rt_fmt, .write_mask = 0xF } },
		.num_targets = 1,
		.sample_count = sample_count,
	};
	return TieFlightPipelines_CreateGraphicsPipeline(vs, ps, &d);
}

/* The skybox pipeline moved into aeron_scene (AeronScene_SetSkyCube). */
