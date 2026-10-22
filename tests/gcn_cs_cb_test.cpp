// rc5b: a compute shader's constant buffers through storage-buffer bindings
// (TranslateOptions::cb_ssbo, as host_gpu_dispatch now translates them):
// s_buffer_load from a V# in user data and s_load from a pointer in user
// data each get a binding in the dispatch's own set (0) from binding 112,
// keep the page-table walk as their fallback, and the SPIR-V is valid. Off,
// nothing is bound (the walk alone, as before). No game files are needed.
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

// The (set, binding) decorations of every variable in the module.
static std::vector<std::pair<std::uint32_t, std::uint32_t>> decorations(const std::vector<std::uint32_t>& spv, std::uint32_t& vars) {
    std::vector<std::pair<std::uint32_t, std::uint32_t>> sets, out;  // (id, set), (id, binding)
    std::vector<std::pair<std::uint32_t, std::uint32_t>> binds;
    for (std::size_t i = 5; i < spv.size();) {
        const std::uint32_t op = spv[i] & 0xffff, n = spv[i] >> 16;
        if (!n) break;
        if (op == 71 && n >= 4) {  // OpDecorate id DescriptorSet(34)/Binding(33) value
            if (spv[i + 2] == 34) sets.push_back({spv[i + 1], spv[i + 3]});
            if (spv[i + 2] == 33) binds.push_back({spv[i + 1], spv[i + 3]});
        }
        i += n;
    }
    vars = static_cast<std::uint32_t>(binds.size());
    for (const auto& b : binds) {
        for (const auto& s : sets) {
            if (s.first == b.first) out.push_back({s.second, b.second});
        }
    }
    return out;
}

static void run(const char* what, const std::vector<std::uint32_t>& words, bool cb_ssbo, bool expect_pointer) {
    const gcn::Program p = gcn::decode(words.data(), words.size());
    gcn::TranslateOptions o;
    o.stage = gcn::Stage::Compute;
    o.rsrc2 = 4 << 1;  // four user SGPRs
    o.cs_threads[0] = 64;
    o.cs_threads[1] = 1;
    o.cs_threads[2] = 1;
    o.cb_ssbo = cb_ssbo;  // compute_options: cb_descriptor_set -1 (the stage's set 0), cb_binding_base 112
    const gcn::TranslateResult r = gcn::translate(p, o);
    std::printf("%s (cb_ssbo %d): %s, %zu buffers, %zu walks\n", what, cb_ssbo ? 1 : 0, r.ok() ? "ok" : r.errors[0].c_str(),
                r.buffers.size(), r.walks.size());
    for (const auto& [why, n] : r.walks) std::printf("  walk: %s (%u)\n", why.c_str(), n);
    CHECK(r.ok());
    if (!r.ok()) return;
    spvtools::SpirvTools tools(SPV_ENV_VULKAN_1_2);
    std::string msg;
    tools.SetMessageConsumer([&](spv_message_level_t, const char*, const spv_position_t&, const char* m) {
        if (msg.empty()) msg = m;
    });
    const bool valid = tools.Validate(r.spirv.data(), r.spirv.size());
    if (!valid) std::printf("  invalid SPIR-V: %s\n", msg.c_str());
    CHECK(valid);
    std::uint32_t vars = 0;
    const auto decs = decorations(r.spirv, vars);
    bool at_112 = false;
    for (const auto& [set, binding] : decs) {
        CHECK(set == 0);  // a compute pipeline has one set
        at_112 |= binding == 112;
    }
    if (!cb_ssbo) {
        CHECK(r.buffers.empty());
        CHECK(!at_112);
        return;
    }
    CHECK(r.buffers.size() == 1);
    if (r.buffers.size() != 1) return;
    const gcn::BufferBinding& b = r.buffers[0];
    CHECK(b.binding == 112);
    CHECK(at_112);
    CHECK(!b.indexed);  // constant offsets: host_gpu_dispatch binds only these
    CHECK(b.pointer == expect_pointer);
    CHECK(b.path.user_sgpr == 0);
    CHECK(b.max_dw == 4);
}

int main() {
    const std::uint32_t kEnd = 0xBF810000u;            // s_endpgm
    const std::uint32_t kBufLoadX4 = 0xC2820100u;      // s_buffer_load_dwordx4 s[4:7], s[0:3], 0
    const std::uint32_t kLoadX4 = 0xC0820100u;         // s_load_dwordx4 s[4:7], s[0:1], 0
    run("V# in user data, constant offset", {kBufLoadX4, kEnd}, false, false);
    run("V# in user data, constant offset", {kBufLoadX4, kEnd}, true, false);
    run("pointer in user data, constant offset", {kLoadX4, kEnd}, false, true);
    run("pointer in user data, constant offset", {kLoadX4, kEnd}, true, true);
    if (failures) {
        std::printf("%d failures\n", failures);
        return 1;
    }
    std::printf("gcn_cs_cb_test: ok\n");
    return 0;
}
