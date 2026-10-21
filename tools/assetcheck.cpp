// ca_assetcheck: validates the format readers against the user's own game image.
//   ca_assetcheck <game.iso>                       parse every model/texture, report stats
//   ca_assetcheck <game.iso> export <ARC> <path> <outdir>
//       exports one PSPM as OBJ (+ its PSPI textures as PNG when given a BPX that holds both)
#include "assets/Formats.h"
#include <cstdio>
#include <cstring>
#include <map>
#include <string>


using namespace ca;

struct Stats {
    int models = 0, modelsFailed = 0, meshes = 0, tris = 0;
    int images = 0, imagesFailed = 0;
    std::map<uint32_t, int> vtypes;
    std::map<std::string, std::string> failures;
};

static void walk(ByteView v, const std::string& path, Stats& st, int depth) {
    std::vector<NamedEntry> entries;
    if (depth > 8 || !parseBpx(v, entries)) return;
    for (const NamedEntry& e : entries) {
        ByteView sub{v.data + e.offset, e.size};
        std::string p = path + "/" + e.name;
        if (sub.size >= 4 && std::memcmp(sub.data, "BPX\0", 4) == 0) {
            walk(sub, p, st, depth + 1);
        } else if (sub.size >= 4 && std::memcmp(sub.data, "PSM\x02", 4) == 0) {
            Model m;
            std::string err;
            ++st.models;
            if (!parsePspm(sub, m, &err)) {
                ++st.modelsFailed;
                if (st.failures.size() < 20) st.failures[p] = err;
                continue;
            }
            st.meshes += int(m.meshes.size());
            for (auto& mesh : m.meshes) st.tris += int(mesh.triangles.size() / 3);
        } else if (sub.size >= 6 && std::memcmp(sub.data, "PSPI01", 6) == 0) {
            PspiFile f;
            if (!f.parse(sub)) { ++st.imagesFailed; continue; }
            for (int i = 0; i < f.imageCount(); ++i) {
                Image img;
                ++st.images;
                if (!f.decode(i, img)) {
                    ++st.imagesFailed;
                    char b[64];
                    std::snprintf(b, sizeof(b), "#%d bpp=%d", i, f.info(i).bpp);
                    if (st.failures.size() < 20) st.failures[p + b] = "texture decode";
                }
            }
        }
    }
}

static int exportModel(IsoImage& iso, const std::string& arcName, const std::string& path, const std::string& out) {
    Bytes arc;
    std::vector<NamedEntry> entries;
    if (!iso.readFile("PSP_GAME/USRDIR/Arc/" + arcName, arc) || !parseArc(arc, entries)) return 1;
    size_t slash = path.find('/');
    std::string top = path.substr(0, slash), rest = slash == std::string::npos ? "" : path.substr(slash + 1);
    for (auto& e : entries) {
        if (e.name != top) continue;
        ByteView v = bpxFind({arc.data() + e.offset, e.size}, rest);
        Model m;
        std::string err;
        if (!parsePspm(v, m, &err)) { std::printf("parse failed: %s\n", err.c_str()); return 1; }
        std::string objPath = out + "/model.obj";
        FILE* f = std::fopen(objPath.c_str(), "w");
        if (!f) return 1;
        int base = 1;
        for (size_t mi = 0; mi < m.meshes.size(); ++mi) {
            std::fprintf(f, "g mesh%zu_tex%d\n", mi, m.meshes[mi].texture);
            for (auto& vx : m.meshes[mi].triangles) {
                std::fprintf(f, "v %f %f %f\nvt %f %f\nvn %f %f %f\n", vx.pos[0], vx.pos[1], vx.pos[2], vx.uv[0],
                             1 - vx.uv[1], vx.normal[0], vx.normal[1], vx.normal[2]);
            }
            for (size_t t = 0; t < m.meshes[mi].triangles.size(); t += 3, base += 3)
                std::fprintf(f, "f %d/%d/%d %d/%d/%d %d/%d/%d\n", base, base, base, base + 1, base + 1, base + 1,
                             base + 2, base + 2, base + 2);
        }
        std::fclose(f);
        std::printf("wrote %s (%zu meshes)\n", objPath.c_str(), m.meshes.size());
        return 0;
    }
    return 1;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage: ca_assetcheck <game.iso> [export <ARC> <bpx path> <outdir>]\n");
        return 2;
    }
    IsoImage iso;
    if (!iso.open(argv[1])) { std::printf("cannot open ISO %s\n", argv[1]); return 1; }
    Bytes param;
    if (!iso.readFile("PSP_GAME/PARAM.SFO", param)) { std::printf("not a PSP game image\n"); return 1; }
    std::printf("volume: %s\n", iso.volumeId().c_str());
    if (argc >= 6 && std::string(argv[2]) == "export") return exportModel(iso, argv[3], argv[4], argv[5]);

    Stats st;
    for (const char* name : {"MAIN.ARC", "DG.ARC", "CASTEST.ARC"}) {
        Bytes arc;
        std::vector<NamedEntry> entries;
        if (!iso.readFile(std::string("PSP_GAME/USRDIR/Arc/") + name, arc) || !parseArc(arc, entries)) {
            std::printf("failed to read %s\n", name);
            return 1;
        }
        std::printf("%s: %zu entries\n", name, entries.size());
        for (auto& e : entries) walk({arc.data() + e.offset, e.size}, std::string(name) + "/" + e.name, st, 0);
    }
    std::printf("models: %d (%d failed), meshes %d, triangles %d\n", st.models, st.modelsFailed, st.meshes, st.tris);
    std::printf("texture images: %d (%d failed)\n", st.images, st.imagesFailed);
    for (auto& [p, e] : st.failures) std::printf("  FAIL %s: %s\n", p.c_str(), e.c_str());
    return st.modelsFailed == 0 ? 0 : 1;
}
