#ifndef TIE_REMASTER_GPU_DEBUG_H
#define TIE_REMASTER_GPU_DEBUG_H

/* Compatibility spellings for TIE's existing GPU annotations. */

#include "aeron/render.h"

#define TIE_GPU_PUSH(cmd, label) Aeron_GpuDebugPush((cmd), (label))
#define TIE_GPU_POP(cmd) Aeron_GpuDebugPop((cmd))
#define TIE_GPU_MARKER(cmd, label) Aeron_GpuDebugMarker((cmd), (label))
#define TIE_GPU_NAME_TEXTURE(dev, tex, nm) Aeron_GpuDebugNameTexture((tex), (nm))
#define TIE_GPU_NAME_BUFFER(dev, buf, nm) Aeron_GpuDebugNameBuffer((buf), (nm))

#endif
