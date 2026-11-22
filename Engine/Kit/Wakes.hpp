// Wakes.hpp
// What floats leaves on the water. Each thing with a Buoyancy that is in the water (a boat, a
// buoy, a crate) is followed: where it is at the waterline, how large (its Buoyancy's hull, or
// else its collider), which way it heads, how fast, and where its stern has been over the last
// few seconds. The water draws from that (Water.metal): foam where the water meets it, thrown
// up at the bow as it moves; behind it the churned white trail it leaves, spreading and fading;
// and the two arms of its wake, opening at the angle every wake opens at.
//
// The world's, not the scene's: World::wakes, made each frame the effects run.
#pragma once

namespace molten {
class World;
}

namespace molten::kit {

/// One frame of the wakes: what floats followed, its trail laid down. `dt` in seconds.
void updateWakes(World& world, float dt);

}  // namespace molten::kit
