# MCXRInput SuperDepth3D preset

`MCXRInput-SuperDepth3D-150-040.ini` is a conservative calibration checkpoint
for SuperDepth3D v5.3.8 when the unified OpenXR bridge is run with a 150-degree
source vertical FOV and a 0.40 world-view scale.

It preserves SuperDepth3D's Minecraft depth profile and the bridge's expected
half-SBS eye routing. In particular, ReShade `FoV`, `IPD`, `Perspective`,
`Theater_Mode`, and barrel distortion remain disabled because those
image-space transforms would invalidate the bridge's projection calibration.

Relative to the user's original preset, this checkpoint:

- uses Normal occlusion quality and the shader's default halo-reduction level;
- enables near-object halo reduction for the held item/hand;
- modestly increases de-artifacting for Minecraft foliage and block edges;
- restores zero-parallax distance from 0.09 to the installed Minecraft/FPS
  profile's comfort-first 0.025 baseline. Zero-parallax distance controls the
  convergence plane; it is intentionally not scaled with the bridge's separate
  angular world-view scale.

Built-in sharpening remains off for the first hardware checkpoint because it
can amplify stereo halos, shimmer, and HUD aliasing. It should be evaluated as
a separate change only if the headset image is visibly soft.

Keep ReShade Performance Mode off while visually validating the profile so
settings remain easy to adjust. Test in singleplayer first. This display preset
does not imply that any multiplayer server permits MCXRInput.
