#!/usr/bin/env python3
"""Generate minimal stub recomp files when SPEED2.EXE is not provided for building the APK."""
import os
import sys

def main():
    outdir = sys.argv[1] if len(sys.argv) > 1 else "src/recomp/gen"
    os.makedirs(outdir, exist_ok=True)

    funcs_h = """/* AUTO-GENERATED FALLBACK STUB. */
#pragma once
#include <stdint.h>
#include "recomp_types.h"

#ifdef __cplusplus
extern "C" {
#endif

void sub_00401000(void);
void sub_0075BCC7(void);

#ifdef __cplusplus
}
#endif
"""
    with open(os.path.join(outdir, "recomp_funcs.h"), "w") as f:
        f.write(funcs_h)

    recomp_c = """/* AUTO-GENERATED FALLBACK STUB. */
#include "recomp_funcs.h"
#include "recomp_types.h"

void sub_00401000(void) {}
void sub_0075BCC7(void) {}
"""
    with open(os.path.join(outdir, "recomp_0000.c"), "w") as f:
        f.write(recomp_c)

    dispatch_c = """/* AUTO-GENERATED FALLBACK STUB. */
#include "recomp_types.h"
#include "recomp_funcs.h"

const recomp_dispatch_entry_t recomp_dispatch_table[] = {
    { 0x00401000u, sub_00401000 },
    { 0x0075BCC7u, sub_0075BCC7 },
};
const uint32_t recomp_dispatch_count = 2;
"""
    with open(os.path.join(outdir, "recomp_dispatch.c"), "w") as f:
        f.write(dispatch_c)

    imports_c = """/* AUTO-GENERATED FALLBACK STUB. */
#include <stdint.h>

typedef struct { uint32_t slot; const char* name; } nfs_import_name_t;

const nfs_import_name_t nfs_import_names[] = {
    { 0, 0 }
};
"""
    with open(os.path.join(outdir, "recomp_imports.c"), "w") as f:
        f.write(imports_c)

    print(f"Generated minimal recomp stubs in {outdir}")

if __name__ == "__main__":
    main()
