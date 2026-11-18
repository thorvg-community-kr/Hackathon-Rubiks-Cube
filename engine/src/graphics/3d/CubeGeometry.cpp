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

/**
 * A rectangle facing `face`, centred on `center`, reaching `half` along each
 * axis in its plane; the component along the face's own axis is unused.
 */
[[nodiscard]] std::array<Vec3, 4> rectangle_on(Face face, const Vec3& center,
                                               const Vec3& half) noexcept
{
    const auto offsets = corners_of(face);
    // The corners are a unit cube's face, one unit out along the normal; that
    // unit is taken off, so the rectangle lies in the plane through `center`.
    const Vec3 normal = face_normal(face);
    std::array<Vec3, 4> points{};
    for (std::size_t i = 0; i < offsets.size(); ++i) {
        const Vec3 along = offsets[i] - normal;
        points[i] = Vec3{center.x + along.x * half.x, center.y + along.y * half.y,
                         center.z + along.z * half.z};
    }
    return points;
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

/**
 * A backing's rectangle with every corner that stands off the cube along both
 * of its edges cut across, `reach` back along each edge.
 *
 * Such a corner is where the reaches past two of the cube's edges meet, and
 * whole it would stand out past the cube's corner as a square spike. A corner
 * at a cut reaches one way only and stays whole, given twice so every backing
 * has the same eight points, in the rectangle's winding.
 */
[[nodiscard]] std::array<Vec3, kBackingPoints> cut_corners(
    const std::array<Vec3, 4>& rectangle, float reach) noexcept
{
    const float beyond = kCubeHalfExtent + 0.5f * reach;
    std::array<Vec3, kBackingPoints> points{};
    for (std::size_t i = 0; i < rectangle.size(); ++i) {
        const Vec3& corner = rectangle[i];
        const Vec3& before = rectangle[(i + rectangle.size() - 1) % rectangle.size()];
        const Vec3& after = rectangle[(i + 1) % rectangle.size()];
        const int off = (std::abs(corner.x) > beyond ? 1 : 0) +
                        (std::abs(corner.y) > beyond ? 1 : 0) +
                        (std::abs(corner.z) > beyond ? 1 : 0);
        const auto toward = [&](const Vec3& other) {
            const Vec3 edge = other - corner;
            return corner + edge * (reach / math::length(edge));
        };
        points[2 * i] = off == 2 ? toward(before) : corner;
        points[2 * i + 1] = off == 2 ? toward(after) : corner;
    }
    return points;
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
 *
 * A backing goes under each of its outer faces, reaching `body_reach()` past
 * the cube's edges and stopping at a cut. A cut has no backing: the body
 * there is the cut face, and two pieces never overlap.
 */
std::size_t append_piece(WorldScene& scene, Axis axis, int first, int last,
                         int size, const std::optional<math::Quaternion>& turn)
{
    const float cubie_half = cubie_half_extent(size);
    const float low = cubie_center(first, size) - cubie_half;
    const float high = cubie_center(last, size) + cubie_half;
    const bool low_outer = first == 0;
    const bool high_outer = last == size - 1;

    PieceOf<Vec3> piece;
    for (std::size_t i = 0; i < piece.box.size(); ++i) {
        Vec3 corner{(i & 4) ? kCubeHalfExtent : -kCubeHalfExtent,
                    (i & 2) ? kCubeHalfExtent : -kCubeHalfExtent,
                    (i & 1) ? kCubeHalfExtent : -kCubeHalfExtent};
        coordinate(corner, axis) = (i & box_bit(axis)) ? high : low;
        piece.box[i] = corner;
    }

    const float reach = body_reach(size);
    const float across = kCubeHalfExtent + reach;
    const float from = low - (low_outer ? reach : 0.0f);
    const float to = high + (high_outer ? reach : 0.0f);
    for (const Face face : cube::faces()) {
        const bool positive = cube::is_positive_face(face);
        Vec3 center{0.0f, 0.0f, 0.0f};
        Vec3 half{across, across, across};
        if (cube::axis_of(face) == axis) {
            // An end of the run: only the cube's own faces have a backing.
            if (!(positive ? high_outer : low_outer)) continue;
            coordinate(center, axis) = positive ? high : low;
        } else {
            center = face_normal(face) * kCubeHalfExtent;
            coordinate(center, axis) = 0.5f * (from + to);
            coordinate(half, axis) = 0.5f * (to - from);
        }
        piece.backings.push_back(
            cut_corners(rectangle_on(face, center, half), reach));
    }

    if (turn) {
        for (auto& corner : piece.box) corner = math::quaternion_rotate(*turn, corner);
        for (auto& backing : piece.backings) {
            for (auto& point : backing) point = math::quaternion_rotate(*turn, point);
        }
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
    // Half a gap in from every edge of its cubie.
    return cubie_half_extent(size) * (1.0f - kStickerGapShare);
}

float body_reach(int size) noexcept
{
    return kStickerGapShare * cubie_half_extent(size);
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
    WorldFace quad;
    quad.color = color;
    quad.points = rectangle_on(face, center,
                               Vec3{half_extent, half_extent, half_extent});
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
