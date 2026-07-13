package dev.mcxrinput.presentation;

import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;

class PresentationStateTest {
	@Test
	void missingWorldOrPlayerTakesPriority() {
		assertEquals(PresentationState.NO_WORLD,
				PresentationState.classify(false, false, true, true));
		assertEquals(PresentationState.NO_WORLD,
				PresentationState.classify(true, false, false, false));
	}

	@Test
	void overlayTakesPriorityOverScreenAndWorld() {
		assertEquals(PresentationState.OVERLAY,
				PresentationState.classify(true, true, true, true));
		assertEquals(PresentationState.SCREEN,
				PresentationState.classify(true, true, true, false));
		assertEquals(PresentationState.WORLD,
				PresentationState.classify(true, true, false, false));
	}

	@Test
	void sequenceStartsAtOneAndResetsOnlyForAnotherSession() {
		PresentationStateSequencer sequencer = new PresentationStateSequencer();
		assertEquals(1L, sequencer.next("0123456789abcdef"));
		assertEquals(2L, sequencer.next("0123456789ABCDEF"));
		assertEquals(1L, sequencer.next("fedcba9876543210"));
	}
}
