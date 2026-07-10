package dev.mcxrinput.input;

import dev.mcxrinput.protocol.VrControllerState;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

class ControllerButtonTest {
	private static final VrControllerState LEFT = new VrControllerState(
			true, 0.25F, -0.5F, 0.3F, 0.7F,
			true, false, false, true, true, true);
	private static final VrControllerState RIGHT = new VrControllerState(
			true, -0.25F, 0.5F, 0.8F, 0.4F,
			false, true, true, false, false, false);

	@Test
	void samplesDigitalAndAnalogControlsFromTheConfiguredHand() {
		assertEquals(0.8F, ControllerButton.RIGHT_TRIGGER.sample(LEFT, RIGHT).value());
		assertEquals(0.7F, ControllerButton.LEFT_GRIP.sample(LEFT, RIGHT).value());
		assertEquals(1.0F, ControllerButton.RIGHT_A.sample(LEFT, RIGHT).value());
		assertEquals(1.0F, ControllerButton.LEFT_X.sample(LEFT, RIGHT).value());
	}

	@Test
	void inactiveAndUnboundControlsCannotAct() {
		ControllerButton.Sample unbound = ControllerButton.NONE.sample(LEFT, RIGHT);
		assertFalse(unbound.active());

		VrControllerState inactive = VrControllerState.INACTIVE;
		assertFalse(ControllerButton.RIGHT_TRIGGER.sample(LEFT, inactive).active());
	}

	@Test
	void stableIdsRoundTripAndUnknownIdsUseFallback() {
		assertEquals(ControllerButton.LEFT_TRIGGER,
				ControllerButton.fromId("left_trigger", ControllerButton.NONE));
		assertEquals(ControllerButton.RIGHT_A,
				ControllerButton.fromId("not_a_control", ControllerButton.RIGHT_A));
		assertTrue(ControllerButton.RIGHT_A.next() != ControllerButton.RIGHT_A);
	}
}
