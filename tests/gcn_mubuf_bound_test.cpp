// rc3: MUBUF buffer_load_format_* indexed into a traced V# reads its
// storage-buffer binding (the walk only as the fallback), as
// tbuffer_load_format_* does: valid SPIR-V, an indexed binding, no walk
// noted - with and without the fallback. No game files are needed.
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

static void run(const char* what, const std::vector<std::uint32_t>& words, bool no_fallback, bool expect_bound) {
    const gcn::Program p = gcn::decode(words.data(), words.size());
    gcn::TranslateOptions o;
    o.stage = gcn::Stage::Compute;
    o.rsrc2 = 4 << 1;  // four user SGPRs
    o.cs_threads[0] = 64;
    o.cb_ssbo = true;
    o.cb_no_fallback = no_fallback;
    const gcn::TranslateResult r = gcn::translate(p, o);
    std::printf("%s%s: %s, %zu buffers, %zu walks\n", what, no_fallback ? " (no fallback)" : "", r.ok() ? "ok" : r.errors[0].c_str(),
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
    bool walks_load = false;
    for (const auto& [why, n] : r.walks) walks_load |= why.find("buffer_load_format") != std::string::npos;
    if (expect_bound) {
        CHECK(!walks_load);
        bool indexed = false;  // (a table's own pointer is a binding too)
        for (const gcn::BufferBinding& b : r.buffers) indexed |= b.indexed;
        CHECK(indexed);
    } else {
        CHECK(walks_load);
    }
}

int main() {
    const std::uint32_t kEnd = 0xBF810000u;                 // s_endpgm
    const std::uint32_t kLoadX4 = 0xC0840100u;              // s_load_dwordx4 s[8:11], s[0:1], 0
    const std::uint32_t kFmtXyIdx0 = 0xE0042000u;           // buffer_load_format_xy, idxen, offset 0
    const std::uint32_t kFmtXyIdx16 = 0xE0042010u;          // ... offset 16
    const std::uint32_t kTable = 0x80020400u;               // v[4:5], v0, s[8:11], soffset 0
    const std::uint32_t kUser = 0x80000400u;                // v[4:5], v0, s[0:3], soffset 0
    const std::uint32_t kFmtXyOffen = 0xE0041000u;          // buffer_load_format_xy, offen
    run("V# in a table, indexed", {kLoadX4, kFmtXyIdx0, kTable, kEnd}, false, true);
    run("V# in a table, indexed", {kLoadX4, kFmtXyIdx0, kTable, kEnd}, true, true);
    run("V# in a table, indexed at +16", {kLoadX4, kFmtXyIdx16, kTable, kEnd}, false, true);
    run("V# in user data, indexed", {kFmtXyIdx0, kUser, kEnd}, false, true);
    run("V# in a table, offset register (walks)", {kLoadX4, kFmtXyOffen, kTable, kEnd}, false, false);
    if (failures) {
        std::printf("%d failures\n", failures);
        return 1;
    }
    std::printf("gcn_mubuf_bound_test: ok\n");
    return 0;
}
