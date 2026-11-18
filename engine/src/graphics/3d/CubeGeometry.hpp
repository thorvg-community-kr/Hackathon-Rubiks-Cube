#pragma once

#include <optional>

#include "cube/Cube.hpp"
#include "cube/Face.hpp"
#include "cube/Surface.hpp"
#include "graphics/ActiveRotation.hpp"
#include "graphics/Color.hpp"
#include "graphics/Palette.hpp"
#include "graphics/3d/Scene.hpp"
#include "math/Quaternion.hpp"
#include "math/Types.hpp"

namespace rubiks::graphics {

/** Half the edge length of the whole cube, whatever N is. */
inline constexpr float kCubeHalfExtent = 1.0f;

/**
 * The gap between two stickers, as a share of one cubie's edge.
 *
 * Every sticker sits in the middle of its cubie's face, half a gap in from
 * each edge, and the body shows through the gaps. Between two stickers of a
 * face the two halves make one gap; where two faces meet at the cube's edge
 * the half on each face make one again; and on the silhouette, where only one
 * face is seen, the face's backing reaches another half gap past the edge
 * in the face's own plane to make the line the same width there too. All of
 * it is on the faces, so it foreshortens with them: a face seen edge on keeps
 * its colours, and no edge of the silhouette is heavier than another.
 */
inline constexpr float kStickerGapShare = 0.06f;

/** Half the edge of one cubie: the cut faces a turn exposes are this size. */
[[nodiscard]] float cubie_half_extent(int size) noexcept;

/** Half the edge of one sticker: its cubie's, less half a gap. */
[[nodiscard]] float sticker_half_extent(int size) noexcept;

/**
 * How far a backing reaches past the cube's edges, in its face's plane: half a
 * gap.
 *
 * Only past the cube's own edges: where a turn cuts the cube, a piece's
 * backings stop at the cut, so that two pieces never overlap.
 */
[[nodiscard]] float body_reach(int size) noexcept;

/** The centre of one surface sticker: the middle of its cubie's outward face. */
[[nodiscard]] math::Vec3 sticker_center(const cube::SurfaceSticker& sticker,
                                        int size) noexcept;

/** Center of the cubie at index `i` along one axis, for an N of `size`. */
[[nodiscard]] float cubie_center(int i, int size) noexcept;

/**
 * Appends one square quad centred on `center`, in the plane through it that
 * faces `face`, wound counter-clockwise as seen from outside.
 *
 * Back-face culling depends on that winding, so this function is the single
 * place the six corner orders are written down.
 */
void append_sticker(WorldScene& scene, const math::Vec3& center,
                    float half_extent, cube::Face face, const Color& color);

/** Unit vector along the positive direction of an axis. */
[[nodiscard]] math::Vec3 axis_direction(cube::Axis axis) noexcept;

/** Unit vector out of the cube through a face. */
[[nodiscard]] math::Vec3 face_normal(cube::Face face) noexcept;

/**
 * Rotation of a turning layer, in the domain's sign convention.
 *
 * CubeMove counts a positive turn clockwise seen from the positive end of the
 * axis, which is a negative right-handed rotation. That conversion happens
 * here and nowhere else, so picking, drag resolution and rendering cannot
 * disagree about which way a positive angle turns.
 */
[[nodiscard]] math::Quaternion layer_rotation(cube::Axis axis,
                                              float degrees) noexcept;

/**
 * Builds the world-space scene for a cube state at rest.
 *
 * Only stickers on the outside of the cube are emitted: a cubie's face is
 * visible exactly when that cubie sits in the outermost layer along the
 * face's axis. At N = 3 that is 54 quads, of which the fixed camera keeps 27.
 *
 * Beside them goes one piece, the whole cube: its box for the shadow pass to
 * project and for the body, and a backing under each of its six faces.
 */
[[nodiscard]] WorldScene build_cube_scene(cube::CubeView state,
                                          Palette palette = Palette::Classic);

/**
 * Builds the scene for a cube state with a layer turn in progress.
 *
 * Without a turn this calls the function above verbatim, so the resting
 * output stays the one the rendered scene contract was derived from rather
 * than whatever a zero-angle rotation happens to produce.
 *
 * With a turn, the stickers of the selected layers rotate about the cube
 * center, and the cut faces revealed at the boundary between turning and
 * still layers are filled with kBodyColor.
 *
 * The cube is cut along the turning axis into one piece per unbroken run of
 * layers, the turning runs rotated with their stickers. The union of their
 * boxes is the cube at this angle, which is what makes the shadow of the
 * union theirs; at an angle of zero it is the same solid as the resting cube
 * in more pieces. Every face names the piece its layer is in (see PieceOf).
 */
[[nodiscard]] WorldScene build_cube_scene(
    cube::CubeView state,
    const std::optional<ActiveRotation>& active,
    Palette palette = Palette::Classic);

}  // namespace rubiks::graphics
