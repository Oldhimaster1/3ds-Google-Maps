#!/usr/bin/env python3
"""Cheap integration assertions that catch orphaned feature modules after merges.
The source-level check is NOT a substitute for an ARM toolchain build."""
from pathlib import Path
root = Path(__file__).resolve().parents[2]
def contains(path, *items):
    src = (root / path).read_text(encoding="utf-8")
    for item in items:
        if item not in src:
            raise AssertionError(f"{path}: missing required integration {item!r}")
contains("source/main.c",
         "detail_runtime_update_panorama(", "detail_runtime_update_globe(",
         "input_remap_apply(REMAP_MAP", "REMAP_STREET:REMAP_EARTH",
         "detail_runtime_stop();", "g_more_page=(g_more_page+1)%3",
         "input_remap_save(&g_controls,INPUT_REMAP_FILE)")
contains("source/guess_ui.c",
         "detail_runtime_update_panorama(", "input_remap_apply(REMAP_GUESS",
         "detail_runtime_stop();")
contains("source/explore.c",
         "bool explore_detail_upload(", "void explore_detail_clear(",
         "detail_mesh_point(", "C3D_DrawElements(GPU_TRIANGLES,detail_index_count")
contains("source/detail_runtime.c",
         "network_request(", "threadCreate(", "atomic_store(&stream.cancel,true)",
         "pano_runtime_network_active()", "explore_detail_upload(")
contains("source/detail_plan.c",
         "pano_detail_visible(", "globe_patch_for_view(",
         "gibs.earthdata.nasa.gov")
contains("source/detail_mesh.c", "atan(sinh(", "DETAIL_PANORAMA", "DETAIL_GLOBE")
contains("source/input_remap.c", "input_remap_bind(", "input_remap_load(",
         "input_remap_save(", "KEY_SELECT")
contains("source/terrain_runtime.c", "terrain_select_zoom(", "terrain_runtime_source_zoom(")
contains("source/building_runtime.c", "building_mesh_add(", "BUILDING_JSON_LIMIT")
print("feature source wiring: PASS")
