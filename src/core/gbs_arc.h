// gbs_arc.h - geometry of an arc node ("CCRA") of a .gbs scene (no drawing here, so it can be tested).
//
// An arc node is a flat-coloured band bent along part of a circle; the HUD's super meter is made of them (one ring per stock, plus a
// dark groove and a white flash).  Everything about it is a number of the node (gbs_scene.h) that an animation can also move:
//
//   segments   how many flat pieces the sweep is cut into
//   radius     of the centre line; the band reaches radius +- thickness / 2
//   sweep      length of the band in degrees
//   start      where the band begins, degrees from the 3 o'clock direction, growing clockwise on the screen (y points down)
//   direction  1: the band runs clockwise from `start`;  anything else: it runs counter-clockwise, and the whole figure is turned half a
//              circle (so that "start" of the two directions are opposite ends of the same circle)
//   colour     0xAARRGGBB, the same for every piece
//
// The band is centred on the middle of the node's box.  An animation that plays the sweep from its full length down to 0 therefore shortens
// the band towards the point where it starts, which is how the game shows a filling meter: it puts the animation on the tick that matches
// the value.  (The numbers follow what the scene format's arc control does with its fields; this file is an independent implementation.)
#pragma once
#include <vector>

namespace zengine {

struct GbsArcStyle {
    int segments = 0;
    float radius = 0;
    float thickness = 0;
    float sweep = 0;
    float start = 0;
    bool clockwise = false;
};

struct GbsArcPoint {
    float x = 0, y = 0;
};

// Point of the band at fraction t (0 = where it starts, 1 = where the sweep ends) on the circle of radius r, relative to the centre.
GbsArcPoint gbsArcPoint(const GbsArcStyle& s, float t, float r);

// The band as triangles (3 points each, 6 per segment).  Empty when there is nothing to see: fewer than 2 segments, no thickness or no sweep.
std::vector<GbsArcPoint> gbsArcTriangles(const GbsArcStyle& s);

}  // namespace zengine
