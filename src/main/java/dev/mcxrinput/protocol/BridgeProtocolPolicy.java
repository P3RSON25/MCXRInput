package dev.mcxrinput.protocol;

/**
 * Defines which local bridge protocol versions production builds accept.
 *
 * <p>Protocol v1 carries synthetic HMD-only test poses. It is intentionally
 * disabled unless a developer explicitly opts in for singleplayer testing.
 * The native OpenXR bridge uses protocol v2.</p>
 */
public final class BridgeProtocolPolicy {
	public static final String ALLOW_V1_TEST_POSES_PROPERTY =
			"mcxrinput.development.allowProtocolV1TestPoses";

	private BridgeProtocolPolicy() {
	}

	public static boolean allowV1TestPosesFromSystemProperty() {
		return Boolean.parseBoolean(System.getProperty(ALLOW_V1_TEST_POSES_PROPERTY, "false"));
	}

	public static boolean accepts(int version, boolean allowV1TestPoses) {
		return version == 2 || (version == 1 && allowV1TestPoses);
	}

	/** Development v1 frames are never eligible to control a multiplayer world. */
	public static boolean allowsInWorld(int version, boolean multiplayerWorld) {
		return version == 2 || (version == 1 && !multiplayerWorld);
	}
}
