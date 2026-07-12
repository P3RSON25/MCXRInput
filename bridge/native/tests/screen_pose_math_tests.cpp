#include <mcxrinput/screen_pose_math.hpp>

#include <cmath>
#include <iostream>
#include <limits>

using namespace mcxrinput::native;

namespace {

constexpr float epsilon = 1.0e-4F;

int failures = 0;

void check(bool condition, const char* message) {
	if (!condition) {
		std::cerr << "FAIL: " << message << '\n';
		++failures;
	}
}

bool near(float left, float right, float tolerance = epsilon) {
	return std::abs(left - right) <= tolerance;
}

bool near(Vec3 left, Vec3 right, float tolerance = epsilon) {
	return near(left.x, right.x, tolerance)
			&& near(left.y, right.y, tolerance)
			&& near(left.z, right.z, tolerance);
}

bool near(Quaternion left, Quaternion right, float tolerance = epsilon) {
	return near(left.x, right.x, tolerance)
			&& near(left.y, right.y, tolerance)
			&& near(left.z, right.z, tolerance)
			&& near(left.w, right.w, tolerance);
}

Quaternion axisAngle(Vec3 axis, float degrees) {
	const float radians = degrees * 3.14159265358979323846F / 180.0F;
	const float sine = std::sin(radians * 0.5F);
	return Quaternion{axis.x * sine, axis.y * sine, axis.z * sine, std::cos(radians * 0.5F)};
}

Quaternion multiply(Quaternion left, Quaternion right) {
	return Quaternion{
			left.w * right.x + left.x * right.w + left.y * right.z - left.z * right.y,
			left.w * right.y - left.x * right.z + left.y * right.w + left.z * right.x,
			left.w * right.z + left.x * right.y - left.y * right.x + left.z * right.w,
			left.w * right.w - left.x * right.x - left.y * right.y - left.z * right.z,
	};
}

Vec3 screenAxis(const Pose& pose, Vec3 localAxis) {
	return rotateVector(pose.orientation, localAxis);
}

void checkOrthonormal(const Pose& pose) {
	const Vec3 right = screenAxis(pose, Vec3{1.0F, 0.0F, 0.0F});
	const Vec3 up = screenAxis(pose, Vec3{0.0F, 1.0F, 0.0F});
	const Vec3 front = screenAxis(pose, Vec3{0.0F, 0.0F, 1.0F});
	auto dot = [](Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; };
	check(near(dot(right, right), 1.0F), "right axis is unit length");
	check(near(dot(up, up), 1.0F), "up axis is unit length");
	check(near(dot(front, front), 1.0F), "front axis is unit length");
	check(near(dot(right, up), 0.0F), "right and up are perpendicular");
	check(near(dot(right, front), 0.0F), "right and front are perpendicular");
	check(near(dot(up, front), 0.0F), "up and front are perpendicular");
}

Pose compute(const Pose& head, float distance, RollFreeBasisState& state) {
	Pose output;
	check(computeGazeCenteredRollFreePose(head, distance, state, output), "pose computation succeeds");
	checkOrthonormal(output);
	return output;
}

void neutralAndTranslation() {
	RollFreeBasisState state;
	const Pose output = compute(Pose{{0, 0, 0, 1}, {1, 2, 3}}, 1.5F, state);
	check(near(output.position, Vec3{1, 2, 1.5F}), "neutral pose uses exact distance");
	check(near(screenAxis(output, Vec3{1, 0, 0}), Vec3{1, 0, 0}), "neutral right is +X");
	check(near(screenAxis(output, Vec3{0, 1, 0}), Vec3{0, 1, 0}), "neutral up is +Y");
	check(near(screenAxis(output, Vec3{0, 0, 1}), Vec3{0, 0, 1}), "neutral front faces viewer");
}

void yawAndPitch() {
	RollFreeBasisState state;
	Pose left = compute(Pose{axisAngle({0, 1, 0}, 90), {}}, 2.0F, state);
	check(near(left.position, Vec3{-2, 0, 0}), "+90 yaw moves screen toward -X");
	check(near(screenAxis(left, Vec3{0, 0, -1}), Vec3{-1, 0, 0}), "+90 yaw preserves gaze");

	state = {};
	Pose pitch = compute(Pose{axisAngle({1, 0, 0}, 30), {}}, 1.0F, state);
	check(near(pitch.position, Vec3{0, 0.5F, -0.8660254F}), "pitch changes gaze and center");
	check(near(screenAxis(pitch, Vec3{1, 0, 0}), Vec3{1, 0, 0}), "pitch keeps screen right level");
}

void rollIsRemoved() {
	RollFreeBasisState neutralState;
	RollFreeBasisState rolledState;
	const Pose neutral = compute(Pose{}, 1.5F, neutralState);
	const Pose rolled = compute(Pose{axisAngle({0, 0, 1}, 67), {}}, 1.5F, rolledState);
	check(near(rolled.position, neutral.position), "pure head roll does not move screen");
	check(near(screenAxis(rolled, {1, 0, 0}), screenAxis(neutral, {1, 0, 0})),
			"pure head roll does not roll screen right");
	check(near(screenAxis(rolled, {0, 1, 0}), screenAxis(neutral, {0, 1, 0})),
			"pure head roll does not roll screen up");

	const Quaternion yawPitch = multiply(axisAngle({0, 1, 0}, 45), axisAngle({1, 0, 0}, 30));
	const Quaternion combined = multiply(yawPitch, axisAngle({0, 0, 1}, 70));
	RollFreeBasisState firstState;
	RollFreeBasisState secondState;
	const Pose first = compute(Pose{yawPitch, {}}, 1.5F, firstState);
	const Pose second = compute(Pose{combined, {}}, 1.5F, secondState);
	check(near(first.position, second.position), "local roll does not alter combined gaze position");
	check(near(screenAxis(first, {1, 0, 0}), screenAxis(second, {1, 0, 0})),
			"local roll does not alter combined screen right");
	check(near(screenAxis(first, {0, 1, 0}), screenAxis(second, {0, 1, 0})),
			"local roll does not alter combined screen up");
}

void verticalFallback() {
	RollFreeBasisState state;
	compute(Pose{}, 1.0F, state);
	const Pose straightUp = compute(Pose{axisAngle({1, 0, 0}, 90), {}}, 1.0F, state);
	check(near(straightUp.position, Vec3{0, 1, 0}), "vertical gaze still updates center");
	check(near(screenAxis(straightUp, {1, 0, 0}), Vec3{1, 0, 0}),
			"vertical gaze preserves previous right axis");

	RollFreeBasisState emptyState;
	const Pose initialVertical = compute(Pose{axisAngle({1, 0, 0}, -90), {}}, 1.0F, emptyState);
	checkOrthonormal(initialVertical);

	RollFreeBasisState noisyState;
	compute(Pose{}, 1.0F, noisyState);
	const Pose nearPoleA = compute(Pose{axisAngle({1, 0, 0}, 88.5F), {}}, 1.0F, noisyState);
	const Pose nearPoleB = compute(Pose{axisAngle({1, 0, 0}, 90.5F), {}}, 1.0F, noisyState);
	check(near(screenAxis(nearPoleA, {1, 0, 0}), screenAxis(nearPoleB, {1, 0, 0})),
			"near-vertical tracking noise preserves screen heading");
}

void invalidInputIsTransactional() {
	RollFreeBasisState state{{0, 0, 1}, true, true};
	Pose output{{1, 2, 3, 4}, {5, 6, 7}};
	const RollFreeBasisState originalState = state;
	const Pose originalOutput = output;
	Pose invalid{{0, 0, 0, 0}, {0, 0, 0}};
	check(!computeGazeCenteredRollFreePose(invalid, 1.0F, state, output), "zero quaternion is rejected");
	check(near(state.previousRight, originalState.previousRight)
			&& state.hasPreviousRight == originalState.hasPreviousRight,
			"invalid quaternion preserves state");
	check(near(output.position, originalOutput.position)
			&& near(output.orientation, originalOutput.orientation),
			"invalid quaternion preserves output");

	invalid.orientation = {0, 0, 0, 1};
	invalid.position.x = std::numeric_limits<float>::quiet_NaN();
	check(!computeGazeCenteredRollFreePose(invalid, 1.0F, state, output), "NaN position is rejected");
	check(!computeGazeCenteredRollFreePose(Pose{}, 0.0F, state, output), "zero distance is rejected");
	check(!computeGazeCenteredRollFreePose(Pose{}, -1.0F, state, output), "negative distance is rejected");
	check(!computeGazeCenteredRollFreePose(
			Pose{{0, 0, std::numeric_limits<float>::infinity(), 1}, {}}, 1.0F, state, output),
			"infinite quaternion is rejected");
	check(!computeGazeCenteredRollFreePose(
			Pose{{0, 0, 0, 1}, {0, 0, -std::numeric_limits<float>::max()}},
			std::numeric_limits<float>::max(), state, output),
			"float-overflowing output position is rejected");
	check(near(state.previousRight, originalState.previousRight)
			&& state.hasPreviousRight == originalState.hasPreviousRight
			&& state.usingVerticalFallback == originalState.usingVerticalFallback
			&& near(output.position, originalOutput.position)
			&& near(output.orientation, originalOutput.orientation),
			"all rejected inputs preserve state and output");
}

} // namespace

int main() {
	Quaternion normalized;
	check(normalizeQuaternion({0, 0, 0, 2}, normalized) && near(normalized.w, 1.0F),
			"scaled quaternion normalizes");
	check(!normalizeQuaternion({0, 0, 0, 0}, normalized), "zero quaternion does not normalize");
	check(near(rotateVector(axisAngle({0, 1, 0}, 90), {0, 0, -1}), {-1, 0, 0}),
			"quaternion rotation follows OpenXR axes");

	neutralAndTranslation();
	yawAndPitch();
	rollIsRemoved();
	verticalFallback();
	invalidInputIsTransactional();

	if (failures != 0) {
		std::cerr << failures << " screen-pose math test(s) failed.\n";
		return 1;
	}
	std::cout << "All screen-pose math tests passed.\n";
	return 0;
}
