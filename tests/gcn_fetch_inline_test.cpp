// rc4: a branch-free vertex shader and its branch-free fetch shader translate
// as one straight line (the fetch shader in place of s_swappc_b64, its
// s_setpc_b64 dropped): valid SPIR-V, the fetch inlined, and the main
// program's bound load kept - where through the dispatcher the fetch shader's
// reuse of the same SGPRs excluded it ("registers redefined, dispatcher
// order"). A fetch shader with a branch keeps the dispatcher. Also the rc4
// bound load at an SGPR offset (indexed_buffer_load). No game files needed.
#include "gcn/isa.h"
#include "gcn/translate.h"

#include <spirv-tools/libspirv.hpp>

#include <cstdio>
#include <string>
#include <vector>

static int failures = 0;
#define CHECK(c)                                                     \
    do {                                                             \
        if (!(c)) {                                                  \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); \
            ++failures;                                              \
        }                                                            \
    } while (0)

static bool valid_spirv(const gcn::TranslateResult& r) {
    spvtools::SpirvTools tools(SPV_ENV_VULKAN_1_2);
    std::string msg;
    tools.SetMessageConsumer([&](spv_message_level_t, const char*, const spv_position_t&, const char* m) {
        if (msg.empty()) msg = m;
    });
    const bool ok = tools.Validate(r.spirv.data(), r.spirv.size());
    if (!ok) std::printf("  invalid SPIR-V: %s\n", msg.c_str());
    return ok;
}

static gcn::TranslateResult vs(const char* what, const std::vector<std::uint32_t>& main_words, const std::vector<std::uint32_t>& fetch_words,
                               bool force_dispatcher) {
    const gcn::Program p = gcn::decode(main_words.data(), main_words.size());
    const gcn::Program f = gcn::decode(fetch_words.data(), fetch_words.size());
    gcn::TranslateOptions o;
    o.stage = gcn::Stage::Vertex;
    o.rsrc2 = 6 << 1;  // six user SGPRs: s[0:1] fetch shader, s[2:3] vertex table, s[4:5] constant table
    o.fetch = &f;
    o.cb_ssbo = true;
    o.force_dispatcher = force_dispatcher;
    gcn::TranslateResult r = gcn::translate(p, o);
    std::printf("%s: %s, fetch_inlined %d, %zu buffers, %zu walks\n", what, r.ok() ? "ok" : r.errors[0].c_str(), r.fetch_inlined ? 1 : 0,
                r.buffers.size(), r.walks.size());
    for (const auto& [why, n] : r.walks) std::printf("  walk: %s (%u)\n", why.c_str(), n);
    CHECK(r.ok());
    if (r.ok()) CHECK(valid_spirv(r));
    return r;
}

static bool has_walk(const gcn::TranslateResult& r, const char* part) {
    for (const auto& [why, n] : r.walks) {
        if (why.find(part) != std::string::npos) return true;
    }
    return false;
}

int main() {
    const std::uint32_t kEnd = 0xBF810000u;        // s_endpgm
    const std::uint32_t kWait = 0xBF8C0000u;       // s_waitcnt 0
    const std::uint32_t kSwappc = 0xBE802100u;     // s_swappc_b64 s[0:1], s[0:1]
    const std::uint32_t kSetpc = 0xBE802000u;      // s_setpc_b64 s[0:1]
    const std::uint32_t kLoadVb = 0xC0840300u;     // s_load_dwordx4 s[8:11], s[2:3], 0
    const std::uint32_t kLoadCb = 0xC0840500u;     // s_load_dwordx4 s[8:11], s[4:5], 0
    const std::uint32_t kFmtXyz = 0xE0082000u;     // buffer_load_format_xyz, idxen, offset 0
    const std::uint32_t kFetchRegs = 0x80020400u;  // v[4:6], v0, s[8:11], soffset 0
    const std::uint32_t kFmtXy = 0xE0042000u;      // buffer_load_format_xy, idxen, offset 0
    const std::uint32_t kMainRegs = 0x80020800u;   // v[8:9], v0, s[8:11], soffset 0
    const std::uint32_t kExpPos[2] = {0xF80008CFu, 0x07060504u};  // exp pos0 v4, v5, v6, v7 done
    const std::uint32_t kBranch = 0xBF820001u;     // s_branch +1
    const std::uint32_t kNop = 0xBF800000u;        // s_nop 0
    const std::vector<std::uint32_t> main_prog = {kSwappc, kLoadCb, kWait, kFmtXy, kMainRegs, kWait, kExpPos[0], kExpPos[1], kEnd};
    const std::vector<std::uint32_t> fetch = {kLoadVb, kWait, kFmtXyz, kFetchRegs, kWait, kSetpc};
    const std::vector<std::uint32_t> fetch_branchy = {kLoadVb, kWait, kBranch, kNop, kFmtXyz, kFetchRegs, kWait, kSetpc};

    const gcn::TranslateResult in = vs("inline", main_prog, fetch, false);
    CHECK(in.fetch_inlined);
    CHECK(!has_walk(in, "dispatcher order"));
    CHECK(!has_walk(in, "buffer_load_format"));
    int indexed = 0;
    for (const gcn::BufferBinding& b : in.buffers) indexed += b.indexed ? 1 : 0;
    CHECK(indexed == 2);  // the vertex buffer and the main program's, both bound

    const gcn::TranslateResult disp = vs("dispatcher (forced)", main_prog, fetch, true);
    CHECK(!disp.fetch_inlined);
    CHECK(has_walk(disp, "dispatcher order"));

    const gcn::TranslateResult br = vs("fetch with a branch", main_prog, fetch_branchy, false);
    CHECK(!br.fetch_inlined);

    // rc4 item 3: buffer_load_format_xy at soffset s12 is bound, with the walk
    // as its fallback (and still a walk with cb_no_fallback).
    {
        const std::uint32_t kLoadX4 = 0xC0840100u;  // s_load_dwordx4 s[8:11], s[0:1], 0
        const std::uint32_t kSoffS12 = 0x0C020400u;  // v[4:5], v0, s[8:11], soffset s12
        const std::vector<std::uint32_t> words = {kLoadX4, kFmtXy, kSoffS12, kEnd};
        for (int nf = 0; nf < 2; ++nf) {
            const gcn::Program p = gcn::decode(words.data(), words.size());
            gcn::TranslateOptions o;
            o.stage = gcn::Stage::Compute;
            o.rsrc2 = 13 << 1;
            o.cs_threads[0] = 64;
            o.cb_ssbo = true;
            o.cb_no_fallback = nf != 0;
            const gcn::TranslateResult r = gcn::translate(p, o);
            std::printf("soffset s12%s: %s, soffset_bound %u, %zu walks\n", nf ? " (no fallback)" : "", r.ok() ? "ok" : r.errors[0].c_str(),
                        r.soffset_bound, r.walks.size());
            for (const auto& [why, n] : r.walks) std::printf("  walk: %s (%u)\n", why.c_str(), n);
            CHECK(r.ok());
            if (!r.ok()) continue;
            CHECK(valid_spirv(r));
            if (nf) {
                CHECK(r.soffset_bound == 0);
                CHECK(has_walk(r, "register offset"));
            } else {
                CHECK(r.soffset_bound == 1);
                CHECK(!has_walk(r, "buffer_load_format"));
            }
        }
    }
    if (failures) {
        std::printf("%d failures\n", failures);
        return 1;
    }
    std::printf("gcn_fetch_inline_test: ok\n");
    return 0;
}
