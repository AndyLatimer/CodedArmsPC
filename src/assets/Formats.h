#pragma once
// Readers for the original Coded Arms (ULES-00124) data formats.
// Every layout here is documented, with the evidence used to confirm it, in
// TECHNICAL_NOTES.md. Fields whose meaning is still unknown are named unkN.
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace ca {

using Bytes = std::vector<uint8_t>;

inline uint16_t rd16(const uint8_t* p) { return uint16_t(p[0] | (p[1] << 8)); }
inline uint32_t rd32(const uint8_t* p) { return uint32_t(p[0] | (p[1] << 8) | (p[2] << 16) | (uint32_t(p[3]) << 24)); }
float rdf(const uint8_t* p);

// ---- ISO 9660 image (read-only) -------------------------------------------------
class IsoImage {
public:
    ~IsoImage();
    bool open(const std::string& path);
    // Path like "PSP_GAME/USRDIR/Arc/MAIN.ARC" (case-insensitive).
    bool readFile(const std::string& path, Bytes& out);
    std::string volumeId() const { return volumeId_; }

private:
    bool readSectors(uint32_t lba, uint32_t bytes, Bytes& out);
    FILE* f_ = nullptr;
    uint32_t rootLba_ = 0, rootSize_ = 0;
    std::string volumeId_;
};

// ---- Generic view over a byte range ------------------------------------------------
struct ByteView {
    const uint8_t* data = nullptr;
    size_t size = 0;
    bool valid() const { return data != nullptr; }
};

// ---- "KCEO ARCDT 1.0" archive (USRDIR/Arc/*.ARC) -----------------------------------
struct NamedEntry {
    std::string name;
    uint32_t offset = 0, size = 0;
};
bool parseArc(const Bytes& arc, std::vector<NamedEntry>& out);

// ---- "BPX" container (nested inside ARC entries and other BPX) ----------------------
bool parseBpx(ByteView v, std::vector<NamedEntry>& out);
// Resolves "A.BPX/B.BPX/C.BIN" inside a BPX. Returns an invalid view when missing.
ByteView bpxFind(ByteView v, const std::string& path);

// ---- "PSPI01" texture set ------------------------------------------------------------
struct PspiImageInfo {
    int index = 0, palette = -1, width = 0, height = 0, bpp = 0;
    uint32_t dataOffset = 0;
};
struct Image {
    int width = 0, height = 0;
    std::vector<uint8_t> rgba;
};
class PspiFile {
public:
    bool parse(ByteView v);
    int imageCount() const { return int(images_.size()); }
    const PspiImageInfo& info(int i) const { return images_[size_t(i)]; }
    bool decode(int i, Image& out) const;

private:
    ByteView v_;
    std::vector<PspiImageInfo> images_;
    struct Pal { int id; int count; uint32_t offset; };
    std::vector<Pal> palettes_;
};

// ---- "PSM\x02" model ----------------------------------------------------------------
struct ModelVertex {
    float pos[3];
    float normal[3];
    float uv[2];
    uint32_t color; // RGBA8, as stored by the GE (A in the high byte)
};
struct ModelMesh {
    int texture = -1;                    // index into the owning PSPI set (see notes)
    std::vector<ModelVertex> triangles;  // triangle list (strips already expanded)
};
struct Model {
    float boundsMin[3] = {}, boundsMax[3] = {};
    std::vector<ModelMesh> meshes;
};
bool parsePspm(ByteView v, Model& out, std::string* error = nullptr);

// Decodes one PSP GE vertex of the given vertex type (GE command VTYPE). Returns the
// stride in bytes, 0 for unsupported types. Exposed for tests.
int geVertexStride(uint32_t vtype);
bool geDecodeVertex(uint32_t vtype, const uint8_t* src, ModelVertex& out);

} // namespace ca
