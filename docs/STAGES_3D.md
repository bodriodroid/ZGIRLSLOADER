# 3D stages

A 3D stage folder (`<stage>/`, named like its `.lvl`) holds `background.sgi.msb`, one `<mesh>.sgm.msb` per mesh and, in the Core's texture folders, `<material>.dds`.
All numbers in those files are big-endian, integers are 8 bytes wide and strings are an 8-byte length + the bytes. The readers (`src/core/sgscene.h`, `src/core/sgmesh.h`) have the exact layouts;
they were checked against every file of the stage folders of the test data (29 scenes in versions 2.0 / 2.1, 1793 meshes).

## What is drawn

* `Stage3D` (`src/core/stage3d.*`, no platform code) turns the scene into world-space triangle lists, one per mesh instance (the instance's 4x4 matrix is row-vector: `p' = [x y z 1] * M`).
* The instance named `FightPlane` is not drawn: it is the quad that **is** the stage's front view. Its width in mesh units gives the scale (`StageSize.x / width`: 16 stage units per mesh unit on the newer scenes, ~14.3 on the oldest),
  its lower edge is the bottom of the 2D picture, and the fighters' floor line is `BottomClearance` stage units above it.
* `fitCamera()` places a pinhole camera (vertical FOV from the `.lvl`'s `CAMERA` line, looking down -z) so that this quad lands where the 2D fight camera puts the stage:
  zoom `s` = pixels per stage unit on the plane, pan `cx`, floor row `floorY`. Nothing is hard coded per stage.
* `render()` projects, clips against the near plane, sorts meshes far to near by the depth of their box centre and the triangles of each mesh far to near, and splits triangles that perspective would bend
  (the renderer interpolates texture coordinates linearly on screen) until their edges are shorter than 40 px. Meshes whose texture coordinates run well past 0..1 (floors, water, fog) are cut along the whole numbers
  so the repeating texture works with a renderer that only samples inside one copy.
* `Stage3DGfx` (`src/app/stage3d_gfx.*`) loads the textures (worker thread), uploads them and draws one `SDL_RenderGeometry` call per mesh. `v` is flipped (`1 - v`) because the meshes store it upward.
  Colours are the textures' own: the stored vertex colours are almost all white (the few exceptions are black with partial alpha on the animated NPC meshes) and are not applied.

## Not done yet

Skeleton and animation files (`.sgs` / `.sga`): NPCs, smoke, screens and scrolling UVs stand still, skinned meshes are drawn in their bind pose. The `.lvl`'s lights and the `3D` tilt line are not used.
The meaning of the instance flag bytes is unknown (kept raw).

## Fallback and the developer option

"3D stages" (Developer Options): Off = the flat picture, **Auto** = 3D, and back to the picture for the rest of the match when the 3D pass keeps taking more than 9 ms per frame (about 2.5 s), On = always 3D.
Whatever the option says, a stage whose scene or textures are not in the mounted data (less than 60% of the triangles have a texture) keeps the picture.
`-DZDROID_TEST_STAGES=<folder>` makes the three test programs walk a real stage folder.
