/*
 * lfd2gltf — extract ship models from a SPECIES*.LFD into glTF 2.0
 * files (one per ship resource) for offline inspection / authoring
 * in Blender.
 *
 * Usage:
 *   lfd2gltf <SPECIES.LFD> <output_dir>
 *
 * Iterates every resource of FOURCC type 'SHIP' (the standard LFD
 * type tag used by the engine's species LFDs), passes each payload
 * to the ShipModelData converter and writes a glTF + .bin
 * pair via gltf_export. Resources that fail to parse are logged and
 * skipped; the rest of the bundle is processed normally.
 *
 * Per-resource output: <output_dir>/<resource_name>.{gltf,bin}.
 * Resource names come from the LFD RMAP directly (e.g. "XWING",
 * "TIEFTR", "ASSAULT").
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "gltf_export.h"
#include "lfd_file.h"
#include "tie_runtime/flight_assets/ship_model_converter.h"

static uint32_t TieLfd2Gltf_Fourcc(const char* s) {
	return ((uint32_t)(uint8_t)s[0] << 24) | ((uint32_t)(uint8_t)s[1] << 16) |
		   ((uint32_t)(uint8_t)s[2] << 8) | ((uint32_t)(uint8_t)s[3]);
}

int main(int argc, char** argv) {
	if (argc != 3) {
		fprintf(stderr,
				"usage: %s <SPECIES.LFD> <output_dir>\n"
				"\n"
				"Extracts every 'SHIP' resource into <output_dir>/<name>.gltf+.bin.\n"
				"Tested against tie-collector/RES640/SPECIES*.LFD.\n",
				argv[0]);
		return 1;
	}
	const char* lfd_path = argv[1];
	const char* out_dir = argv[2];

	/* Make sure the output dir exists. errno-EEXIST is fine. */
	if (mkdir(out_dir, 0755) != 0 && errno != EEXIST) {
		fprintf(stderr, "mkdir %s: %s\n", out_dir, strerror(errno));
		return 1;
	}

	TieLfdFile lfd;
	char err[512];
	if (!TieLfdFile_Open(&lfd, lfd_path, err, sizeof err)) {
		fprintf(stderr, "%s\n", err);
		return 1;
	}
	printf("opened %s (%u resources)\n", lfd_path, (unsigned)lfd.count);

	const uint32_t want = TieLfd2Gltf_Fourcc("SHIP");
	unsigned exported = 0, skipped = 0, failed = 0;

	for (uint32_t i = 0; i < lfd.count; i++) {
		const TieLfdFileEntry* e = &lfd.entries[i];
		if (e->type != want) {
			++skipped;
			continue;
		}

		/* Mirror fediskio_loadspecies's orient strip: ship species
		 * have load_flags & 1, which makes the engine read (and
		 * DISCARD) the first 2 bytes of the LFD chunk payload before
		 * loading the rest into species_buf. The converter then skips
		 * another 2 bytes ("file-size prefix" in its comment — really
		 * the ShipModelData.prefix magic), so the total payload-to-
		 * ShipModelData offset is 4. Skip the first 2 here; let proc-
		 * mesh handle its own +2 internally. */
		if (e->size < 2u + 0x20u) {
			fprintf(stderr, "  %-9s: too small (size=%u)\n", e->name, (unsigned)e->size);
			++failed;
			continue;
		}
		const uint8_t* blob = TieLfdFile_Data(&lfd, e) + 2;
		const uint32_t blob_size = e->size - 2u;

		TieFlightShipModel pm = { 0 };
		if (!TieShipModelConverter_Build(blob, blob_size, 90.0f, &pm)) {
			fprintf(stderr, "  %-9s: parse failed (size=%u)\n", e->name, (unsigned)blob_size);
			++failed;
			continue;
		}

		const bool ok = TieLfd2GltfExport_Ship(&pm, out_dir, e->name);
		if (ok) {
			printf("  %-9s: %u verts, %u tris -> %s/%s.{gltf,bin}\n", e->name, pm.vertex_count,
				   pm.index_count / 3u, out_dir, e->name);
			++exported;
		} else {
			++failed;
		}

		TieShipModelConverter_Free(&pm);
	}

	printf("done: %u exported, %u failed, %u non-SHIP resources skipped\n", exported, failed, skipped);

	TieLfdFile_Close(&lfd);
	return (failed > 0) ? 1 : 0;
}
