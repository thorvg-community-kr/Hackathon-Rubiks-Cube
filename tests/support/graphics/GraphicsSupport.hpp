#pragma once

#include <array>
#include <cstdint>

#include "CubeSupport.hpp"
#include "graphics/3d/Camera.hpp"
#include "graphics/Layout.hpp"
#include "graphics/3d/OrbitCamera.hpp"
#include "graphics/Rect.hpp"

/**
 * What the tests share from graphics upwards.
 *
 * May include graphics/, math/ and the cube helpers below it, and nothing
 * above: tests/graphics, tests/interaction and tests/app are given this
 * directory, and tests/cube and tests/solver are not. What only the
 * application's tests need stays in tests/app/EngineLifecycle.hpp.
 *
 * No Catch2 in here either, for the same reason as below.
 */
namespace rubiks::test {

/** One frame of elapsed time, at the rate a display would deliver them. */
inline constexpr double kFrameMs = 16.0;

/**
 * The side of the square canvas most pointer tests work on.
 *
 * Large enough that a sticker of a small cube is many pixels across, so a
 * point a test aims at is nowhere near the edge of what it is aiming for.
 */
inline constexpr std::uint32_t kCanvas = 1024;

/** The region the 3D cube is drawn in, on a canvas showing both views. */
[[nodiscard]] inline graphics::Rect cube_rect(
    std::uint32_t width = kCanvas, std::uint32_t height = kCanvas) noexcept
{
    return graphics::layout(width, height).cube;
}

/**
 * The home viewpoint looking into that region.
 *
 * The same call the application makes for a cube nobody has turned the view
 * of, so a test presses through the camera a resting cube is drawn with.
 */
[[nodiscard]] inline graphics::Camera cube_camera(
    std::uint32_t width = kCanvas, std::uint32_t height = kCanvas) noexcept
{
    return graphics::camera_into(graphics::home_orbit(),
                                 cube_rect(width, height));
}

/** A point as fractions of a region, measured from its top left. */
struct Sample {
    float x;
    float y;
};

// Where the home viewpoint shows what on a solved 3x3x3, as fractions of the
// square 3D region, which keeps them valid at any canvas aspect. Written here
// by hand from docs/tasks/04-rubiks-cube-domain.md rather than read from the
// engine, so a change to the camera, the layout or the stickers' gap has to
// move them deliberately -- and web/tests/e2e/sceneContract.ts with them.

/** The middles of the three visible faces' centre stickers. */
inline constexpr Sample kUpSample{0.50f, 0.29f};
inline constexpr Sample kFrontSample{0.31f, 0.61f};
inline constexpr Sample kRightSample{0.69f, 0.61f};

/**
 * The middles of three gaps between stickers, projected from the home
 * viewpoint: the edges between cubies, x or y = 1/3 on the cube's face. The
 * line there is two cubies' borders, 2 * kCubieBorderShare of a cubie wide.
 */
inline constexpr std::array<Sample, 3> kSeamSamples{{
    {0.37144f, 0.64844f},  // +Z, between the center and right stickers
    {0.30717f, 0.53711f},  // +Z, between the center and top stickers
    {0.56428f, 0.31445f},  // +Y, between the center and right stickers
}};

}  // namespace rubiks::test
