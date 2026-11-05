// The Maxwell shader translator (runtime/nvn/maxwell/translate.c) over the game's own shaders: every vertex, fragment
// and compute binary of the RomFS's shader archives (runtime/nvn/shader_archive.c, as the precompilation finds them)
// translated to GLSL with plain keys (float attributes and targets, every varying), each written to
// <out>/<code hash>.<vert|frag|comp> for tools/test_translate_corpus.sh to compile with glslangValidator. The summary
// counts the shaders with branches and those of them that still run the `switch (pc)` state machine. Built with
// MAXWELL_FLOW_CHECK (translate.c flow_check), each program goto elimination structures is also run against the
// branches it replaces, with random branch outcomes: the blocks they run must be the same.
//
// usage: test_translate_corpus <romfs folder (extracted/romfs)> <out folder>
// The translator's switches come from the environment (SAVAGEWIND_XLATE_*, SAVAGEWIND_VK_*): one run per setting.
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../runtime/nvn/maxwell/shader.h"
#include "../runtime/nvn/shader_archive.h"

void rt_fatal_trace(void) { abort(); }
#ifdef MAXWELL_FLOW_CHECK
extern unsigned maxwell_flow_checked, maxwell_flow_bad, maxwell_stacks_why[8];
#endif

static bool has_flow(const uint8_t *code, size_t n) {  // BRA SSY PBK PCNT SYNC BRK CONT BRX
    for (size_t i = 0; i < n / 8; i++) {
        if (i % 4 == 0) continue;
        uint64_t w;
        memcpy(&w, code + i * 8, 8);
        uint64_t t = w & 0xfff0000000000000ull;
        if (t == 0xe240000000000000ull || t == 0xe290000000000000ull || t == 0xe2a0000000000000ull ||
            t == 0xe2b0000000000000ull || t == 0xe340000000000000ull || t == 0xe350000000000000ull ||
            t == 0xe250000000000000ull || (w & 0xfff8000000000000ull) == 0xf0f8000000000000ull)
            return true;
    }
    return false;
}

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: %s <romfs folder> <out folder>\n", argv[0]); return 2; }
    SaSet set;
    char why[512];
    if (!shader_archive_scan(argv[1], &set, NULL, NULL, why, sizeof why)) { fprintf(stderr, "%s\n", why); return 1; }
    maxwell_gmem_base = 0x1000000000ull;  // (any heap: global memory addresses are relative to it)
    maxwell_gmem_size = 0xC0000000ull;
    int n[3] = {0}, failed[3] = {0}, branchy[3] = {0}, machine[3] = {0};
    static const char *const ext[3] = {"vert", "frag", "comp"};
    for (int i = 0; i < set.nshaders; i++) {
        const SaShader *s = &set.shaders[i];
        int k = s->stage == STAGE_VERTEX ? 0 : s->stage == STAGE_FRAGMENT ? 1 : s->stage == STAGE_COMPUTE ? 2 : -1;
        if (k < 0) continue;
        ShaderKey key;
        memset(&key, 0, sizeof key);
        key.glsl = 1;
        if (k == 1) key.vs_out_mask = 0xFFFFFFFFu;
        if (k == 2) key.shared_size = s->shared;
#ifdef HAVE_CONST_DATA
        // (c1: the compiler constants after the code, at the control section's constant offset)
        maxwell_const_data = s->max_size < s->size ? s->code + s->max_size : NULL;
        maxwell_const_size = s->max_size < s->size ? s->size - s->max_size : 0;
#endif
        size_t head = k == 2 ? 0 : 0x80;
        size_t bin = maxwell_binary_size(s->code, s->max_size, s->stage);
        bool flow = bin > head && has_flow(s->code + head, bin - head);
        ShaderInfo *si = maxwell_translate(s->code, s->max_size, s->stage, &key);
        n[k]++;
        if (!si->msl) {
            failed[k]++;
            if (failed[k] <= 5) fprintf(stderr, "%s %016llx: %s\n", ext[k], (unsigned long long)s->hash, si->error);
            maxwell_free(si);
            continue;
        }
        branchy[k] += flow;
        machine[k] += strstr(si->msl, "switch (pc)") != NULL;
        char path[1024];
        snprintf(path, sizeof path, "%s/%016llx.%s", argv[2], (unsigned long long)s->hash, ext[k]);
        FILE *f = fopen(path, "w");
        if (!f) { perror(path); return 1; }
        fputs(si->msl, f);
        fclose(f);
        maxwell_free(si);
    }
    for (int k = 0; k < 3; k++)
        printf("%s: %d shaders, %d failed to translate, %d with branches, %d of them on the pc state machine\n", ext[k], n[k],
               failed[k], branchy[k], machine[k]);
#ifdef MAXWELL_FLOW_CHECK
    printf("flow check: %u structured programs run against their branches, %u differ\n", maxwell_flow_checked, maxwell_flow_bad);
    printf("stacks kept: jump to no label %u, paths disagree %u, overflow %u, call / indirect %u, empty pop %u, pop target "
           "differs %u\n", maxwell_stacks_why[1], maxwell_stacks_why[2], maxwell_stacks_why[3], maxwell_stacks_why[4],
           maxwell_stacks_why[5], maxwell_stacks_why[6]);
#endif
    shader_archive_free(&set);
    return 0;
}
