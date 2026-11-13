#include <cmath>

#include "test.h"

#include "../vr_math.h"
#include "../vr_minigame_space.h"

using namespace dxvk;

namespace {

  constexpr float Near = 1e-3f;

  bool same(const VrVector3& a, const VrVector3& b) {
    return std::abs(a.x - b.x) < Near && std::abs(a.y - b.y) < Near && std::abs(a.z - b.z) < Near;
  }

  VrPose headAt(float x, float y, float z) {
    VrPose head;
    head.position = { x, y, z };
    return head;
  }

}


namespace {

  // A table turned a quarter of a turn: its model's +X is the world's -Y and
  // its +Y the world's +X
  VrWorldObject turnedTable() {
    VrWorldObject object;
    object.x        = { 0.0f, -1.0f, 0.0f };
    object.y        = { 1.0f, 0.0f, 0.0f };
    object.z        = { 0.0f, 0.0f, 1.0f };
    object.position = { 1000.0f, 2000.0f, 300.0f };
    return object;
  }

  VrSceneFit tableFit() {
    VrSceneFit fit;
    fit.sceneX = { 1.0f, 0.0f, 0.0f };
    fit.sceneY = { 0.0f, 1.0f, 0.0f };
    fit.sceneZ = { 0.0f, 0.0f, 1.0f };
    fit.origin = { 0.0f, 0.0f, 70.0f };
    fit.scale  = 2.0f;
    fit.seat   = { -70.0f, 0.0f, 0.0f };
    fit.facing = { 1.0f, 0.0f, 0.0f };
    return fit;
  }

  // The eye's camera for a point of the scene, as the game would see it
  VrVector3 sceneInWorld(const VrWorldObject& object, const VrSceneFit& fit, const VrVector3& point) {
    VrVector3 model = fit.origin + (fit.sceneX * point.x + fit.sceneY * point.y + fit.sceneZ * point.z) * fit.scale;
    return object.position + (object.x * model.x + object.y * model.y + object.z * model.z) * object.scale;
  }

}


TEST_CASE(seat_brings_the_head_to_the_object) {
  VrPose reference = vrMakeRecenterReference(headAt(0.0f, 1.6f, 0.0f));
  // Half a metre to the right of the reference and turned to the right
  VrPose head = headAt(0.5f, 1.6f, 0.0f);
  head.orientation = vrQuaternionFromAxisAngle({ 0.0f, 1.0f, 0.0f }, -1.0f);

  VrPose inReference = vrComputeEyeInReference(reference, head);
  VrWorldObject object = turnedTable();
  VrGameCameraPose camera = vrSeatCamera(object, tableFit(), inReference, 420.0f, 70.0f);

  VrGameCameraPose eye = vrComputeEyeCameraPose(camera, inReference, 70.0f);

  // The seat is 70 units along the model's -X, so the world's +Y
  CHECK(std::abs(eye.position.x - 1000.0f) < 1e-2f);
  CHECK(std::abs(eye.position.y - 2070.0f) < 1e-2f);
  // The height is the game's camera's and the head's own in the reference
  CHECK(std::abs(eye.position.z - 420.0f) < 1e-2f);
  // Looking along the model's +X, the world's -Y
  CHECK(same(eye.forward, { 0.0f, -1.0f, 0.0f }));
  CHECK(same(eye.up, { 0.0f, 0.0f, 1.0f }));
}


TEST_CASE(seat_with_an_eye_height_brings_the_head_to_it) {
  VrPose reference = vrMakeRecenterReference(headAt(0.0f, 1.6f, 0.0f));
  VrPose inReference = vrComputeEyeInReference(reference, headAt(0.0f, 1.3f, 0.0f));

  VrWorldObject object = turnedTable();
  VrSceneFit fit = tableFit();
  fit.eyeHeight = 27.0f;

  VrGameCameraPose camera = vrSeatCamera(object, fit, inReference, 420.0f, 70.0f);
  VrGameCameraPose eye = vrComputeEyeCameraPose(camera, inReference, 70.0f);

  // Whatever the game's camera and the head in the room, the eye is at the height
  CHECK(std::abs(eye.position.z - 327.0f) < 1e-2f);
}


TEST_CASE(scene_on_object_is_seen_where_the_object_is) {
  VrPose reference = vrMakeRecenterReference(headAt(0.3f, 1.6f, -0.4f));
  VrPose head = headAt(0.1f, 1.5f, -0.2f);
  head.orientation = vrQuaternionFromAxisAngle({ 0.0f, 1.0f, 0.0f }, 0.6f);

  VrWorldObject object = turnedTable();
  VrSceneFit fit = tableFit();
  VrPose inReference = vrComputeEyeInReference(reference, head);
  VrGameCameraPose camera = vrSeatCamera(object, fit, inReference, 420.0f, 70.0f);

  VrSceneSpace space = vrSceneSpaceOnObject(reference, camera, object, fit, 70.0f);

  // A model scaled twice as much has half as many units per metre
  CHECK(std::abs(space.unitsPerMetre - 35.0f) < Near);

  for (const VrVector3& point : { VrVector3 { 0.0f, 0.0f, 0.0f }, VrVector3 { 20.0f, -30.0f, 5.0f } }) {
    VrVector3 world = sceneInWorld(object, fit, point);
    VrVector3 room  = vrTransformPoint(reference, vrComputeGamePointInReference(camera, world, 70.0f));

    CHECK(same(vrSceneToRoom(space, point), room));
    CHECK(same(vrRoomToScene(space, room), point));
  }

  // The scene's camera for the eye looks at the scene as the world's does
  VrGameCameraPose scene = vrSceneCameraPose(space, head);
  VrGameCameraPose eye   = vrComputeEyeCameraPose(camera, inReference, 70.0f);

  VrVector3 sceneForward = sceneInWorld(object, fit, scene.forward) - sceneInWorld(object, fit, { 0.0f, 0.0f, 0.0f });

  CHECK(same(sceneForward * (1.0f / fit.scale), eye.forward));
  CHECK(same(sceneInWorld(object, fit, scene.position), eye.position));
}


TEST_CASE(a_body_in_the_table_or_on_the_seat_is_in_the_way) {
  constexpr VrModelArea Areas[] = { { -30.0f, 46.0f, -70.0f, 66.0f } };

  VrWorldObject object = turnedTable();
  VrSceneFit fit = tableFit();
  fit.areas     = Areas;
  fit.areaCount = 1;

  // The model's +X is the world's -Y
  VrVector3 model = vrPointInObject(object, { 1010.0f, 1950.0f, 345.0f });
  CHECK(same(model, { 50.0f, 10.0f, 45.0f }));

  // Through the table, a little beyond its edge, and in the dealer's place
  CHECK(vrBodyInTheWay(fit, { 10.0f, 5.0f, 45.0f }));
  CHECK(vrBodyInTheWay(fit, { 50.0f, 10.0f, 45.0f }));
  CHECK(!vrBodyInTheWay(fit, { 60.0f, 0.0f, 45.0f }));

  // On the player's seat, and on the stool at the end of the table
  CHECK(vrBodyInTheWay(fit, { -70.0f, 20.0f, 45.0f }));
  CHECK(!vrBodyInTheWay(fit, { -49.0f, -74.0f, 45.0f }));

  // Over the table, on the floor above
  CHECK(!vrBodyInTheWay(fit, { 10.0f, 5.0f, 300.0f }));
}


TEST_CASE(table_camera_stands_on_the_head) {
  VrPose head = headAt(0.5f, 1.6f, -0.2f);
  VrVector3 camera = { -32.0f, 25.0f, 36.0f };

  // Looking east and down: the table's east is the room's ahead
  VrSceneSpace space = vrSceneSpaceAroundHead(head, camera, { 0.7071f, 0.0f, -0.7071f }, 70.0f);

  CHECK(same(vrSceneToRoom(space, camera), head.position));

  // 70 units east of the camera is a metre ahead of the head
  CHECK(same(vrSceneToRoom(space, { 38.0f, 25.0f, 36.0f }), { 0.5f, 1.6f, -1.2f }));
  // 70 units below is a metre below
  CHECK(same(vrSceneToRoom(space, { -32.0f, 25.0f, -34.0f }), { 0.5f, 0.6f, -0.2f }));
  // Looking east, south is to the right
  CHECK(same(vrSceneToRoom(space, { -32.0f, -45.0f, 36.0f }), { 1.5f, 1.6f, -0.2f }));
}


TEST_CASE(scene_and_room_points_convert_both_ways) {
  VrPose head = headAt(0.2f, 1.5f, 0.3f);
  head.orientation = vrQuaternionFromAxisAngle({ 0.0f, 1.0f, 0.0f }, 0.8f);

  VrSceneSpace space = vrSceneSpaceAroundHead(head, { 1.0f, 2.0f, 3.0f }, { 0.3f, -0.8f, -0.5f }, 70.0f);
  VrVector3 point = { 12.0f, -8.0f, 4.0f };

  CHECK(same(vrRoomToScene(space, vrSceneToRoom(space, point)), point));
}


TEST_CASE(eye_camera_looks_at_the_scene_as_the_room_says) {
  VrPose head = headAt(0.0f, 1.6f, 0.0f);
  VrSceneSpace space = vrSceneSpaceInFront(head, { 140.0f, 0.6f, 0.0f }, 0.55f, 0.1f, 120.0f);

  // The eye at the head, not turned
  VrGameCameraPose camera = vrSceneCameraPose(space, head);

  CHECK(same(camera.forward, { 1.0f, 0.0f, 0.0f }));
  CHECK(same(camera.up,      { 0.0f, 1.0f, 0.0f }));
  CHECK(same(camera.right,   { 0.0f, 0.0f, 1.0f }));
  // 0.55 m before the target and 0.1 m above it, at 120 units per metre
  CHECK(same(camera.position, { 140.0f - 66.0f, 0.6f + 12.0f, 0.0f }));

  // Right-handed as the game's camera: right = forward x up
  VrVector3 right = {
    camera.forward.y * camera.up.z - camera.forward.z * camera.up.y,
    camera.forward.z * camera.up.x - camera.forward.x * camera.up.z,
    camera.forward.x * camera.up.y - camera.forward.y * camera.up.x };

  CHECK(same(right, camera.right));
}


TEST_CASE(eye_camera_on_a_table_is_right_handed) {
  VrSceneSpace space = vrSceneSpaceAroundHead(headAt(0.0f, 1.6f, 0.0f),
    { 0.0f, 0.0f, 30.0f }, { 0.0f, 1.0f, -1.0f }, 70.0f);

  VrPose eye = headAt(-0.03f, 1.6f, 0.0f);
  VrGameCameraPose camera = vrSceneCameraPose(space, eye);

  CHECK(same(camera.forward, { 0.0f, 1.0f, 0.0f }));
  CHECK(same(camera.up,      { 0.0f, 0.0f, 1.0f }));
  CHECK(same(camera.right,   { 1.0f, 0.0f, 0.0f }));
  // The left eye is to the west when the table's north is ahead
  CHECK(same(camera.position, { -0.03f * 70.0f, 0.0f, 30.0f }));
}


TEST_CASE(felt_is_pointed_at_from_above) {
  VrSceneSpace space = vrSceneSpaceAroundHead(headAt(0.0f, 1.6f, 0.0f),
    { 0.0f, -50.0f, 70.0f }, { 0.0f, 1.0f, -1.0f }, 70.0f);

  VrVector3 centre = { 0.0f, 0.0f, 0.0f };
  VrPanelPlacement felt = vrSceneSurface(space, centre, 70.0f, 35.0f);

  CHECK_NEAR(felt.width, 1.0f, Near);
  CHECK_NEAR(felt.height, 0.5f, Near);
  CHECK(same(felt.pose.position, vrSceneToRoom(space, centre)));

  // A ray straight down onto a point 14 units east and 7 north of the centre
  VrVector3 target = vrSceneToRoom(space, { 14.0f, 7.0f, 0.0f });

  VrPose ray;
  ray.position = target + VrVector3 { 0.0f, 0.5f, 0.0f };
  // The ray points along its -Z: turn it to point down
  ray.orientation = vrQuaternionFromAxisAngle({ 1.0f, 0.0f, 0.0f }, -1.5707963f);

  VrPanelHit hit = vrIntersectPanel(ray, felt);
  CHECK(hit.hit);
  CHECK_NEAR(hit.distance, 0.5f, Near);

  VrVector3 point = vrSurfacePoint(centre, 70.0f, 35.0f, hit.uv);
  CHECK_NEAR(point.x, 14.0f, 0.05f);
  CHECK_NEAR(point.y, 7.0f, 0.05f);

  // From below it is not hit
  ray.position = target + VrVector3 { 0.0f, -0.5f, 0.0f };
  ray.orientation = vrQuaternionFromAxisAngle({ 1.0f, 0.0f, 0.0f }, 1.5707963f);
  CHECK(!(vrIntersectPanel(ray, felt).hit));
}


TEST_CASE(view_rectangle_gives_the_camera_cursor_position) {
  VrPose head = headAt(0.0f, 1.6f, 0.0f);
  VrSceneSpace space = vrSceneSpaceInFront(head, { 20.0f, 0.38f, 0.0f }, 0.7f, 0.0f, 9.63f);

  VrGameFrustum frustum;
  frustum.left = -0.149f;
  frustum.right = 0.149f;
  frustum.top = 0.112f;
  frustum.bottom = -0.112f;

  VrPanelPlacement view = vrSceneViewRectangle(space, 20.0f, frustum);

  CHECK_NEAR(view.width, 2.0f * 20.0f * 0.149f / 9.63f, Near);
  CHECK_NEAR(view.height, 2.0f * 20.0f * 0.112f / 9.63f, Near);

  // A point of the scene on the plane
  VrVector3 point = { 20.0f, 1.0f, 2.0f };
  VrVector3 room  = vrSceneToRoom(space, point);

  VrPose ray;
  ray.position = room + VrVector3 { 0.0f, 0.0f, 0.3f };

  VrPanelHit hit = vrIntersectPanel(ray, view);
  CHECK(hit.hit);

  // What the game's camera, at the scene's origin looking along +X, sees there
  float u = (point.z / point.x - frustum.left) / (frustum.right - frustum.left);
  float v = (frustum.top - point.y / point.x) / (frustum.top - frustum.bottom);

  CHECK_NEAR(hit.uv.x, u, Near);
  CHECK_NEAR(hit.uv.y, v, Near);
}


TEST_CASE(fit_of_a_model_finds_its_points_back) {
  // A model stood up and turned in its scene, as the vigor tester's
  VrWorldObject model;
  model.x        = { -1.0f, 0.0f, 0.0f };
  model.y        = { 0.0f, 0.0f, -1.0f };
  model.z        = { 0.0f, 1.0f, 0.0f };
  model.position = { 0.0f, -85.0f, -85.0f };
  model.scale    = 2.0f;

  VrSceneFit fit = vrSceneFitOfModel(model);

  for (const VrVector3& point : { VrVector3 { 0.0f, 0.0f, 0.0f }, VrVector3 { 10.0f, -15.0f, 92.0f } }) {
    VrVector3 scene = model.position + (model.x * point.x + model.y * point.y + model.z * point.z) * model.scale;
    VrVector3 back  = fit.origin + (fit.sceneX * scene.x + fit.sceneY * scene.y + fit.sceneZ * scene.z) * fit.scale;
    CHECK(same(back, point));
  }
}


TEST_CASE(object_in_object_keeps_its_points) {
  VrWorldObject frame = turnedTable();
  frame.scale = 2.0f;

  VrWorldObject object;
  object.x        = { 0.0f, 0.0f, 1.0f };
  object.y        = { 0.0f, 1.0f, 0.0f };
  object.z        = { -1.0f, 0.0f, 0.0f };
  object.position = { 1010.0f, 1990.0f, 320.0f };
  object.scale    = 3.0f;

  VrWorldObject inFrame = vrObjectInObject(frame, object);
  VrVector3 point = { 1.0f, -2.0f, 4.0f };

  VrVector3 world = object.position + (object.x * point.x + object.y * point.y + object.z * point.z) * object.scale;
  VrVector3 local = inFrame.position + (inFrame.x * point.x + inFrame.y * point.y + inFrame.z * point.z) * inFrame.scale;

  CHECK(same(vrPointInObject(frame, world), local));
}


TEST_CASE(menu_screen_lies_on_the_world_screen) {
  // The menu's glass faces -Y with +Z up and +X right, at 2 units of scale
  VrWorldObject sceneScreen;
  sceneScreen.x        = { 1.0f, 0.0f, 0.0f };
  sceneScreen.y        = { 0.0f, 1.0f, 0.0f };
  sceneScreen.z        = { 0.0f, 0.0f, 1.0f };
  sceneScreen.position = { 20.0f, 0.4f, 0.0f };
  sceneScreen.scale    = 2.0f;

  VrScreenGlass sceneGlass;
  sceneGlass.centre = { 0.0f, 0.0f, 0.5f };
  sceneGlass.right  = { 1.0f, 0.0f, 0.0f };
  sceneGlass.up     = { 0.0f, 0.0f, 1.0f };
  sceneGlass.width  = 6.0f;
  sceneGlass.height = 4.0f;

  // The world's faces +X with +Y right, tilted back, wider than tall by less
  VrWorldObject modelScreen;
  modelScreen.x        = { 1.0f, 0.0f, 0.0f };
  modelScreen.y        = { 0.0f, 1.0f, 0.0f };
  modelScreen.z        = { 0.0f, 0.0f, 1.0f };
  modelScreen.position = { 0.0f, 0.0f, 10.0f };

  VrScreenGlass modelGlass;
  modelGlass.centre = { 5.0f, 0.0f, 20.0f };
  modelGlass.right  = { 0.0f, 1.0f, 0.0f };
  modelGlass.up     = { -0.6f, 0.0f, 0.8f };
  modelGlass.width  = 24.0f;
  modelGlass.height = 20.0f;

  VrSceneFit fit = vrSceneFitOnScreen(sceneScreen, sceneGlass, modelScreen, modelGlass, 0.5f);

  auto inModel = [&] (const VrVector3& p) {
    return fit.origin + (fit.sceneX * p.x + fit.sceneY * p.y + fit.sceneZ * p.z) * fit.scale;
  };

  // Its width takes half of the world's: 12 units for 12
  CHECK_NEAR(fit.scale, 1.0f, Near);

  // Middle on middle, right on right, up on up
  CHECK(same(inModel({ 20.0f, 0.4f, 1.0f }), { 5.0f, 0.0f, 30.0f }));
  CHECK(same(inModel({ 21.0f, 0.4f, 1.0f }), { 5.0f, 1.0f, 30.0f }));
  CHECK(same(inModel({ 20.0f, 0.4f, 2.0f }), { 4.4f, 0.0f, 30.8f }));
  // The menu's viewer side, -Y, is the world's
  CHECK(same(inModel({ 20.0f, -0.6f, 1.0f }), { 5.8f, 0.0f, 30.6f }));
}


TEST_CASE(camera_view_rectangle_gives_the_cursor_of_a_camera_anywhere) {
  VrPose reference = vrMakeRecenterReference(headAt(0.0f, 1.6f, 0.0f));

  VrSceneSpace space;
  space.anchor        = reference;
  space.origin        = { 5.0f, 0.0f, 0.0f };
  space.right         = { 1.0f, 0.0f, 0.0f };
  space.up            = { 0.0f, 0.0f, 1.0f };
  space.back          = { 0.0f, -1.0f, 0.0f };
  space.unitsPerMetre = 70.0f;

  // At (0, 0, 1) looking at the origin with +Y up, as the vigor tester's
  VrGameCameraPose camera;
  camera.position = { 0.0f, 0.0f, 1.0f };
  camera.forward  = { 0.0f, 0.0f, -1.0f };
  camera.up       = { 0.0f, 1.0f, 0.0f };
  camera.right    = { 1.0f, 0.0f, 0.0f };

  VrGameFrustum frustum;
  frustum.left   = -0.8f;
  frustum.right  = 0.8f;
  frustum.top    = 0.45f;
  frustum.bottom = -0.45f;

  VrPanelPlacement view = vrCameraViewRectangle(space, camera, 60.0f, frustum);

  CHECK_NEAR(view.width, 1.6f * 60.0f / 70.0f, Near);
  CHECK_NEAR(view.height, 0.9f * 60.0f / 70.0f, Near);

  // A point on the plane, 60 units ahead of the camera
  VrVector3 point = { -12.0f, 9.0f, -59.0f };
  VrVector3 room  = vrSceneToRoom(space, point);
  VrVector3 normal = vrRotate(view.pose.orientation, VrVector3 { 0.0f, 0.0f, 1.0f });

  VrPose ray;
  ray.position    = room + normal * 0.3f;
  ray.orientation = view.pose.orientation;

  VrPanelHit hit = vrIntersectPanel(ray, view);
  CHECK(hit.hit);

  VrVector3 offset = point - camera.position;
  float depth = vrDot(offset, camera.forward);
  float u = (vrDot(offset, camera.right) / depth - frustum.left) / (frustum.right - frustum.left);
  float v = (frustum.top - vrDot(offset, camera.up) / depth) / (frustum.top - frustum.bottom);

  CHECK_NEAR(hit.uv.x, u, Near);
  CHECK_NEAR(hit.uv.y, v, Near);
}


TEST_CASE(camera_along_a_ray_sees_the_ray_at_the_point) {
  VrGameCameraPose camera;
  camera.position = { 0.0f, 0.0f, 1.0f };
  camera.forward  = { 0.0f, 0.0f, -1.0f };
  camera.up       = { 0.0f, 1.0f, 0.0f };
  camera.right    = { 1.0f, 0.0f, 0.0f };

  VrGameFrustum frustum;
  frustum.left   = -0.8f;
  frustum.right  = 0.8f;
  frustum.top    = 0.45f;
  frustum.bottom = -0.45f;

  VrVector2 uv = { 0.7f, 0.2f };
  VrVector3 origin = { 10.0f, -20.0f, -30.0f };
  VrVector3 direction = { -0.3f, 0.5f, -0.6f };

  VrGameCameraPose pose = vrCameraAlongRay(camera, frustum, uv, origin, direction);

  CHECK(same(pose.position, origin));
  CHECK_NEAR(vrLength(pose.forward), 1.0f, Near);
  CHECK_NEAR(vrDot(pose.forward, pose.up), 0.0f, Near);
  CHECK_NEAR(vrDot(pose.forward, pose.right), 0.0f, Near);

  VrVector3 view = pose.forward
    + pose.right * (frustum.left + uv.x * (frustum.right - frustum.left))
    + pose.up    * (frustum.top  - uv.y * (frustum.top - frustum.bottom));

  CHECK(same(view * (1.0f / vrLength(view)), direction * (1.0f / vrLength(direction))));
}


TEST_CASE(ray_meets_a_sphere_ahead_only) {
  VrVector3 origin = { 0.0f, 0.0f, 0.0f };
  VrVector3 ahead  = { 0.0f, 0.0f, -2.0f };

  CHECK(vrRayMeetsSphere(origin, ahead, { 0.5f, 0.0f, -10.0f }, 0.6f));
  CHECK(!vrRayMeetsSphere(origin, ahead, { 0.7f, 0.0f, -10.0f }, 0.6f));
  CHECK(!vrRayMeetsSphere(origin, ahead, { 0.0f, 0.0f, 10.0f }, 0.6f));
  CHECK(vrRayMeetsSphere(origin, ahead, { 0.0f, 0.3f, 0.2f }, 0.6f));
}


TEST_CASE(pin_points_at_the_hand) {
  VrPose lock;
  lock.position = { 0.0f, 1.4f, -0.5f };

  CHECK_NEAR(vrLockPinAngle(lock, { 0.0f, 1.6f, -0.3f }, 7.0f), 0.0f, 0.01f);
  CHECK_NEAR(vrLockPinAngle(lock, { 0.2f, 1.6f, -0.3f }, 7.0f), 45.0f, 0.01f);
  CHECK_NEAR(vrLockPinAngle(lock, { -0.2f, 1.4f, -0.3f }, 7.0f), -90.0f, 0.01f);
  // Below the lock it stays at the end of its half turn
  CHECK_NEAR(vrLockPinAngle(lock, { 0.1f, 1.2f, -0.3f }, 7.0f), 90.0f, 0.01f);
  // Too close: unknown
  CHECK_NEAR(vrLockPinAngle(lock, { 0.01f, 1.41f, -0.3f }, 7.0f), 7.0f, 0.01f);

  CHECK_NEAR(vrLockPinSlider(-90.0f, 400.0f), 0.0f, 0.01f);
  CHECK_NEAR(vrLockPinSlider(0.0f, 400.0f), 200.0f, 0.01f);
  CHECK_NEAR(vrLockPinSlider(90.0f, 400.0f), 400.0f, 0.01f);
}


TEST_CASE(pin_stick_moves_the_slider_as_the_game_does) {
  // The game adds v^3 * 80
  float stick = vrLockPinStick(100.0f, 120.0f);
  CHECK_NEAR(stick * stick * stick * 80.0f, 20.0f, 0.01f);

  stick = vrLockPinStick(100.0f, 90.0f);
  CHECK_NEAR(stick * stick * stick * 80.0f, -10.0f, 0.01f);

  // Outside the game's dead zone, or none
  CHECK(std::abs(vrLockPinStick(100.0f, 101.2f)) * 32767.0f > 7849.0f);
  CHECK((vrLockPinStick(100.0f, 100.5f)) == (0.0f));

  // Far away it pushes the stick all the way
  CHECK((vrLockPinStick(0.0f, 300.0f)) == (1.0f));
  CHECK((vrLockPinStick(300.0f, 0.0f)) == (-1.0f));
}


TEST_CASE(tension_turns_from_the_trigger_or_the_wrist) {
  VrLockTension tension;
  VrQuaternion level;
  VrVector3 axis = { 0.0f, 0.0f, -1.0f };

  CHECK((tension.update(false, false, level)) == (0.0f));
  CHECK((tension.update(true, false, level)) == (1.0f));

  // Without the grip, the wrist does nothing
  CHECK((tension.update(false, false, vrQuaternionFromAxisAngle(axis, 0.8f))) == (0.0f));

  // The grip takes it where the hand is
  VrQuaternion taken = vrQuaternionFromAxisAngle(axis, 0.5f);
  CHECK((tension.update(false, true, taken)) == (0.0f));
  CHECK((tension.update(false, true, vrQuaternionFromAxisAngle(axis, 0.6f))) == (0.0f));

  float half = tension.update(false, true, vrQuaternionFromAxisAngle(axis, 0.5f + 0.475f));
  CHECK_NEAR(half, 0.5f, 0.01f);

  // Either way
  CHECK_NEAR(tension.update(false, true, vrQuaternionFromAxisAngle(axis, 0.5f - 0.475f)), 0.5f, 0.01f);
  CHECK((tension.update(false, true, vrQuaternionFromAxisAngle(axis, 2.0f))) == (1.0f));

  // Stick values the game accepts
  CHECK((vrLockTensionStick(0.0f)) == (0.0f));
  CHECK(vrLockTensionStick(0.01f) * 32767.0f < -8689.0f);
  CHECK_NEAR(vrLockTensionStick(1.0f), -1.0f, Near);
}


TEST_CASE(slot_lever_follows_the_hand_that_holds_it) {
  // The game's lever, in model units at the casino's scale
  const VrVector3 top = { 2.84f, 0.0f, 21.6f };
  constexpr float UnitsPerMetre = 50.0f;
  constexpr float Step = 1.0f / 90.0f;

  VrSlotLever lever(top);
  std::array<VrSlotLeverHand, VrHandCount> hands;
  hands[1].active   = true;
  hands[1].position = top;

  // The lever turns towards the machine's front, -Y
  VrVector3 down = VrSlotLever::turn(top, 0.5f);
  CHECK(down.y < 0.0f);
  CHECK(down.z < top.z);
  CHECK_NEAR(VrSlotLever::angleOf(down), 0.5f, Near);

  // An open hand on the knob does nothing
  CHECK(lever.update(hands, UnitsPerMetre, Step).taken == -1);
  CHECK(!lever.driven());

  // A grip closed half a metre away does not take it, even brought to the knob
  hands[1].grip     = true;
  hands[1].position = top + VrVector3 { 25.0f, 0.0f, 0.0f };
  CHECK(lever.update(hands, UnitsPerMetre, Step).taken == -1);
  hands[1].position = top;
  CHECK(lever.update(hands, UnitsPerMetre, Step).taken == -1);
  hands[1].grip = false;
  lever.update(hands, UnitsPerMetre, Step);

  // Closed a few centimetres from the knob, it takes the lever
  hands[1].grip     = true;
  hands[1].position = top + VrVector3 { 2.0f, -2.0f, 1.0f };
  CHECK(lever.update(hands, UnitsPerMetre, Step).taken == 1);
  CHECK(lever.holder() == 1);
  CHECK(lever.driven());

  // Halfway: the lever follows, notches felt, no spin yet
  bool notch = false;
  bool pulled = false;

  for (uint32_t i = 1; i <= 10; i++) {
    hands[1].position = VrSlotLever::turn(top + VrVector3 { 2.0f, -2.0f, 1.0f }, 0.04f * float(i));
    VrSlotLeverEvents events = lever.update(hands, UnitsPerMetre, Step);
    notch  = notch  || events.notch;
    pulled = pulled || events.pulled;
  }

  CHECK_NEAR(lever.angle(), 0.4f, 0.01f);
  CHECK(notch);
  CHECK(!pulled);

  // The hand stays where it holds the lever
  CHECK(vrLength(lever.heldPoint() - VrSlotLever::turn(top, 0.4f)) < 0.5f);

  // Down: one spin, then the end stop
  const VrVector3 grab = top + VrVector3 { 2.0f, -2.0f, 1.0f };
  hands[1].position = VrSlotLever::turn(grab, 0.75f);
  CHECK(lever.update(hands, UnitsPerMetre, Step).pulled);
  hands[1].position = VrSlotLever::turn(grab, 1.3f);
  VrSlotLeverEvents end = lever.update(hands, UnitsPerMetre, Step);
  CHECK(end.stop);
  CHECK(!end.pulled);
  CHECK_NEAR(lever.angle(), VrSlotLever::MaxAngle, Near);

  // Let go: it springs back to its rest
  hands[1].grip = false;
  CHECK(lever.update(hands, UnitsPerMetre, Step).released);
  CHECK(lever.holder() == -1);

  for (uint32_t i = 0; i < 180; i++) {
    lever.update(hands, UnitsPerMetre, Step);
    CHECK(lever.angle() >= 0.0f);
    CHECK(lever.angle() <= VrSlotLever::MaxAngle);
  }

  CHECK((lever.angle()) == (0.0f));
  CHECK(!lever.driven());

  // Back at rest it starts another spin
  hands[1].grip     = true;
  hands[1].position = top;
  CHECK(lever.update(hands, UnitsPerMetre, Step).taken == 1);
  hands[1].position = VrSlotLever::turn(top, 0.8f);
  CHECK(lever.update(hands, UnitsPerMetre, Step).pulled);

  // A hand pulled away from the lever lets go of it
  hands[1].position = VrSlotLever::turn(top, 0.8f) + VrVector3 { 0.0f, 0.0f, -20.0f };
  CHECK(lever.update(hands, UnitsPerMetre, Step).released);
}


TEST_CASE(slot_lever_keeps_the_trigger_of_a_hand_closed_on_it) {
  const VrVector3 top = { 2.84f, 0.0f, 21.6f };
  constexpr float UnitsPerMetre = 50.0f;
  constexpr float Step = 1.0f / 90.0f;

  VrSlotLever lever(top);
  std::array<VrSlotLeverHand, VrHandCount> hands;
  hands[0].active   = true;
  hands[0].position = top + VrVector3 { 0.0f, -60.0f, 0.0f };
  hands[1].active   = true;
  hands[1].position = top;

  // The trigger closes on the knob just before the grip: kept
  hands[1].trigger = true;
  lever.update(hands, UnitsPerMetre, Step);
  CHECK(lever.keepsTrigger(1));

  hands[1].grip = true;
  CHECK(lever.update(hands, UnitsPerMetre, Step).taken == 1);
  CHECK(lever.keepsTrigger(1));

  // Kept while the lever is pulled, and after the hand leaves it squeezed
  hands[1].position = VrSlotLever::turn(top, 0.8f);
  CHECK(lever.update(hands, UnitsPerMetre, Step).pulled);
  CHECK(lever.keepsTrigger(1));

  hands[1].grip     = false;
  hands[1].position = top + VrVector3 { 0.0f, -60.0f, 0.0f };
  lever.update(hands, UnitsPerMetre, Step);
  CHECK(lever.keepsTrigger(1));

  // Released, the trigger is the menu's again
  hands[1].trigger = false;
  lever.update(hands, UnitsPerMetre, Step);
  CHECK(!lever.keepsTrigger(1));

  hands[1].trigger = true;
  lever.update(hands, UnitsPerMetre, Step);
  CHECK(!lever.keepsTrigger(1));

  // Away from the lever, the other hand's trigger stays a click, even
  // when that hand then comes close with it held
  hands[0].trigger = true;
  lever.update(hands, UnitsPerMetre, Step);
  CHECK(!lever.keepsTrigger(0));

  hands[0].position = top;
  lever.update(hands, UnitsPerMetre, Step);
  CHECK(!lever.keepsTrigger(0));
}


TEST_CASE(slot_lever_starts_where_the_game_has_it) {
  const VrVector3 top = { 2.84f, 0.0f, 21.6f };
  constexpr float UnitsPerMetre = 50.0f;
  constexpr float Step = 1.0f / 90.0f;

  VrSlotLever lever(top);
  std::array<VrSlotLeverHand, VrHandCount> hands;

  // The game's own animation, played by a button
  lever.follow(0.5f);
  CHECK_NEAR(lever.angle(), 0.5f, Near);

  // The left hand takes it where it is: no jump
  hands[0].active   = true;
  hands[0].grip     = true;
  hands[0].position = VrSlotLever::turn(top, 0.5f);
  CHECK(lever.update(hands, UnitsPerMetre, Step).taken == 0);
  CHECK_NEAR(lever.angle(), 0.5f, Near);

  // Held, the game no longer moves it
  lever.follow(0.0f);
  CHECK_NEAR(lever.angle(), 0.5f, Near);

  // The other hand takes it over
  hands[1].active   = true;
  hands[1].grip     = true;
  hands[1].position = VrSlotLever::turn(top * 0.7f, 0.5f);
  CHECK(lever.update(hands, UnitsPerMetre, Step).taken == 1);
  CHECK(lever.holder() == 1);
}


TEST_CASE(tap_on_the_felt_is_a_signal) {
  VrTableSignals signals;
  std::array<VrTableHand, VrHandCount> hands;
  constexpr float Step = 1.0f / 90.0f;

  hands[1].active = true;
  hands[1].over   = true;
  hands[1].height = 0.2f;

  CHECK(signals.update(hands, Step) == VrTableSignal::None);

  // Lowered slowly: the hand rests on the table
  hands[1].height = 0.05f + Step * 0.2f;
  CHECK(signals.update(hands, Step) == VrTableSignal::None);
  hands[1].height = 0.05f - Step * 0.2f;
  CHECK(signals.update(hands, Step) == VrTableSignal::None);

  // Raised again, for a real tap the cooldown is free
  hands[1].height = 0.2f;
  CHECK(signals.update(hands, Step) == VrTableSignal::None);

  hands[1].height = 0.06f;
  CHECK(signals.update(hands, Step) == VrTableSignal::None);
  hands[1].height = 0.04f;
  CHECK(signals.update(hands, Step) == VrTableSignal::Tap);

  // Not two at once
  hands[1].height = 0.3f;
  signals.update(hands, Step);
  hands[1].height = 0.06f;
  signals.update(hands, Step);
  hands[1].height = 0.04f;
  CHECK(signals.update(hands, Step) == VrTableSignal::None);

  // Off the table it is nothing
  VrTableSignals off;
  hands[1].over = false;
  hands[1].height = 0.3f;
  off.update(hands, Step);
  hands[1].height = 0.06f;
  off.update(hands, Step);
  hands[1].height = 0.04f;
  CHECK(off.update(hands, Step) == VrTableSignal::None);
}


TEST_CASE(wave_over_the_table_is_a_signal) {
  VrTableSignals signals;
  std::array<VrTableHand, VrHandCount> hands;
  constexpr float Step = 1.0f / 90.0f;

  hands[0].active = true;
  hands[0].over   = true;
  hands[0].height = 0.2f;

  VrTableSignal got = VrTableSignal::None;
  float side = 0.0f;

  // Back and forth at 1 m/s, 0.2 s each way
  for (uint32_t i = 0; i < 90 && got == VrTableSignal::None; i++) {
    float direction = (i / 18) % 2 ? -1.0f : 1.0f;
    side += direction * 1.0f * Step;
    hands[0].side = side;
    got = signals.update(hands, Step);
  }

  CHECK(got == VrTableSignal::Wave);

  // One move across the table is not a wave
  VrTableSignals one;
  side = 0.0f;

  for (uint32_t i = 0; i < 40; i++) {
    side += 1.0f * Step;
    hands[0].side = side;
    CHECK(one.update(hands, Step) == VrTableSignal::None);
  }
}


TEST_CASE(table_between_lies_on_the_line_short_of_the_other) {
  VrWorldObject table;
  VrVector3 player = { 100.0f, 200.0f, 10.0f };
  VrVector3 other  = { 100.0f, 400.0f, 12.0f };

  CHECK(vrTableBetween(player, other, 25.0f, 20.0f, 65.0f, table));

  // +Y towards the other, +X to the player's right, up stays up
  CHECK(same(table.y, VrVector3 { 0.0f, 1.0f, 0.0f }));
  CHECK(same(table.x, VrVector3 { 1.0f, 0.0f, 0.0f }));
  CHECK(same(table.z, VrVector3 { 0.0f, 0.0f, 1.0f }));
  CHECK(same(table.position, VrVector3 { 100.0f, 355.0f, 65.0f }));

  // The far edge is the gap short of the other person
  VrVector3 edge = table.position + table.y * 25.0f;
  CHECK(std::abs(other.y - edge.y - 20.0f) < Near);

  // Facing west, the right is north
  CHECK(vrTableBetween(player, VrVector3 { 0.0f, 200.0f, 10.0f }, 25.0f, 20.0f, 65.0f, table));
  CHECK(same(table.x, VrVector3 { 0.0f, 1.0f, 0.0f }));

  CHECK(!vrTableBetween(player, VrVector3 { 100.0f, 200.0f, 80.0f }, 25.0f, 20.0f, 65.0f, table));
}
