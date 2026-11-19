#include "graphics/3d/Passes.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

#include "graphics/3d/ConvexHull.hpp"

namespace rubiks::graphics {
namespace {

constexpr std::size_t kCorners = 4;

math::Vec3 to_vec3(const math::Vec4& v) noexcept
{
    return math::Vec3{v.x, v.y, v.z};
}

/** Twice the signed area of a quad; positive for counter-clockwise winding. */
float signed_area_doubled(const std::array<math::Vec2, kCorners>& points) noexcept
{
    float sum = 0.0f;
    for (std::size_t i = 0; i < kCorners; ++i) {
        const auto& current = points[i];
        const auto& next = points[(i + 1) % kCorners];
        sum += current.x * next.y - next.x * current.y;
    }
    return sum;
}

/** A piece with the same map applied to every corner of its box. */
template <typename From, typename To, typename Map>
PieceOf<To> map_piece(const PieceOf<From>& piece, Map&& map)
{
    PieceOf<To> result;
    for (std::size_t i = 0; i < piece.box.size(); ++i) result.box[i] = map(piece.box[i]);
    return result;
}

/** The same map applied to every point of a shadow group. */
template <typename From, typename To, typename Map>
std::optional<ShadowGroup<To>> map_shadow(
    const std::optional<ShadowGroup<From>>& shadow, Map&& map)
{
    if (!shadow) return std::nullopt;

    ShadowGroup<To> result;
    result.opacity = shadow->opacity;
    result.fade_start = map(shadow->fade_start);
    result.fade_end = map(shadow->fade_end);

    result.polygons.reserve(shadow->polygons.size());
    for (const auto& polygon : shadow->polygons) {
        std::vector<To> mapped;
        mapped.reserve(polygon.size());
        for (const auto& point : polygon) mapped.push_back(map(point));
        result.polygons.push_back(std::move(mapped));
    }

    return result;
}

/** Whether every point of a view-space shadow group is in front of the near plane. */
bool shadow_in_front(const ShadowGroup<math::Vec3>& shadow, float near_plane)
{
    const auto in_front = [near_plane](const math::Vec3& point) {
        return point.z < -near_plane;
    };
    for (const auto& polygon : shadow.polygons) {
        if (!std::all_of(polygon.begin(), polygon.end(), in_front)) return false;
    }
    return in_front(shadow.fade_start) && in_front(shadow.fade_end);
}

/**
 * How far inside a face plane, in view-space units, a corner on it may come
 * out once rounded.
 *
 * A distance, not a share of the box: the thinnest piece, one layer of the
 * largest cube, is a few hundredths across, and a tolerance that shrank with
 * it fell below the rounding of the view transform itself, so the plane two
 * thin pieces share stopped parting them. A thousandth is far above that
 * rounding and far below the thinnest piece.
 */
constexpr float kPartedTolerance = 1e-3f;

/**
 * Whether the eye is on `beyond`'s side of a face plane of `box` that parts
 * the two, or nothing when no face plane of `box` parts them -- or one does
 * but the eye is on it, within kPartedTolerance: seen edge on, the two cannot
 * cover each other, and either order is right.
 *
 * A face of a box is the four corners sharing one bit of their index (see
 * PieceOf::box), and since the box is a box its outward normal is the way from
 * the box's centre to the face's. `beyond` is parted by that plane when no
 * corner of it is inside by more than kPartedTolerance: two pieces of one
 * stack share a plane, and half the corners lie on it, give or take rounding.
 */
std::optional<bool> side_of_eye(const std::array<math::Vec3, 8>& box,
                                const std::array<math::Vec3, 8>& beyond)
{
    math::Vec3 centre{0.0f, 0.0f, 0.0f};
    for (const auto& corner : box) centre = centre + corner * 0.125f;

    for (const std::size_t bit : {std::size_t{4}, std::size_t{2}, std::size_t{1}}) {
        for (const std::size_t value : {std::size_t{0}, bit}) {
            math::Vec3 middle{0.0f, 0.0f, 0.0f};
            for (std::size_t i = 0; i < box.size(); ++i) {
                if ((i & bit) == value) middle = middle + box[i] * 0.25f;
            }
            const math::Vec3 normal = math::normalize(middle - centre);
            const bool parted = std::all_of(
                beyond.begin(), beyond.end(), [&](const math::Vec3& corner) {
                    return math::dot(corner - middle, normal) >= -kPartedTolerance;
                });
            if (!parted) continue;
            const float eye = math::dot(math::Vec3{0.0f, 0.0f, 0.0f} - middle, normal);
            if (std::abs(eye) <= kPartedTolerance) return std::nullopt;
            return eye > 0.0f;
        }
    }
    return std::nullopt;
}

}  // namespace

ViewScene ViewPass::operator()(const WorldScene& scene) const
{
    ViewScene result;
    result.faces.reserve(scene.faces.size());

    const auto to_view = [this](const math::Vec3& point) {
        return to_vec3(math::apply_point(view_matrix, point));
    };

    for (const auto& face : scene.faces) {
        ViewFace transformed;
        transformed.color = face.color;
        transformed.piece = face.piece;
        for (std::size_t i = 0; i < kCorners; ++i) {
            transformed.points[i] = to_view(face.points[i]);
        }
        result.faces.push_back(transformed);
    }

    result.shadow = map_shadow<math::Vec3, math::Vec3>(scene.shadow, to_view);

    // The pieces come along to become the bodies drawn under their faces.
    result.pieces.reserve(scene.pieces.size());
    for (const auto& piece : scene.pieces) {
        result.pieces.push_back(map_piece<math::Vec3, math::Vec3>(piece, to_view));
    }
    return result;
}

ViewPass view(const Camera& camera) noexcept
{
    return ViewPass{camera.view()};
}

ViewScene OrderPass::operator()(ViewScene scene) const
{
    const std::size_t count = scene.pieces.size();
    if (count < 2) return scene;

    // drawn_before[a][b]: a face plane of one parts a from b, and the eye --
    // the origin of view space -- is on b's side of it. Each pair is decided
    // once, from a's planes or else b's: asked from both, an eye on the plane
    // the two share could have its side rounded each way, and the two answers
    // would leave no order at all.
    std::vector<std::vector<bool>> drawn_before(count,
                                                std::vector<bool>(count, false));
    for (std::size_t a = 0; a < count; ++a) {
        for (std::size_t b = a + 1; b < count; ++b) {
            const auto& box_a = scene.pieces[a].box;
            const auto& box_b = scene.pieces[b].box;
            if (const auto beyond_a = side_of_eye(box_a, box_b)) {
                // The eye is beyond a's plane, where b lies, or it is not.
                drawn_before[*beyond_a ? a : b][*beyond_a ? b : a] = true;
            } else if (const auto beyond_b = side_of_eye(box_b, box_a)) {
                drawn_before[*beyond_b ? b : a][*beyond_b ? a : b] = true;
            }
        }
    }

    // Each step takes a piece that nothing left has to go before. A stack
    // always has one; were there none, the lowest index left goes, so the
    // result is a permutation whatever the geometry.
    std::vector<std::size_t> order;
    std::vector<bool> placed(count, false);
    order.reserve(count);
    while (order.size() < count) {
        std::size_t next = count;
        for (std::size_t candidate = 0; candidate < count && next == count;
             ++candidate) {
            if (placed[candidate]) continue;
            bool free = true;
            for (std::size_t other = 0; other < count && free; ++other) {
                free = placed[other] || other == candidate ||
                       !drawn_before[other][candidate];
            }
            if (free) next = candidate;
        }
        if (next == count) {
            next = static_cast<std::size_t>(
                std::find(placed.begin(), placed.end(), false) - placed.begin());
        }
        placed[next] = true;
        order.push_back(next);
    }

    std::vector<std::size_t> rank(count, 0);
    std::vector<PieceOf<math::Vec3>> ordered;
    ordered.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        rank[order[i]] = i;
        ordered.push_back(std::move(scene.pieces[order[i]]));
    }
    scene.pieces = std::move(ordered);
    for (auto& face : scene.faces) {
        if (face.piece < count) face.piece = rank[face.piece];
    }
    return scene;
}

OrderPass order_pieces() noexcept
{
    return OrderPass{};
}

ClipScene ProjectPass::operator()(const ViewScene& scene) const
{
    ClipScene result;
    result.faces.reserve(scene.faces.size());

    // w is the positive view-space depth, so this division is safe for every
    // point the near-plane guards below have let through.
    const auto to_ndc = [this](const math::Vec3& point) {
        const auto clip = math::apply_point(projection_matrix, point);
        return math::Vec2{clip.x / clip.w, clip.y / clip.w};
    };

    for (const auto& face : scene.faces) {
        ClipFace projected;
        projected.color = face.color;
        projected.piece = face.piece;

        float depth_sum = 0.0f;
        bool in_front = true;
        for (std::size_t i = 0; i < kCorners; ++i) {
            const auto& point = face.points[i];
            if (point.z >= -near_plane) {
                in_front = false;
                break;
            }
            projected.ndc[i] = to_ndc(point);
            depth_sum += point.z;
        }
        if (!in_front) continue;

        projected.depth = depth_sum / static_cast<float>(kCorners);

        if (face.shading) {
            // The stops are evenly spaced along the axis on the plane; here
            // each is projected and its place along the projected axis kept,
            // so the brightness read at a stop is drawn at that stop's pixel
            // rather than at an even share of the screen segment.
            const auto& shading = *face.shading;
            ShadingOf<math::Vec2> flat;
            flat.from = to_ndc(shading.from);
            flat.to = to_ndc(shading.to);
            flat.colors = shading.colors;
            const math::Vec2 axis{flat.to.x - flat.from.x, flat.to.y - flat.from.y};
            const float axis_length = axis.x * axis.x + axis.y * axis.y;
            const std::size_t count = shading.colors.size();
            if (count > 1 && axis_length > 0.0f) {
                flat.offsets.reserve(count);
                for (std::size_t i = 0; i < count; ++i) {
                    const float t = static_cast<float>(i) / static_cast<float>(count - 1);
                    const math::Vec2 stop = to_ndc(shading.from + (shading.to - shading.from) * t);
                    const float along = ((stop.x - flat.from.x) * axis.x +
                                         (stop.y - flat.from.y) * axis.y) / axis_length;
                    flat.offsets.push_back(std::clamp(along, 0.0f, 1.0f));
                }
            }
            projected.shading = std::move(flat);
        }
        // A highlight lies on the face's plane, and its footprint can be far
        // wider than the face. Under perspective a footprint like that is not
        // an ellipse on screen, and an ellipse fitted through its projected
        // rims would put the far side of the lobe, compressed, onto the
        // stickers. So the footprint is carried through the tangent of the
        // projection at the glint's anchor on the face: exact where the glint
        // is seen, and never asking a rim behind the eye to project. Only the
        // anchor and its two tangent steps are projected.
        for (const auto& glint : face.highlights) {
            constexpr float kTangentStep = 0.25f;
            const math::Vec3& anchor = glint.anchor;
            const math::Vec3 step_u = anchor + glint.axis_u * kTangentStep;
            const math::Vec3 step_v = anchor + glint.axis_v * kTangentStep;
            if (anchor.z >= -near_plane || step_u.z >= -near_plane ||
                step_v.z >= -near_plane) {
                continue;
            }
            const math::Vec2 origin = to_ndc(anchor);
            const math::Vec2 along_u = to_ndc(step_u);
            const math::Vec2 along_v = to_ndc(step_v);
            const auto flatten = [&](const math::Vec3& point) {
                const math::Vec3 d = point - anchor;
                const float cu = math::dot(d, glint.axis_u) / kTangentStep;
                const float cv = math::dot(d, glint.axis_v) / kTangentStep;
                return math::Vec2{
                    origin.x + cu * (along_u.x - origin.x) + cv * (along_v.x - origin.x),
                    origin.y + cu * (along_u.y - origin.y) + cv * (along_v.y - origin.y)};
            };
            HighlightOf<math::Vec2> flat;
            flat.centre = flatten(glint.centre);
            for (std::size_t i = 0; i < glint.rims.size(); ++i) {
                flat.rims[i] = flatten(glint.rims[i]);
            }
            flat.stops = glint.stops;
            projected.highlights.push_back(flat);
        }
        result.faces.push_back(projected);
    }

    // The shadow is one thing: a polygon of it reaching the near plane cannot
    // be dropped on its own without tearing the outline, so it goes whole.
    if (scene.shadow && shadow_in_front(*scene.shadow, near_plane)) {
        result.shadow = map_shadow<math::Vec3, math::Vec2>(scene.shadow, to_ndc);
    }

    // A piece whose box reaches the near plane goes whole, like a face, and
    // keeps its place so the faces' pieces still name the right ones.
    const float limit = -near_plane;
    result.pieces.reserve(scene.pieces.size());
    for (const auto& piece : scene.pieces) {
        const bool in_front = std::all_of(
            piece.box.begin(), piece.box.end(),
            [limit](const math::Vec3& corner) { return corner.z < limit; });
        if (!in_front) {
            result.pieces.emplace_back(std::nullopt);
            continue;
        }
        result.pieces.emplace_back(map_piece<math::Vec3, math::Vec2>(piece, to_ndc));
    }
    return result;
}

ProjectPass project(const Camera& camera) noexcept
{
    return ProjectPass{camera.projection(), camera.near_plane()};
}

ClipScene CullPass::operator()(ClipScene scene) const
{
    auto& faces = scene.faces;
    faces.erase(std::remove_if(faces.begin(), faces.end(),
                               [](const ClipFace& face) {
                                   return signed_area_doubled(face.ndc) <= 0.0f;
                               }),
                faces.end());
    return scene;
}

CullPass cull() noexcept
{
    return CullPass{};
}

ClipScene DepthSortPass::operator()(ClipScene scene) const
{
    // Ascending view-space z is back to front: z is negative in front of the
    // camera and decreases with distance.
    std::stable_sort(scene.faces.begin(), scene.faces.end(),
                     [](const ClipFace& a, const ClipFace& b) {
                         if (a.piece != b.piece) return a.piece < b.piece;
                         return a.depth < b.depth;
                     });
    return scene;
}

DepthSortPass depth_sort() noexcept
{
    return DepthSortPass{};
}

RenderScene ViewportPass::operator()(const ClipScene& scene) const
{
    RenderScene result;
    result.faces.reserve(scene.faces.size());

    const auto to_pixels = [this](const math::Vec2& ndc) {
        return ndc_to_pixels(rect, ndc);
    };

    for (const auto& face : scene.faces) {
        RenderFace mapped;
        mapped.color = face.color;
        for (std::size_t i = 0; i < kCorners; ++i) {
            mapped.points[i] = to_pixels(face.ndc[i]);
        }
        if (face.shading) {
            mapped.shading = RenderShading{to_pixels(face.shading->from),
                                           to_pixels(face.shading->to),
                                           face.shading->colors,
                                           face.shading->offsets};
        }
        for (const auto& glint : face.highlights) {
            RenderHighlight pixels;
            for (std::size_t i = 0; i < glint.rims.size(); ++i) {
                pixels.rims[i] = to_pixels(glint.rims[i]);
            }
            pixels.stops = glint.stops;
            mapped.highlights.push_back(pixels);
        }
        result.faces.push_back(mapped);
    }

    if (const auto mapped =
            map_shadow<math::Vec2, math::Vec2>(scene.shadow, to_pixels)) {
        RenderShadow shadow;
        shadow.polygons = mapped->polygons;
        shadow.fade_start = mapped->fade_start;
        shadow.fade_end = mapped->fade_end;
        shadow.opacity = mapped->opacity;
        shadow.blur_sigma = kShadowBlurShare * rect.width;
        shadow.clip = clip;
        result.shadow = std::move(shadow);
    }

    // The bodies: a convex box's outline on screen is the hull of its corners,
    // and it goes before the first face of its own piece. The faces arrive
    // grouped by piece in the pieces' order, so that is a running count.
    result.bodies.reserve(scene.pieces.size());
    std::size_t first_face = 0;
    for (std::size_t index = 0; index < scene.pieces.size(); ++index) {
        while (first_face < scene.faces.size() &&
               scene.faces[first_face].piece < index) {
            ++first_face;
        }
        const auto& piece = scene.pieces[index];
        if (!piece) continue;
        std::vector<math::Vec2> corners;
        corners.reserve(piece->box.size());
        for (const auto& corner : piece->box) corners.push_back(to_pixels(corner));
        result.bodies.push_back(
            RenderBody{convex_hull(std::move(corners)), first_face});
    }
    return result;
}

ViewportPass viewport(const Rect& rect, const Rect& stage) noexcept
{
    return ViewportPass{rect, stage};
}

}  // namespace rubiks::graphics
