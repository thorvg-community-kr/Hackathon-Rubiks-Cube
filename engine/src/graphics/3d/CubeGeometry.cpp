#include "graphics/3d/CubeGeometry.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

#include "graphics/Palette.hpp"
#include "cube/Surface.hpp"
#include "math/Angle.hpp"

namespace rubiks::graphics {
namespace {

using cube::Axis;
using cube::Face;
using cube::in_layers;
using math::Vec3;

/** Corner offsets in units of half_extent, wound outward-facing. */
using Corners = std::array<Vec3, 4>;

constexpr float kH = 1.0f;

[[nodiscard]] Corners corners_of(Face face) noexcept
{
    switch (face) {
        case Face::Right:
            return {Vec3{kH, -kH, kH}, Vec3{kH, -kH, -kH}, Vec3{kH, kH, -kH},
                    Vec3{kH, kH, kH}};
        case Face::Left:
            return {Vec3{-kH, -kH, -kH}, Vec3{-kH, -kH, kH}, Vec3{-kH, kH, kH},
                    Vec3{-kH, kH, -kH}};
        case Face::Up:
            return {Vec3{-kH, kH, kH}, Vec3{kH, kH, kH}, Vec3{kH, kH, -kH},
                    Vec3{-kH, kH, -kH}};
        case Face::Down:
            return {Vec3{-kH, -kH, -kH}, Vec3{kH, -kH, -kH}, Vec3{kH, -kH, kH},
                    Vec3{-kH, -kH, kH}};
        case Face::Front:
            return {Vec3{-kH, -kH, kH}, Vec3{kH, -kH, kH}, Vec3{kH, kH, kH},
                    Vec3{-kH, kH, kH}};
        case Face::Back:
            break;
    }
    return {Vec3{kH, -kH, -kH}, Vec3{-kH, -kH, -kH}, Vec3{-kH, kH, -kH},
            Vec3{kH, kH, -kH}};
}

/** The cubie at one cell of a layer's cut face. */
[[nodiscard]] cube::CubiePosition cut_cubie(
    Axis axis, int slice, int across, int down) noexcept
{
    const std::array<Axis, 2> plane = cube::other_axes(axis);
    cube::CubiePosition position{0, 0, 0};
    cube::set_coordinate(position, axis, slice);
    cube::set_coordinate(position, plane[0], across);
    cube::set_coordinate(position, plane[1], down);
    return position;
}

/** One coordinate of a point, by axis. */
[[nodiscard]] float& coordinate(Vec3& point, Axis axis) noexcept
{
    switch (axis) {
        case Axis::X:
            return point.x;
        case Axis::Y:
            return point.y;
        case Axis::Z:
            break;
    }
    return point.z;
}

/** The bit of a box corner's index that says which end of `axis` it is at. */
[[nodiscard]] std::size_t box_bit(Axis axis) noexcept
{
    switch (axis) {
        case Axis::X:
            return 4;
        case Axis::Y:
            return 2;
        case Axis::Z:
            break;
    }
    return 1;
}

/**
 * Appends the piece that is the run of layers `first` to `last` along `axis`,
 * turned by `turn` when it is turning, and gives its index.
 *
 * Its box spans the run along the axis and the whole cube across it. At rest
 * that is the cube itself; during a turn the union of the runs is the cube at
 * that angle, so the union of their shadows is its shadow, whatever N is.
 */
std::size_t append_piece(WorldScene& scene, Axis axis, int first, int last,
                         int size, const std::optional<math::Quaternion>& turn)
{
    const float cubie_half = cubie_half_extent(size);
    const float low = cubie_center(first, size) - cubie_half;
    const float high = cubie_center(last, size) + cubie_half;

    PieceOf<Vec3> piece;
    for (std::size_t i = 0; i < piece.box.size(); ++i) {
        Vec3 corner{(i & 4) ? kCubeHalfExtent : -kCubeHalfExtent,
                    (i & 2) ? kCubeHalfExtent : -kCubeHalfExtent,
                    (i & 1) ? kCubeHalfExtent : -kCubeHalfExtent};
        coordinate(corner, axis) = (i & box_bit(axis)) ? high : low;
        piece.box[i] = corner;
    }

    if (turn) {
        for (auto& corner : piece.box) corner = math::quaternion_rotate(*turn, corner);
    }
    scene.pieces.push_back(std::move(piece));
    return scene.pieces.size() - 1;
}

}  // namespace

float cubie_half_extent(int size) noexcept
{
    return kCubeHalfExtent / static_cast<float>(size);
}

float sticker_half_extent(int size) noexcept
{
    // The border in from every edge of its cubie's face; the border is a
    // share of the whole edge, twice the half extent.
    return cubie_half_extent(size) * (1.0f - 2.0f * kCubieBorderShare);
}

Vec3 sticker_center(const cube::SurfaceSticker& sticker, int size) noexcept
{
    // The middle of its cubie's outward face.
    const Axis normal = cube::axis_of(sticker.face);
    const float plane = cube::is_positive_face(sticker.face) ? kCubeHalfExtent
                                                            : -kCubeHalfExtent;
    const auto place = [&](Axis axis, int index) {
        return axis == normal ? plane : cubie_center(index, size);
    };
    return Vec3{place(Axis::X, sticker.x), place(Axis::Y, sticker.y),
                place(Axis::Z, sticker.z)};
}

float cubie_center(int i, int size) noexcept
{
    // Pitch is 2 / N and the first center sits half a pitch in from the face.
    return kCubeHalfExtent * static_cast<float>(2 * i - (size - 1)) /
           static_cast<float>(size);
}

void append_sticker(WorldScene& scene, const Vec3& center, float half_extent,
                    Face face, const Color& color)
{
    const auto offsets = corners_of(face);
    // The corners are a unit cube's face, one unit out along the normal; that
    // unit is taken off, so the quad lies in the plane through `center`.
    const Vec3 normal = face_normal(face);

    WorldFace quad;
    quad.color = color;
    for (std::size_t i = 0; i < offsets.size(); ++i) {
        quad.points[i] = center + (offsets[i] - normal) * half_extent;
    }
    scene.faces.push_back(quad);
}

Vec3 axis_direction(Axis axis) noexcept
{
    switch (axis) {
        case Axis::X:
            return Vec3{1.0f, 0.0f, 0.0f};
        case Axis::Y:
            return Vec3{0.0f, 1.0f, 0.0f};
        case Axis::Z:
            break;
    }
    return Vec3{0.0f, 0.0f, 1.0f};
}

Vec3 face_normal(Face face) noexcept
{
    const Vec3 axis = axis_direction(cube::axis_of(face));
    return cube::is_positive_face(face) ? axis : axis * -1.0f;
}

math::Quaternion layer_rotation(Axis axis, float degrees) noexcept
{
    // The single place the domain's clockwise-from-the-positive-end convention
    // becomes a right-handed rotation.
    return math::quaternion_from_axis_angle(axis_direction(axis),
                                            math::to_radians(-degrees));
}

WorldScene build_cube_scene(cube::CubeView state, Palette palette)
{
    const int size = state.size();
    const float half_extent = sticker_half_extent(size);

    WorldScene scene;
    // Six faces of an N x N grid: 54 at N = 3.
    scene.faces.reserve(static_cast<std::size_t>(6 * size * size));
    // At rest the whole cube is one piece, and one box is one shadow.
    append_piece(scene, Axis::X, 0, size - 1, size, std::nullopt);

    for (const cube::SurfaceSticker sticker : cube::surface_cells(size)) {
        append_sticker(scene, sticker_center(sticker, size), half_extent,
                       sticker.face,
                       to_lit_color(state.sticker(sticker), palette));
    }
    return scene;
}

WorldScene build_cube_scene(cube::CubeView state,
                            const std::optional<ActiveRotation>& active,
                            Palette palette)
{
    if (!active) return build_cube_scene(state, palette);

    const int size = state.size();
    const float sticker_half = sticker_half_extent(size);
    // Half a cubie: a cut face is bare body as large as its cubie.
    const float cubie_half = cubie_half_extent(size);
    const math::Quaternion turn =
        layer_rotation(active->axis, active->angle_degrees);
    // The cut faces face along the turning axis: the lower layer's towards
    // its positive end, the upper layer's towards the end opposite.
    const std::array<Face, 2> cut_faces{cube::end_face(active->axis, true),
                                        cube::end_face(active->axis, false)};

    WorldScene scene;

    // One piece per unbroken run of layers along the turning axis, the
    // turning runs carried round with their stickers. Only this axis is cut:
    // the other two do not move in this turn, so there is nothing to split.
    // A run mask makes three at most -- still, turning, still -- and a mask
    // with gaps in it, which nothing here produces, makes one more per gap.
    // Every layer remembers which piece it is in.
    std::vector<std::size_t> piece_of(static_cast<std::size_t>(size), 0);
    int run_start = 0;
    for (int index = 1; index <= size; ++index) {
        const bool run_turning = in_layers(active->layers, run_start);
        if (index < size && in_layers(active->layers, index) == run_turning) {
            continue;
        }

        const std::size_t piece =
            append_piece(scene, active->axis, run_start, index - 1, size,
                         run_turning ? std::optional{turn} : std::nullopt);
        for (int layer = run_start; layer < index; ++layer) {
            piece_of[static_cast<std::size_t>(layer)] = piece;
        }
        run_start = index;
    }

    // Reserve for surface stickers and the two cut faces of a single interface.
    scene.faces.reserve(static_cast<std::size_t>(8 * size * size));

    /**
     * Appends one quad onto the piece its layer is in, carrying it round with
     * the layer when the layer moves.
     */
    const auto emit = [&](const Vec3& center, float half_extent, Face face,
                          const Color& color, int layer) -> void {
        append_sticker(scene, center, half_extent, face, color);
        WorldFace& quad = scene.faces.back();
        quad.piece = piece_of[static_cast<std::size_t>(layer)];
        if (!in_layers(active->layers, layer)) return;

        for (auto& point : quad.points) {
            point = math::quaternion_rotate(turn, point);
        }
    };

    for (const cube::SurfaceSticker sticker : cube::surface_cells(size)) {
        emit(sticker_center(sticker, size), sticker_half, sticker.face,
             to_lit_color(state.sticker(sticker), palette),
             cube::coordinate_on(active->axis, sticker));
    }

    /** The middle of a cubie's face towards the cut: on the cut itself. */
    const auto cut_center = [&](const cube::CubiePosition& cubie,
                                Face face) -> Vec3 {
        const Vec3 middle{cubie_center(cubie.x, size),
                          cubie_center(cubie.y, size),
                          cubie_center(cubie.z, size)};
        return middle + face_normal(face) * cubie_half;
    };

    // Only an interface between moving and still layers exposes a cut face.
    // Both sides of each interface are body geometry, independent of colours.
    for (int slice = 0; slice + 1 < size; ++slice) {
        if (in_layers(active->layers, slice) ==
            in_layers(active->layers, slice + 1)) {
            continue;
        }

        for (int down = 0; down < size; ++down) {
            for (int across = 0; across < size; ++across) {
                emit(cut_center(cut_cubie(active->axis, slice, across, down),
                                cut_faces[0]),
                     cubie_half, cut_faces[0], kBodyColor, slice);
                emit(cut_center(cut_cubie(active->axis, slice + 1, across, down),
                                cut_faces[1]),
                     cubie_half, cut_faces[1], kBodyColor, slice + 1);
            }
        }
    }
    return scene;
}

}  // namespace rubiks::graphics
