package dev.mcxrinput.presentation;

import org.junit.jupiter.api.Test;

import java.nio.charset.StandardCharsets;
import java.util.List;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

class PresentationProtocolTest {
	@Test
	void parsesStrictOfferAndPreservesSessionSpelling() {
		PresentationOffer offer = parse(
				"MCXRD1 OFFER A1b2C3d4E5f60718 18446744073709551615 110000 60 90");
		assertEquals("A1b2C3d4E5f60718", offer.session());
		assertEquals("18446744073709551615", Long.toUnsignedString(offer.revision()));
		assertEquals(110_000, offer.sourceFovMilliDegrees());
		assertEquals(60, offer.hudHorizontalPermille());
		assertEquals(90, offer.hudVerticalPermille());
		assertEquals(110.0F, offer.sourceFovDegrees());
		assertEquals(0.06, offer.hudHorizontalInset());
		assertEquals(0.09, offer.hudVerticalInset());
	}

	@Test
	void acceptsExpandedRenderFovUpperBoundary() {
		PresentationOffer offer = parse(
				"MCXRD1 OFFER 0123456789abcdef 2 130000 450 450");
		assertEquals(130_000, offer.sourceFovMilliDegrees());
		assertEquals(130.0F, offer.sourceFovDegrees());
		assertEquals(450, offer.hudHorizontalPermille());
		assertEquals(450, offer.hudVerticalPermille());
	}

	@Test
	void rejectsMalformedOrOutOfRangeOffers() {
		for (String message : List.of(
				"MCXRD1  OFFER 0123456789abcdef 1 110000 60 90",
				"MCXRD1\tOFFER 0123456789abcdef 1 110000 60 90",
				"MCXRD1 OFFER 0123456789abcdeg 1 110000 60 90",
				"MCXRD1 OFFER 0123456789abcdef -1 110000 60 90",
				"MCXRD1 OFFER 0123456789abcdef 18446744073709551616 110000 60 90",
				"MCXRD1 OFFER 0123456789abcdef 1 29999 60 90",
				"MCXRD1 OFFER 0123456789abcdef 1 130001 60 90",
				"MCXRD1 OFFER 0123456789abcdef 1 110000 451 90",
				"MCXRD1 OFFER 0123456789abcdef 1 110000 60 451",
				"MCXRD1 OFFER 0123456789abcdef 1 110000 60 90 extra",
				"MCXRD1 STATE 0123456789abcdef 1 110000 60 90",
				"MCXRD1 OFFER 0123456789abcdef 1 110000 60 90\n")) {
			byte[] bytes = message.getBytes(StandardCharsets.UTF_8);
			assertTrue(PresentationProtocol.parseOffer(bytes, 0, bytes.length).isEmpty(), message);
		}

		byte[] nonAscii = "MCXRD1 OFFER 0123456789abcdef 1 110000 60 é"
				.getBytes(StandardCharsets.UTF_8);
		assertTrue(PresentationProtocol.parseOffer(nonAscii, 0, nonAscii.length).isEmpty());
	}

	@Test
	void enforcesBoundedSlicesAndRecognizesPrefixBeforeJson() {
		byte[] framed = "xMCXRD1 OFFER 0123456789abcdef 7 30000 0 450y"
				.getBytes(StandardCharsets.US_ASCII);
		assertTrue(PresentationProtocol.hasPresentationPrefix(framed, 1, framed.length - 2));
		assertEquals(7L, PresentationProtocol.parseOffer(framed, 1, framed.length - 2)
				.orElseThrow().revision());
		assertFalse(PresentationProtocol.hasPresentationPrefix(framed, -1, framed.length));
		assertTrue(PresentationProtocol.parseOffer(new byte[161], 0, 161).isEmpty());
	}

	@Test
	void formatsStateWithUnsignedCountersAndExactSessionToken() {
		byte[] state = PresentationProtocol.formatState(
				"ABCDEF0123456789", -1L, Long.MIN_VALUE,
				PresentationState.SCREEN, 110_000);
		assertArrayEquals(
				"MCXRD1 STATE ABCDEF0123456789 18446744073709551615 9223372036854775808 SCREEN 110000"
						.getBytes(StandardCharsets.US_ASCII),
				state);
	}

	@Test
	void convertsOnlyFiniteRepresentableFovValues() {
		assertEquals(110_125, PresentationProtocol.toFovMilliDegrees(110.125F));
		assertEquals(0, PresentationProtocol.toFovMilliDegrees(Float.NaN));
		assertEquals(0, PresentationProtocol.toFovMilliDegrees(-5.0F));
		assertEquals(360_000, PresentationProtocol.toFovMilliDegrees(500.0F));
	}

	private static PresentationOffer parse(String message) {
		byte[] bytes = message.getBytes(StandardCharsets.US_ASCII);
		return PresentationProtocol.parseOffer(bytes, 0, bytes.length).orElseThrow();
	}
}
