# Orange frog expression assets

The current firmware uses the eight PNGs supplied by the user in
`小橙蛙表情套装.zip` (2026-10-13). The files are copied byte for byte;
no image generation, texture retouching or source-image resizing was performed.

| Embedded file | Supplied file |
| --- | --- |
| `frog_happy.png` | `01_happy_开心.png` |
| `frog_laugh.png` | `02_laugh_大笑.png` |
| `frog_surprised.png` | `03_surprised_惊讶.png` |
| `frog_shy.png` | `04_shy_害羞.png` |
| `frog_sad.png` | `05_sad_难过.png` |
| `frog_angry.png` | `06_angry_生气.png` |
| `frog_sleepy.png` | `07_sleepy_犯困.png` |
| `frog_wink.png` | `08_wink_眨眼.png` |

Seven sources are 477×385 RGBA. Sleepy is 491×459, with the same body
beginning at row 74 and additional Zs above it. Every body is displayed at the
same scale, 218/477, and anchor (11,44) on a 240×240 RGB565 canvas. During display
composition only, Sleepy's original Zs are clipped and its outside-body pixels
are masked against the common happy-body silhouette (the original lower Z has a
small tail that overlaps source row 74). Smaller brown Zs are drawn
near the head within the collision envelope. Its body is neither shifted nor
shrunk relative to the other expressions. The original PNG remains unchanged.

All eight PNGs are decoded once into PSRAM, plus one speaking-body cache, one
working frame and one transition-source snapshot:
11 × 240 × 240 × 2 = 1,267,200 bytes. There is no PNG decode during
animation. The former 1,272,050-byte source below is no longer embedded; the eight
new PNGs total 1,093,263 bytes.

Expression changes use a 360 ms smoothstep crossfade. A transition interrupted
by a new expression starts from a snapshot of the currently displayed mixed
frame, so repeated voice/motion changes cannot jump back to an earlier face.
Only the local 240×240 body sprite is mixed; position, elastic deformation and the
complete gravity-relative rotation then apply to the result as one object.
Blending explicitly decodes LGFX's byte-swapped RGB565 storage and mixes the
5/6/5-bit channels separately. The renderer stays active until it has painted the
final full-target frame. After settling it resumes the usual static/no-refresh
behavior; the microphone mouth envelope continues to update during the fade.

The soft body uses one affine matrix for the whole composited sprite, including
eyes, mouth and Zs. The motion model supplies spring-driven vertical stretch and
horizontal shear; the renderer applies their combination before the full angle
rotation. Old impact scaling is not stacked on top. A convex hull of every
nonzero-alpha body pixel across the supplied designs, plus display Z strokes
and 0.8 px source-raster padding, bounds the complete transformed silhouette to
120 px around the pivot. One uniform correction also caps the maximum singular
value at 1.10; it never clamps scale axes independently. This keeps deformation
continuous, gives clear squash/rebound without sharp skew, and leaves room for
rounding before the battery ring at radius 228. The original 124 px collision
envelope and 127 px redraw margin remain sufficient. The hull is code generated
from read-only pixel inspection; PNG assets stay unchanged. `pushAffine` uses the
same nearest-neighbour display path as the previous rotation/zoom; no new large
buffer or per-frame image decode is added.

The happy/talking/dizzy white eyes are replaced locally so the original static
pupils do not remain beneath moving pupils or spirals. The white circles are
about 1.7 displayed pixels larger than the supplied circles to cover the static
pupils where they originally overlap the white edges. The pupils retain the
supplied inward gaze, dark brown-black color and white glints. Other expressions
use their supplied eyelids, brows, cheeks and tears directly. Speech uses the
supplied low mouth position and laugh-mouth palette; its opening follows the
smoothed sound level. Only the roughly 30×9 pixel happy-mouth stroke is removed
in the separate startup speaking cache using adjacent same-column skin colors,
so there is no solid-color facial patch. The happy source and its cache are kept
intact, and silence restores its original closed-mouth design.

Dizziness draws two spiral pupils and three orbiting, filled gold five-point
stars. Angry faces retain their own brows and eyes even when dizzy stars remain.
The complete elliptical star orbit and the star points use the same soft body
matrix in the pet's local head coordinates through the full 360 degrees,
following the body's gradual gravity alignment and elastic settling. This also
keeps the orbit inside its local redraw envelope.
Star centers are limited to radius 219 around the physical screen center; their
5.8-pixel source tips remain inside the battery ring's inner radius 228 after the
bounded deformation and raster rounding. The body and small Z decorations fit
the existing 124-pixel collision envelope and dirty rectangle throughout the
elastic squash, rebound, shear and full rotation.

## Previous mouthless sprite (historical, not embedded)

`orange_character.png` is the user's supplied orange character, extracted with the built-in imagegen tool. It preserves the orange texture and white eyes, with transparent surroundings and no pupils. Firmware scales and decodes it once at startup; the black pupils are drawn separately and move gently only while microphone input contains sound.

The source is 1254×1254 RGBA. The two measured white-eye centres are (373.1286, 386.8764) and (881.8142, 387.0029). `character_asset.hpp` scales these to the 240×240 cached sprite. Pupil radius and displacement leave the pupils inside the white eyes. The 466×466 round-screen layout and collision envelope are shared in `pet_layout.h`; the alpha silhouette at this size fits a radius of 115.4 pixels before impact squashing, with a conservative 124-pixel collision radius including deformation and sampling margins.

Final image-edit prompt:

> Use case: background-extraction / precise-object-edit. Image 1 is the edit target: the exact orange textured frog-like blob mascot with two eyes. Create a clean sprite asset for a small AMOLED device. Preserve the original orange silhouette, the two rounded bumps on top, the wider rounded bottom, original mottled soft clay/watercolor texture and orange color, front-facing composition, and original proportions exactly. Remove the outer black background and the off-white rounded rectangle card completely, output actual transparent alpha around the mascot. Keep both off-white circular eyeballs in their original positions, but remove only the two black pupils and fill those regions seamlessly with the same off-white eye color: firmware will draw and animate black pupils separately. Keep eyes as plain solid off-white circles, no outlines, no inner shadows, no pupil remnants. No mouth, no arms, no accessories, no border, no text, no shadow outside the mascot. Crop composition tightly with a small transparent margin around the full mascot; center it in a square image, mascot about 88% of width. This is a faithful extraction and pupil removal, not a redesign.
