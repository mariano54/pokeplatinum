#ifndef POKEPLATINUM_TOUCH_PROJECTION_H
#define POKEPLATINUM_TOUCH_PROJECTION_H

#include <nitro/fx/fx.h>

#include "camera.h"

// Maps touch screen positions to positions on the ground of the field map,
// from a snapshot of the camera taken when created.
typedef struct TouchProjection {
    u16 fovY;
    u16 fovX;
    VecFx32 cameraDistance;
    VecFx32 groundNormal;
    fx32 cameraHeight;
    MtxFx43 cameraPitch;
    Camera *camera;
    VecFx32 unused;
} TouchProjection;

VecFx32 TouchProjection_GetGroundPosition(u16 touchX, u16 touchY, const TouchProjection *projection);
TouchProjection *TouchProjection_New(Camera *const camera);
void TouchProjection_Free(TouchProjection **projection);

#endif // POKEPLATINUM_TOUCH_PROJECTION_H
