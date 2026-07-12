package dev.mcxrinput.protocol;

import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.parallel.ResourceLock;

import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

@ResourceLock("systemProperties")
class BridgeProtocolPolicyTest {
	private String originalProperty;
	private boolean propertyChanged;

	@AfterEach
	void restoreSystemProperty() {
		if (!propertyChanged) {
			return;
		}
		if (originalProperty == null) {
			System.clearProperty(BridgeProtocolPolicy.ALLOW_V1_TEST_POSES_PROPERTY);
		} else {
			System.setProperty(BridgeProtocolPolicy.ALLOW_V1_TEST_POSES_PROPERTY, originalProperty);
		}
	}

	@Test
	void productionDefaultRejectsProtocolV1TestPoses() {
		rememberProperty();
		System.clearProperty(BridgeProtocolPolicy.ALLOW_V1_TEST_POSES_PROPERTY);

		boolean allowV1 = BridgeProtocolPolicy.allowV1TestPosesFromSystemProperty();
		assertFalse(allowV1);
		assertFalse(BridgeProtocolPolicy.accepts(1, allowV1));
		assertTrue(BridgeProtocolPolicy.accepts(2, allowV1));
	}

	@Test
	void explicitDevelopmentOptionAllowsProtocolV1() {
		rememberProperty();
		System.setProperty(BridgeProtocolPolicy.ALLOW_V1_TEST_POSES_PROPERTY, "true");

		boolean allowV1 = BridgeProtocolPolicy.allowV1TestPosesFromSystemProperty();
		assertTrue(allowV1);
		assertTrue(BridgeProtocolPolicy.accepts(1, allowV1));
		assertTrue(BridgeProtocolPolicy.accepts(2, allowV1));
		assertTrue(BridgeProtocolPolicy.allowsInWorld(1, false));
		assertFalse(BridgeProtocolPolicy.allowsInWorld(1, true));
		assertTrue(BridgeProtocolPolicy.allowsInWorld(2, true));
	}

	@Test
	void unknownProtocolVersionsRemainRejected() {
		assertFalse(BridgeProtocolPolicy.accepts(0, true));
		assertFalse(BridgeProtocolPolicy.accepts(3, true));
	}

	private void rememberProperty() {
		originalProperty = System.getProperty(BridgeProtocolPolicy.ALLOW_V1_TEST_POSES_PROPERTY);
		propertyChanged = true;
	}
}
