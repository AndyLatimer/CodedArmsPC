#include "assets/Formats.h"
#include <algorithm>
#include <cctype>
#include <cstring>

namespace ca {

float rdf(const uint8_t* p) {
    uint32_t u = rd32(p);
    float f;
    std::memcpy(&f, &u, 4);
    return f;
}

// ---- ISO 9660 ------------------------------------------------------------------

static constexpr uint32_t kIsoSector = 2048;

IsoImage::~IsoImage() {
    if (f_) std::fclose(f_);
}

bool IsoImage::readSectors(uint32_t lba, uint32_t bytes, Bytes& out) {
    out.resize(bytes);
    if (std::fseek(f_, long(lba) * long(kIsoSector), SEEK_SET) != 0) return false;
    return std::fread(out.data(), 1, bytes, f_) == bytes;
}

bool IsoImage::open(const std::string& path) {
    f_ = std::fopen(path.c_str(), "rb");
    if (!f_) return false;
    Bytes pvd;
    if (!readSectors(16, kIsoSector, pvd)) return false;
    if (pvd[0] != 1 || std::memcmp(&pvd[1], "CD001", 5) != 0) return false;
    volumeId_.assign(reinterpret_cast<const char*>(&pvd[40]), 32);
    volumeId_.erase(volumeId_.find_last_not_of(' ') + 1);
    rootLba_ = rd32(&pvd[156 + 2]);
    rootSize_ = rd32(&pvd[156 + 10]);
    return true;
}

static bool iequal(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (std::toupper((unsigned char)a[i]) != std::toupper((unsigned char)b[i])) return false;
    return true;
}

bool IsoImage::readFile(const std::string& path, Bytes& out) {
    if (!f_) return false;
    uint32_t lba = rootLba_, size = rootSize_;
    size_t start = 0;
    while (start < path.size()) {
        size_t slash = path.find('/', start);
        std::string part = path.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
        start = slash == std::string::npos ? path.size() : slash + 1;
        Bytes dir;
        if (!readSectors(lba, size, dir)) return false;
        bool found = false;
        for (size_t p = 0; p < dir.size();) {
            uint8_t len = dir[p];
            if (len == 0) { // records never cross sectors: skip to next sector
                p = (p / kIsoSector + 1) * kIsoSector;
                continue;
            }
            uint8_t nameLen = dir[p + 32];
            std::string name(reinterpret_cast<const char*>(&dir[p + 33]), nameLen);
            size_t semi = name.find(';');
            if (semi != std::string::npos) name.resize(semi);
            if (iequal(name, part)) {
                lba = rd32(&dir[p + 2]);
                size = rd32(&dir[p + 10]);
                found = true;
                break;
            }
            p += len;
        }
        if (!found) return false;
    }
    return readSectors(lba, size, out);
}

// ---- ARC -------------------------------------------------------------------------

bool parseArc(const Bytes& arc, std::vector<NamedEntry>& out) {
    out.clear();
    if (arc.size() < 0x800 || std::memcmp(arc.data(), "KCEO ARCDT 1.0", 14) != 0) return false;
    uint32_t count = rd32(&arc[0x10]), sector = rd32(&arc[0x14]), table = rd32(&arc[0x18]);
    if (table != count * 36 || 0x800 + size_t(table) > arc.size()) return false;
    for (uint32_t i = 0; i < count; ++i) {
        const uint8_t* e = &arc[0x800 + i * 36];
        NamedEntry n;
        n.name.assign(reinterpret_cast<const char*>(e), strnlen(reinterpret_cast<const char*>(e), 24));
        n.offset = rd32(e + 24) * sector;
        n.size = rd32(e + 28);
        if (size_t(n.offset) + n.size > arc.size()) return false;
        out.push_back(n);
    }
    return true;
}

// ---- BPX -------------------------------------------------------------------------

bool parseBpx(ByteView v, std::vector<NamedEntry>& out) {
    out.clear();
    if (!v.valid() || v.size < 0x2c || std::memcmp(v.data, "BPX\0", 4) != 0) return false;
    uint32_t count = rd32(v.data + 0x18), names = rd32(v.data + 0x20);
    if (size_t(names) + size_t(count) * 32 > v.size) return false;
    for (uint32_t i = 0; i < count; ++i) {
        const uint8_t* e = v.data + names + i * 32;
        NamedEntry n;
        n.name.assign(reinterpret_cast<const char*>(e), strnlen(reinterpret_cast<const char*>(e), 24));
        n.offset = rd32(e + 24);
        n.size = rd32(e + 28);
        if (size_t(n.offset) + n.size > v.size) return false;
        out.push_back(n);
    }
    return true;
}

ByteView bpxFind(ByteView v, const std::string& path) {
    size_t start = 0;
    std::vector<NamedEntry> entries;
    while (start < path.size()) {
        size_t slash = path.find('/', start);
        std::string part = path.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
        start = slash == std::string::npos ? path.size() : slash + 1;
        if (!parseBpx(v, entries)) return {};
        auto it = std::find_if(entries.begin(), entries.end(), [&](const NamedEntry& e) { return iequal(e.name, part); });
        if (it == entries.end()) return {};
        v = {v.data + it->offset, it->size};
    }
    return v;
}

// ---- PSPI ------------------------------------------------------------------------

bool PspiFile::parse(ByteView v) {
    images_.clear();
    palettes_.clear();
    if (!v.valid() || v.size < 0x10 || std::memcmp(v.data, "PSPI01", 6) != 0) return false;
    v_ = v;
    int ni = rd16(v.data + 8), np = rd16(v.data + 10);
    if (0x10 + size_t(ni + np) * 24 > v.size) return false;
    for (int i = 0; i < ni; ++i) {
        const uint8_t* r = v.data + 0x10 + i * 24;
        PspiImageInfo info;
        info.index = rd16(r);
        info.palette = rd16(r + 2);
        info.width = rd16(r + 4);
        info.height = rd16(r + 6);
        info.bpp = int(rd32(r + 8));
        info.dataOffset = rd32(r + 16);
        images_.push_back(info);
    }
    for (int i = 0; i < np; ++i) {
        const uint8_t* r = v.data + 0x10 + (ni + i) * 24;
        palettes_.push_back({rd16(r), rd16(r + 2), rd32(r + 4)});
    }
    return true;
}

bool PspiFile::decode(int i, Image& out) const {
    if (i < 0 || i >= imageCount()) return false;
    const PspiImageInfo& in = images_[size_t(i)];
    if (in.bpp != 4 && in.bpp != 8 && in.bpp != 32) return false;
    const int rowBytes = in.width * in.bpp / 8;
    const size_t bytes = size_t(rowBytes) * size_t(in.height);
    if (in.dataOffset + bytes > v_.size || rowBytes % 16 != 0 || in.height % 8 != 0) return false;

    // Undo the PSP texture swizzle: data is stored as 16-byte x 8-row blocks.
    std::vector<uint8_t> linear(bytes);
    const uint8_t* src = v_.data + in.dataOffset;
    for (int by = 0; by < in.height; by += 8)
        for (int bx = 0; bx < rowBytes; bx += 16)
            for (int r = 0; r < 8; ++r, src += 16)
                std::memcpy(&linear[size_t(by + r) * rowBytes + bx], src, 16);

    const uint8_t* pal = nullptr;
    int palCount = 0;
    if (in.bpp != 32) {
        for (const Pal& p : palettes_)
            if (p.id == in.palette && p.offset + size_t(p.count) * 4 <= v_.size) {
                pal = v_.data + p.offset;
                palCount = p.count;
            }
        if (!pal) return false;
    }
    out.width = in.width;
    out.height = in.height;
    out.rgba.resize(size_t(in.width) * in.height * 4);
    for (int y = 0; y < in.height; ++y)
        for (int x = 0; x < in.width; ++x) {
            uint8_t* dst = &out.rgba[(size_t(y) * in.width + x) * 4];
            const uint8_t* row = &linear[size_t(y) * rowBytes];
            if (in.bpp == 32) {
                std::memcpy(dst, row + x * 4, 4);
                continue;
            }
            int idx = in.bpp == 8 ? row[x] : ((row[x >> 1] >> ((x & 1) * 4)) & 15);
            if (idx >= palCount) idx = 0;
            std::memcpy(dst, pal + idx * 4, 4);
        }
    return true;
}

// ---- RLVB ------------------------------------------------------------------------

bool parseRlvSnapshot(ByteView v, RlvSnapshot& out) {
    if (!v.valid() || v.size < 0x58 || std::memcmp(v.data, "RLVB", 4) != 0) return false;
    uint32_t grid = rd32(v.data + 0x2c);
    out.width = rd16(v.data + 0x30);
    out.layers = rd16(v.data + 0x32);
    out.depth = rd16(v.data + 0x34);
    size_t n = size_t(out.width) * out.layers * out.depth;
    if (n == 0 || grid + n * 4 > v.size) return false;
    out.cells.resize(n);
    for (size_t i = 0; i < n; ++i) out.cells[i] = rd32(v.data + grid + i * 4);
    return true;
}

// ---- GE vertices -------------------------------------------------------------------

static int compSize(int fmt, const int sizes[4]) { return sizes[fmt & 3]; }

int geVertexStride(uint32_t vtype) {
    static const int kTc[4] = {0, 1, 2, 4}, kNrm[4] = {0, 1, 2, 4}, kPos[4] = {0, 1, 2, 4}, kW[4] = {0, 1, 2, 4};
    static const int kCol[8] = {0, 0, 0, 0, 2, 2, 2, 4};
    int wfmt = (vtype >> 9) & 3, wcount = ((vtype >> 14) & 7) + 1;
    int tc = vtype & 3, col = (vtype >> 2) & 7, nrm = (vtype >> 5) & 3, pos = (vtype >> 7) & 3;
    if ((vtype >> 11) & 3) return 0;            // indexed drawing not supported (not seen in data)
    if (((vtype >> 18) & 7) != 0) return 0;     // morphing not supported
    if (pos == 0) return 0;
    int off = 0, align = 1;
    auto add = [&](int sz, int n) {
        if (!sz) return;
        off = (off + sz - 1) / sz * sz;
        off += sz * n;
        align = std::max(align, sz);
    };
    add(wfmt ? compSize(wfmt, kW) : 0, wcount);
    add(compSize(tc, kTc), 2);
    if (kCol[col]) add(kCol[col], 1);
    add(compSize(nrm, kNrm), 3);
    add(compSize(pos, kPos), 3);
    return (off + align - 1) / align * align;
}

bool geDecodeVertex(uint32_t vtype, const uint8_t* p, ModelVertex& out) {
    int wfmt = (vtype >> 9) & 3, wcount = ((vtype >> 14) & 7) + 1;
    int tc = vtype & 3, col = (vtype >> 2) & 7, nrm = (vtype >> 5) & 3, pos = (vtype >> 7) & 3;
    static const int kSz[4] = {0, 1, 2, 4};
    int off = 0;
    auto align = [&](int sz) { off = (off + sz - 1) / sz * sz; };
    auto readNum = [&](int fmt, bool isUnsigned) -> float {
        float v = 0;
        switch (fmt) {
        case 1: v = isUnsigned ? p[off] / 128.0f : int8_t(p[off]) / 127.0f; break;
        case 2: v = isUnsigned ? rd16(p + off) / 32768.0f : int16_t(rd16(p + off)) / 32767.0f; break;
        case 3: v = rdf(p + off); break;
        }
        off += kSz[fmt];
        return v;
    };
    if (wfmt) { align(kSz[wfmt]); off += kSz[wfmt] * wcount; }
    out = ModelVertex{};
    out.color = 0xFFFFFFFFu;
    if (tc) { align(kSz[tc]); out.uv[0] = readNum(tc, true); out.uv[1] = readNum(tc, true); }
    if (col == 7) {
        align(4);
        out.color = rd32(p + off);
        off += 4;
    } else if (col >= 4) {
        align(2);
        uint16_t c = rd16(p + off);
        off += 2;
        uint32_t r, g, b, a;
        if (col == 4) { r = (c & 31) * 255 / 31; g = ((c >> 5) & 63) * 255 / 63; b = ((c >> 11) & 31) * 255 / 31; a = 255; }
        else if (col == 5) { r = (c & 31) * 255 / 31; g = ((c >> 5) & 31) * 255 / 31; b = ((c >> 10) & 31) * 255 / 31; a = (c >> 15) ? 255 : 0; }
        else { r = (c & 15) * 17; g = ((c >> 4) & 15) * 17; b = ((c >> 8) & 15) * 17; a = ((c >> 12) & 15) * 17; }
        out.color = r | (g << 8) | (b << 16) | (a << 24);
    }
    if (nrm) { align(kSz[nrm]); for (float& n : out.normal) n = readNum(nrm, false); }
    if (!pos) return false;
    align(kSz[pos]);
    for (float& c : out.pos) c = readNum(pos, false);
    return true;
}

// ---- PSPM ------------------------------------------------------------------------

namespace {
struct StripHeader {
    uint32_t prim, vtype, count, unk0, unk1, bytes, unk2, trailer;
};
bool readStrip(ByteView v, size_t at, StripHeader& h) {
    if (at + 0x50 > v.size) return false;
    const uint8_t* p = v.data + at;
    h = {rd32(p), rd32(p + 4), rd32(p + 8), rd32(p + 12), rd32(p + 16), rd32(p + 20), rd32(p + 24), rd32(p + 28)};
    if (h.prim > 6 || h.count == 0 || h.count > 65535) return false;
    int stride = geVertexStride(h.vtype);
    if (stride == 0 || size_t(stride) * h.count != h.bytes) return false;
    return at + 0x50 + h.bytes <= v.size;
}
} // namespace

bool parsePspm(ByteView v, Model& out, std::string* error) {
    auto fail = [&](const char* msg) { if (error) *error = msg; return false; };
    out = Model{};
    if (!v.valid() || v.size < 0x60 || std::memcmp(v.data, "PSM\x02", 4) != 0) return fail("bad magic");
    for (int i = 0; i < 3; ++i) {
        out.boundsMin[i] = rdf(v.data + 8 + i * 4);
        out.boundsMax[i] = rdf(v.data + 20 + i * 4);
    }
    size_t meshAt = rd32(v.data + 0x38);
    while (meshAt + 0x40 <= v.size) {
        const uint8_t* m = v.data + meshAt;
        uint32_t meshSize = rd32(m + 0x28);
        if (meshSize == 0 || meshAt + meshSize > v.size) break;
        uint32_t stripCount = rd16(m + 0x32);
        ModelMesh mesh;
        mesh.flags = rd16(m + 0x38);    // material flags (meaning not decoded yet)
        mesh.texture = rd16(m + 0x3c);  // image index in the owning PSPI set (verified visually)
        // The first strip header follows a variable-size mesh header; locate it by
        // scanning 16-byte aligned positions for a self-consistent strip header.
        size_t at = meshAt + 0x40;
        StripHeader h;
        while (at < meshAt + meshSize && !readStrip(v, at, h)) at += 16;
        for (uint32_t s = 0; s < stripCount && at < meshAt + meshSize; ++s) {
            if (!readStrip(v, at, h)) return fail("bad strip header");
            std::vector<ModelVertex> verts(h.count);
            int stride = geVertexStride(h.vtype);
            for (uint32_t k = 0; k < h.count; ++k) geDecodeVertex(h.vtype, v.data + at + 0x50 + k * stride, verts[k]);
            auto emit = [&](const ModelVertex& a, const ModelVertex& b, const ModelVertex& c) {
                auto same = [](const ModelVertex& x, const ModelVertex& y) {
                    return std::memcmp(x.pos, y.pos, sizeof(x.pos)) == 0;
                };
                if (same(a, b) || same(b, c) || same(a, c)) return; // degenerate strip joint
                mesh.triangles.push_back(a);
                mesh.triangles.push_back(b);
                mesh.triangles.push_back(c);
            };
            if (h.prim == 3) {
                for (uint32_t k = 0; k + 2 < h.count; k += 3) emit(verts[k], verts[k + 1], verts[k + 2]);
            } else if (h.prim == 4) {
                for (uint32_t k = 0; k + 2 < h.count; ++k) {
                    if (k & 1) emit(verts[k + 1], verts[k], verts[k + 2]);
                    else emit(verts[k], verts[k + 1], verts[k + 2]);
                }
            } else if (h.prim == 5) {
                for (uint32_t k = 1; k + 1 < h.count; ++k) emit(verts[0], verts[k], verts[k + 1]);
            }
            at += 0x50 + h.bytes + h.trailer;
        }
        out.meshes.push_back(std::move(mesh));
        meshAt += meshSize;
    }
    if (out.meshes.empty()) return fail("no meshes");
    return true;
}

} // namespace ca
