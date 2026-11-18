#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "graphics/Color.hpp"
#include "graphics/RenderScene.hpp"
#include "math/Types.hpp"

/**
 * Intermediate representations flowing between pipeline passes.
 *
 * Each stage has its own type so a pass cannot be applied to the wrong space
 * by accident; the compiler rejects `view(camera)` applied to view-space data.
 */
namespace rubiks::graphics {

/** How many points a backing has; see PieceOf::backings. */
inline constexpr std::size_t kBackingPoints = 8;

/**
 * One rigid piece of the cube, in any space: the whole cube at rest, one run
 * of layers during a turn.
 *
 * Its box casts the shadow. The stickers cannot cast it themselves: they have
 * gaps between them, so their shadow would be a grid, and the shadow of a
 * convex box is the convex hull of the shadows of its eight corners.
 *
 * The box and the backings are also the piece's body, drawn in the body
 * colour straight before the piece's own faces: what shows in the gaps
 * between its stickers is its own body, and never a piece behind it.
 */
template <typename Point>
struct PieceOf {
    /**
     * The corners, numbered by three bits, one per edge direction of the box
     * (4 for x, 2 for y and 1 for z before any turn), set at the high end; so
     * the four corners sharing a bit's value are one face of the box.
     */
    std::array<Point, 8> box{};
    /**
     * A body-coloured backing under each outer face of the box, in the face's
     * plane: the face reaching `body_reach()` past the cube's edges, stopping
     * at a cut. It shows in the half gap round the outermost stickers, and
     * past the edge on the silhouette, where only that face is seen -- in the
     * face's plane, so it foreshortens with the face the way the gaps between
     * its stickers do. Wound like a face, and culled like one.
     *
     * Eight points: the rectangle with every corner cut across, so it stays
     * inside a 45-degree bevel round the box's edges -- no square spike past
     * the cube's corners, and no tab past a cut face turned away.
     */
    std::vector<std::array<Point, kBackingPoints>> backings;
};

/**
 * The shadow as one thing, carried through the passes beside the faces.
 *
 * Fade, opacity and the occluding silhouettes belong to the shadow as a
 * whole rather than to any one polygon of it, which is why this is a group
 * with a few points of its own and not a list of shadow faces. Every point in
 * it goes through the same transforms as a face corner.
 */
template <typename Point>
struct ShadowGroup {
    /** One convex polygon on the ground per piece. */
    std::vector<std::vector<Point>> polygons;
    /** Where the shadow is darkest: the cube's centre dropped onto the ground. */
    Point fade_start{};
    /** Where it has thinned to its floor: the centre cast from the light. */
    Point fade_end{};
    /** Strength before the renderer's own fade and blur, 0 meaning none. */
    std::uint8_t opacity = 0;
};

/** The glint on one face, as geometry rather than as a colour. */
template <typename Point>
struct HighlightOf {
    /**
     * The centre of the lobe's footprint on the plane -- an ellipse, since a
     * lobe seen at a slant reaches further one way than the other. Outside
     * the face when the glint only reaches into it.
     */
    Point centre{};
    /** The ends of the footprint's two axes: +u, -u, +v, -v from the centre. */
    std::array<Point, 4> rims{};
    /**
     * The lobe's strength at evenly spaced distances from the centre to the
     * rim, sampled from the lobe itself; the last is the rim and is zero.
     */
    std::array<std::uint8_t, kGlintStops> stops{};
    /**
     * Where the footprint is flattened for the screen: a point of the plane
     * on the cube face, with the plane's unit axes. The screen ellipse is
     * the footprint carried through the projection's tangent there, so it
     * is right where the glint is seen -- a footprint wider than the face is
     * far from affine under perspective, and fitting it through its own rims
     * would draw the far side of the lobe compressed onto the stickers.
     * Unused once flattened.
     */
    Point anchor{};
    Point axis_u{};
    Point axis_v{};
};

/**
 * How a face's brightness runs across it.
 *
 * The brightness function belongs to the plane: one axis from the key
 * light's foot through the middle of the cube face, spanning every face on
 * the plane, and one set of evenly spaced stops with the true brightness at
 * each. Every sticker on the plane carries that same axis and those same
 * stops; only the colours differ, each sticker's own colour at each stop's
 * brightness. That is what makes the shading run on across the seams -- the
 * same function under a red sticker and the green one beside it.
 */
template <typename Point>
struct ShadingOf {
    Point from{};
    Point to{};
    /** This face's colour at each stop, evenly spaced from `from` to `to`. */
    std::vector<Color> colors;
    /**
     * Where each stop lies along `from` to `to` once on screen, as a
     * fraction. Empty until the projection: the stops are evenly spaced on
     * the plane, and perspective does not keep them evenly spaced on the
     * screen, so a gradient that spaced them evenly there would put the
     * plane's brightness at the wrong pixels.
     */
    std::vector<float> offsets;
};

/** Quad in world space. */
struct WorldFace {
    std::array<math::Vec3, 4> points{};
    Color color{};
    /** The index of the piece the face lies on, in the scene's `pieces`. */
    std::size_t piece = 0;
};

struct WorldScene {
    std::vector<WorldFace> faces;
    std::vector<PieceOf<math::Vec3>> pieces;
    /** Filled in by the shadow pass; empty until then and when there is none. */
    std::optional<ShadowGroup<math::Vec3>> shadow;
};

/** Quad in view space, where the camera sits at the origin looking down -Z. */
struct ViewFace {
    std::array<math::Vec3, 4> points{};
    /** After lighting: the plane's colour where it comes nearest the cube's centre. */
    Color color{};
    /** Set by the lighting pass; absent for a face no lamp reaches. */
    std::optional<ShadingOf<math::Vec3>> shading;
    /** Set by the lighting pass: one per lamp whose glint reaches this face. */
    std::vector<HighlightOf<math::Vec3>> highlights;
    /** As in WorldFace. */
    std::size_t piece = 0;
};

struct ViewScene {
    std::vector<ViewFace> faces;
    /** Once ordered, in the order they are drawn: far first. */
    std::vector<PieceOf<math::Vec3>> pieces;
    std::optional<ShadowGroup<math::Vec3>> shadow;
};

/**
 * Quad after the perspective divide.
 *
 * Culling reads `ndc`, which still has +Y up so a front face has a positive
 * signed area. Depth sorting reads `piece` and then `depth`, the mean
 * view-space z of the four corners; it is negative in front of the camera and
 * decreases with distance.
 */
struct ClipFace {
    std::array<math::Vec2, 4> ndc{};
    float depth = 0.0f;
    Color color{};
    std::optional<ShadingOf<math::Vec2>> shading;
    std::vector<HighlightOf<math::Vec2>> highlights;
    std::size_t piece = 0;
};

struct ClipScene {
    std::vector<ClipFace> faces;
    /**
     * At the same index as in the view scene, so a face's `piece` still names
     * its own. A piece whose box reaches the near plane goes whole, like a
     * face, and is left empty: drawn as no body.
     */
    std::vector<std::optional<PieceOf<math::Vec2>>> pieces;
    std::optional<ShadowGroup<math::Vec2>> shadow;
};

}  // namespace rubiks::graphics
