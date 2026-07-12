package dev.mcxrinput.input;

import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

class MovementStickGateTest {
	@Test
	void suppressionRequiresNeutralBeforeMovementReturns() {
		MovementStickGate gate = new MovementStickGate();
		assertFalse(gate.accepts(true, 0.0F, 1.0F, 0.35F));
		assertFalse(gate.accepts(true, 0.0F, 0.0F, 0.35F));
		assertTrue(gate.accepts(true, 0.0F, 1.0F, 0.35F));

		gate.suppress();
		assertFalse(gate.accepts(true, 0.0F, 1.0F, 0.35F));
		assertFalse(gate.accepts(true, 0.0F, 0.0F, 0.35F));
		assertTrue(gate.accepts(true, 0.0F, 1.0F, 0.35F));
	}
}
