#include "game/OriginalWorld.h"
#include "core/Log.h"
#include <cfloat>
#include <cmath>

namespace ca {

bool GameData::open(const std::string& isoPath) {
    open_ = false;
    if (!iso_.open(isoPath)) return false;
    Bytes sfo;
    if (!iso_.readFile("PSP_GAME/PARAM.SFO", sfo)) return false;
    std::string s(sfo.begin(), sfo.end());
    if (s.find("CODED ARMS") == std::string::npos) log::warn("PARAM.SFO does not name CODED ARMS; continuing anyway");
    path_ = isoPath;
    open_ = true;
    return true;
}

bool GameData::loadArc(const std::string& arc) {
    if (arcData_.count(arc)) return true;
    Bytes data;
    std::vector<NamedEntry> idx;
    if (!iso_.readFile("PSP_GAME/USRDIR/Arc/" + arc, data) || !parseArc(data, idx)) return false;
    arcData_[arc] = std::move(data);
    arcIndex_[arc] = std::move(idx);
    return true;
}

ByteView GameData::arcEntry(const std::string& arc, const std::string& entry) {
    if (!open_ || !loadArc(arc)) return {};
    for (const NamedEntry& e : arcIndex_[arc])
        if (e.name == entry) return {arcData_[arc].data() + e.offset, e.size};
    return {};
}

std::vector<std::string> GameData::arcEntries(const std::string& arc) {
    std::vector<std::string> out;
    if (!open_ || !loadArc(arc)) return out;
    for (const NamedEntry& e : arcIndex_[arc]) out.push_back(e.name);
    return out;
}

bool loadPspiImage(ByteView pspi, int index, Image& out) {
    PspiFile f;
    return f.parse(pspi) && f.decode(index, out);
}

bool OriginalWorld::loadTileset(GameData& data, Renderer& r, const std::string& tileset, float unitsPerMeter,
                                std::string& error) {
    unload(r);
    tileset_ = tileset;
    scale_ = 1.0f / unitsPerMeter;
    ByteView set = data.arcEntry("DG.ARC", tileset + ".BPX");
    if (!set.valid()) { error = "tile set " + tileset + " not found in DG.ARC"; return false; }
    ByteView models = bpxFind(set, "MDL.BPX/MODEL.BPX");
    ByteView tex = bpxFind(set, "MDL.BPX/TP.BPX/TEX.PSPIP");
    PspiFile pspi;
    if (!models.valid() || !tex.valid() || !pspi.parse(tex)) { error = "unexpected tile set layout"; return false; }

    std::map<int, GLuint> texCache;
    auto texture = [&](int idx) -> GLuint {
        auto it = texCache.find(idx);
        if (it != texCache.end()) return it->second;
        Image img;
        GLuint t = pspi.decode(idx, img) ? r.uploadTexture(img) : 0;
        texCache[idx] = t;
        return t;
    };
    std::vector<NamedEntry> entries;
    parseBpx(models, entries);
    int failed = 0;
    library_.reserve(entries.size());
    for (const NamedEntry& e : entries) {
        Model m;
        if (!parsePspm({models.data + e.offset, e.size}, m)) { ++failed; continue; }
        Piece piece;
        piece.name = e.name.substr(0, e.name.rfind('.'));
        piece.boundsMin = {m.boundsMin[0], m.boundsMin[1], m.boundsMin[2]};
        piece.boundsMax = {m.boundsMax[0], m.boundsMax[1], m.boundsMax[2]};
        for (ModelMesh& mesh : m.meshes) {
            PieceMesh pm;
            pm.gpu = r.uploadMesh(mesh.triangles, texture(mesh.texture));
            pm.cpu = std::move(mesh.triangles);
            piece.meshes.push_back(std::move(pm));
        }
        library_.push_back(std::move(piece));
    }
    log::info("Tile set %s: %zu pieces (%d failed)", tileset.c_str(), library_.size(), failed);
    if (library_.empty()) { error = "no models could be parsed"; return false; }
    return true;
}

const Piece* OriginalWorld::findPiece(const std::string& name) const {
    for (const Piece& p : library_)
        if (p.name == name) return &p;
    return nullptr;
}

const Piece* OriginalWorld::findByShape(const std::string& prefix, const Vec3& mn, const Vec3& mx) const {
    auto near = [](const Vec3& a, const Vec3& b) {
        return std::fabs(a.x - b.x) < 0.5f && std::fabs(a.y - b.y) < 0.5f && std::fabs(a.z - b.z) < 0.5f;
    };
    const Piece* fallback = nullptr;
    for (const Piece& p : library_) {
        if (p.name.rfind(prefix, 0) != 0 || !near(p.boundsMin, mn) || !near(p.boundsMax, mx)) continue;
        if (p.name.back() != 'T') return &p; // prefer non-"T" variants (meaning of T unknown)
        if (!fallback) fallback = &p;
    }
    return fallback;
}

void OriginalWorld::addInstance(const Piece* p, const Mat4& unitsTransform) {
    if (!p) return;
    Mat4 toMetres = Mat4::translateScale({0, 0, 0}, {scale_, scale_, scale_});
    PieceInstance inst{p, toMetres * unitsTransform};
    for (const PieceMesh& m : p->meshes)
        for (size_t i = 0; i + 2 < m.cpu.size(); i += 3) {
            auto P = [&](size_t k) { return inst.transform.transformPoint({m.cpu[k].pos[0], m.cpu[k].pos[1], m.cpu[k].pos[2]}); };
            collision_.addTriangle(P(i), P(i + 1), P(i + 2));
        }
    instances_.push_back(inst);
}

void OriginalWorld::finishCollision() { collision_.build(2.0f); }

void OriginalWorld::showAllPieces() {
    instances_.clear();
    collision_.clear();
    for (const Piece& p : library_) addInstance(&p, Mat4::identity());
    finishCollision();
    Vec3 mn = collision_.boundsMin(), mx = collision_.boundsMax();
    spawn_ = {(mn.x + mx.x) * 0.5f, mx.y + 2.0f, (mn.z + mx.z) * 0.5f};
    spawnYaw_ = 0;
    description_ = tileset_ + " - all pieces (inspection)";
}

bool OriginalWorld::buildFromSnapshot(GameData& data, const std::string& snapshot, std::string& error) {
    RlvSnapshot snap;
    if (!parseRlvSnapshot(data.arcEntry("MAIN.ARC", snapshot + ".RLVSNAP"), snap)) {
        error = "snapshot " + snapshot + " not found or invalid";
        return false;
    }
    instances_.clear();
    collision_.clear();
    // Representative pieces chosen by shape (one 40-unit cell), so every tile set works.
    const float C = kCellUnits;
    const Piece* floorP = findByShape("GR_", {0, 0, 0}, {C, 0, C});
    const Piece* ceilP = findByShape("CE_", {0, C, 0}, {C, C, C});
    const Piece* wallP = findByShape("WA_", {0, 0, 0}, {C, C, 0});
    if (!floorP || !ceilP || !wallP) {
        error = "tile set lacks the representative floor/ceiling/wall pieces";
        return false;
    }
    bool haveSpawn = false;
    for (int l = 0; l < snap.layers; ++l)
        for (int z = 0; z < snap.depth; ++z)
            for (int x = 0; x < snap.width; ++x) {
                uint32_t c = snap.at(x, l, z);
                if (!c) continue;
                float x0 = x * C, y0 = l * C, z0 = z * C;
                // Cell bits (verified by layout): 0x10 floor, 0x20 ceiling, 1 north (-Z),
                // 2 east (+X), 4 west (-X), 8 south (+Z) wall.
                if (c & 0x10) {
                    addInstance(floorP, Mat4::rotateYTranslate(0, {x0, y0, z0}));
                    if (!haveSpawn && l == 0 && (c & 0xF) == 0) { // first interior floor cell
                        spawn_ = Vec3{x0 + C / 2, y0, z0 + C / 2} * scale_;
                        haveSpawn = true;
                    }
                }
                if (c & 0x20) addInstance(ceilP, Mat4::rotateYTranslate(0, {x0, y0, z0}));
                if (c & 1) addInstance(wallP, Mat4::rotateYTranslate(0, {x0, y0, z0}));
                if (c & 8) addInstance(wallP, Mat4::rotateYTranslate(kPi, {x0 + C, y0, z0 + C}));
                if (c & 4) addInstance(wallP, Mat4::rotateYTranslate(-kPi / 2, {x0, y0, z0}));
                if (c & 2) addInstance(wallP, Mat4::rotateYTranslate(kPi / 2, {x0 + C, y0, z0 + C}));
            }
    finishCollision();
    if (!haveSpawn) spawn_ = (collision_.boundsMin() + collision_.boundsMax()) * 0.5f;
    spawnYaw_ = kPi; // face +Z, into the room from its north-west interior corner
    description_ = snapshot + " (" + std::to_string(snap.width) + "x" + std::to_string(snap.layers) + "x" +
                   std::to_string(snap.depth) + " cells)";
    log::info("Assembled %s: %zu instances, %zu triangles", description_.c_str(), instances_.size(),
              collision_.triangleCount());
    return true;
}

void OriginalWorld::unload(Renderer& r) {
    for (Piece& p : library_)
        for (PieceMesh& m : p.meshes) r.destroyMesh(m.gpu);
    library_.clear();
    instances_.clear();
    collision_.clear();
}

void OriginalWorld::draw(Renderer& r) const {
    static const float white[4] = {1, 1, 1, 1};
    for (const PieceInstance& inst : instances_)
        for (const PieceMesh& m : inst.piece->meshes) r.drawMesh(m.gpu, inst.transform, white);
}

} // namespace ca
