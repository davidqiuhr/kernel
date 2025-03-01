/*
 * Copyright 2013 Red Hat Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Ben Skeggs
 */
#include "nv50.h"

#include <subdev/bios.h>
#include <subdev/bios/init.h>

static u64
g98_devinit_disable(struct nvkm_devinit *init)
{
	struct nvkm_device *device = init->subdev.device;
	u32 r001540 = nvkm_rd32(device, 0x001540);
	u32 r00154c = nvkm_rd32(device, 0x00154c);
	u64 disable = 0ULL;

	if (!(r001540 & 0x40000000)) {
		disable |= (1ULL << NVKM_ENGINE_MSPDEC);
		disable |= (1ULL << NVKM_ENGINE_MSVLD);
		disable |= (1ULL << NVKM_ENGINE_MSPPP);
	}

	if (!(r00154c & 0x00000004))
		disable |= (1ULL << NVKM_ENGINE_DISP);
	if (!(r00154c & 0x00000020))
		disable |= (1ULL << NVKM_ENGINE_MSVLD);
	if (!(r00154c & 0x00000040))
		disable |= (1ULL << NVKM_ENGINE_SEC);

	return disable;
}

static const struct nvkm_devinit_func
g98_devinit = {
	.preinit = nv50_devinit_preinit,
	.init = nv50_devinit_init,
	.post = nv04_devinit_post,
	.pll_set = nv50_devinit_pll_set,
	.disable = g98_devinit_disable,
};

int
g98_devinit_new(struct nvkm_device *device, int index,
		struct nvkm_devinit **pinit)
{
	return nv50_devinit_new_(&g98_devinit, device, index, pinit);
}

static const struct dmi_system_id
mcp79_force_post_ids[] = {
	{
		// Force NvForcePost=1 for Apple Nvidia 9400M devices.
		// so that the external display works at higher
		// resolutions. OVER-10385
		.ident = "Apple NVIDIA 9400M",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Apple Inc."),
		}
	},
	{ }
};

int
mcp79_devinit_new(struct nvkm_device *device, int index,
		  struct nvkm_devinit **pinit)
{
	int ret = nv50_devinit_new_(&g98_devinit, device, index, pinit);
	// Force post on quirked Apple MCP79 devices to fix issues with
	// external monitors.
	// OVER-10385
	if (!ret && dmi_check_system(mcp79_force_post_ids)) {
		nvdev_info(device, "Force NvForcePost=1 for Apple device. OVER-10385\n");
		(*pinit)->force_post = true;
	}
	return ret;
}
