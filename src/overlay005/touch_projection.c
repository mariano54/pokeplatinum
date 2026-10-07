#include "overlay005/touch_projection.h"

#include <nitro.h>
#include <string.h>

#include "camera.h"
#include "heap.h"

static void TouchProjection_Init(TouchProjection *projection, Camera *const camera);

// Casts a ray from the camera through the touched pixel and returns where it
// hits the ground plane (y = 0 relative to the camera target).
VecFx32 TouchProjection_GetGroundPosition(u16 touchX, u16 touchY, const TouchProjection *projection)
{
    s8 unused0, unused1;
    VecFx32 cameraTarget;
    VecFx32 groundPos;
    u8 pixelsFromCenterY, pixelsFromCenterX;
    u16 pitch, yaw;

    groundPos.y = 0;
    cameraTarget = Camera_GetTarget(projection->camera);

    MtxFx43 rotation;
    VecFx32 rayDir;
    VecFx32 forward = { 0, 0, -FX32_ONE };

    if (touchY < HW_LCD_HEIGHT / 2) {
        pixelsFromCenterY = HW_LCD_HEIGHT / 2 - touchY;
        pitch = projection->fovY * pixelsFromCenterY / (HW_LCD_HEIGHT / 2);
    } else {
        pixelsFromCenterY = touchY - HW_LCD_HEIGHT / 2;
        pitch = projection->fovY * pixelsFromCenterY / (HW_LCD_HEIGHT / 2);
        pitch *= -1;
    }

    if (touchX < HW_LCD_WIDTH / 2) {
        pixelsFromCenterX = HW_LCD_WIDTH / 2 - touchX;
        yaw = projection->fovX * pixelsFromCenterX / (HW_LCD_WIDTH / 2);
    } else {
        pixelsFromCenterX = touchX - HW_LCD_WIDTH / 2;
        yaw = projection->fovX * pixelsFromCenterX / (HW_LCD_WIDTH / 2);
        yaw *= -1;
    }

    MTX_RotX43(&rotation, FX_SinIdx(pitch), FX_CosIdx(pitch));
    MTX_MultVec43(&forward, &rotation, &rayDir);
    MTX_RotY43(&rotation, FX_SinIdx(yaw), FX_CosIdx(yaw));
    MTX_MultVec43(&rayDir, &rotation, &rayDir);
    MTX_MultVec43(&rayDir, &projection->cameraPitch, &rayDir);

    VecFx32 hitOffset;
    fx32 rayLength;
    fx32 rayDotNormal;

    rayDotNormal = VEC_DotProduct(&projection->groundNormal, &rayDir);
    rayLength = -(FX_Div(projection->cameraHeight, rayDotNormal));

    VEC_MultAdd(rayLength, &rayDir, &projection->cameraDistance, &hitOffset);

    groundPos.x = cameraTarget.x + (hitOffset.x);
    groundPos.z = cameraTarget.z + (hitOffset.z);

    return groundPos;
}

TouchProjection *TouchProjection_New(Camera *const camera)
{
    TouchProjection *projection = Heap_Alloc(HEAP_ID_FIELD1, sizeof(TouchProjection));
    TouchProjection_Init(projection, camera);

    return projection;
}

static void TouchProjection_Init(TouchProjection *projection, Camera *const camera)
{
    CameraAngle angle;

    angle = Camera_GetAngle(camera);
    projection->fovY = Camera_GetFOV(camera);

    fx32 tan = FX_Div(FX_SinIdx(projection->fovY), FX_CosIdx(projection->fovY));

    // The screen is 4:3, so the horizontal field of view is wider.
    projection->fovX = FX_AtanIdx(tan * 4 / 3);

    VecFx32 up = { 0, FX32_ONE, 0 };
    VecFx32 position, target;

    position = Camera_GetPosition(camera);
    target = Camera_GetTarget(camera);

    VEC_Subtract(&position, &target, &projection->cameraDistance);

    projection->groundNormal = up;
    projection->cameraHeight = VEC_DotProduct(&up, &projection->cameraDistance);

    MTX_RotX43(&projection->cameraPitch, FX_SinIdx(angle.x), FX_CosIdx(angle.x));
    projection->camera = camera;
}

void TouchProjection_Free(TouchProjection **projection)
{
    if (*projection == NULL) {
        return;
    }

    GF_ASSERT(*projection != NULL);

    Heap_Free(*projection);
    *projection = NULL;
}
