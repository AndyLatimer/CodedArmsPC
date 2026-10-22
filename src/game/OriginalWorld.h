#pragma once
// Original level geometry from the user's game image.
//
// A stage tile set (DG.ARC/DG_xx.BPX) is a library of prefab pieces (floor GR_, ceiling
// CE_, wall WA_, door DO_, ...) authored on a 40-unit cell grid. Levels are cell grids:
// MAIN.ARC/DGxx_{L,M,S}_00P.RLVSNAP stores pre-generated ones (format in
// TECHNICAL_NOTES.md). Which exact piece the original picks for each cell is NOT decoded
// yet (TIBKLAY / cell bytes 1-2), so assembly uses one representative piece per cell
// feature: structure is original, piece choice is a reconstruction.
#include "assets/Formats.h"
#include "game/Collision.h"
#include "render/Renderer.h"
#include <map>
#include <string>
#include <vector>

namespace ca {

class GameData {
public:
    bool open(const std::string& isoPath);
    bool isOpen() const { return open_; }
    std::string isoPath() const { return path_; }
    ByteView arcEntry(const std::string& arc, const std::string& entry);
    std::vector<std::string> arcEntries(const std::string& arc);

private:
    bool loadArc(const std::string& arc);
    IsoImage iso_;
    bool open_ = false;
    std::string path_;
    std::map<std::string, Bytes> arcData_;
    std::map<std::string, std::vector<NamedEntry>> arcIndex_;
};

struct PieceMesh {
    GpuMesh gpu;
    std::vector<ModelVertex> cpu; // original units, kept for collision
};
struct Piece {
    std::string name;
    std::vector<PieceMesh> meshes;
    Vec3 boundsMin, boundsMax; // original units
};
struct PieceInstance {
    const Piece* piece = nullptr;
    Mat4 transform; // includes the units -> metres scale
};

class OriginalWorld {
public:
    static constexpr float kCellUnits = 40.0f; // verified: piece bounds are multiples of 40

    // Loads the piece library of a tile set ("DG_00").
    bool loadTileset(GameData& data, Renderer& r, const std::string& tileset, float unitsPerMeter, std::string& error);
    // Places every piece at its stored coordinates (inspection mode: pieces overlap).
    void showAllPieces();
    // Assembles the level stored in MAIN.ARC/<snapshot>.RLVSNAP.
    bool buildFromSnapshot(GameData& data, const std::string& snapshot, std::string& error);
    void unload(Renderer& r);
    void draw(Renderer& r) const;

    const CollisionWorld& collision() const { return collision_; }
    Vec3 suggestedSpawn() const { return spawn_; }
    float suggestedYaw() const { return spawnYaw_; }
    std::string tileset() const { return tileset_; }
    std::string description() const { return description_; }
    size_t triangleCount() const { return collision_.triangleCount(); }
    size_t instanceCount() const { return instances_.size(); }

private:
    const Piece* findPiece(const std::string& name) const;
    // First piece with the given name prefix whose bounds match (within 0.5 units).
    const Piece* findByShape(const std::string& prefix, const Vec3& mn, const Vec3& mx) const;
    void addInstance(const Piece* p, const Mat4& unitsTransform);
    void finishCollision();

    std::vector<Piece> library_;
    std::vector<PieceInstance> instances_;
    CollisionWorld collision_;
    Vec3 spawn_;
    float spawnYaw_ = 0;
    std::string tileset_, description_;
    float scale_ = 1.0f;
};

bool loadPspiImage(ByteView pspi, int index, Image& out);

} // namespace ca
