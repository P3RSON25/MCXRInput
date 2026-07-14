package dev.mcxrinput.presentation;

import java.nio.charset.StandardCharsets;
import java.util.Optional;

/** Strict parser/formatter for the display-only MCXRD1 loopback protocol. */
public final class PresentationProtocol {
	public static final String PREFIX = "MCXRD1";
	public static final int MAX_OFFER_BYTES = 160;
	private static final byte[] PREFIX_BYTES = PREFIX.getBytes(StandardCharsets.US_ASCII);

	private PresentationProtocol() {
	}

	public static boolean hasPresentationPrefix(byte[] bytes, int offset, int length) {
		if (!validSlice(bytes, offset, length) || length < PREFIX_BYTES.length) {
			return false;
		}
		for (int index = 0; index < PREFIX_BYTES.length; index++) {
			if (bytes[offset + index] != PREFIX_BYTES[index]) {
				return false;
			}
		}
		return true;
	}

	public static Optional<PresentationOffer> parseOffer(byte[] bytes, int offset, int length) {
		if (!isBoundedAsciiMessage(bytes, offset, length)) {
			return Optional.empty();
		}

		String message = new String(bytes, offset, length, StandardCharsets.US_ASCII);
		String[] fields = message.split(" ", -1);
		if (fields.length != 7 || !PREFIX.equals(fields[0]) || !"OFFER".equals(fields[1])) {
			return Optional.empty();
		}

		try {
			if (!PresentationOffer.isSessionToken(fields[2])
					|| !isUnsignedDecimal(fields[3])
					|| !isUnsignedDecimal(fields[4])
					|| !isUnsignedDecimal(fields[5])
					|| !isUnsignedDecimal(fields[6])) {
				return Optional.empty();
			}
			long revision = Long.parseUnsignedLong(fields[3]);
			int sourceFov = Integer.parseInt(fields[4]);
			int hudHorizontal = Integer.parseInt(fields[5]);
			int hudVertical = Integer.parseInt(fields[6]);
			return Optional.of(new PresentationOffer(
					fields[2], revision, sourceFov, hudHorizontal, hudVertical));
		} catch (IllegalArgumentException exception) {
			return Optional.empty();
		}
	}

	public static Optional<PresentationCalibration> parseCalibration(
			byte[] bytes, int offset, int length) {
		if (!isBoundedAsciiMessage(bytes, offset, length)) {
			return Optional.empty();
		}

		String message = new String(bytes, offset, length, StandardCharsets.US_ASCII);
		String[] fields = message.split(" ", -1);
		if (fields.length != 5
				|| !PREFIX.equals(fields[0])
				|| !"CALIBRATION".equals(fields[1])) {
			return Optional.empty();
		}

		try {
			if (!PresentationOffer.isSessionToken(fields[2])
					|| !isUnsignedDecimal(fields[3])
					|| !isUnsignedDecimal(fields[4])) {
				return Optional.empty();
			}
			return Optional.of(new PresentationCalibration(
					fields[2],
					Long.parseUnsignedLong(fields[3]),
					Integer.parseInt(fields[4])));
		} catch (IllegalArgumentException exception) {
			return Optional.empty();
		}
	}

	public static byte[] formatState(
			String session,
			long sequence,
			long revision,
			PresentationState state,
			int appliedFovMilliDegrees) {
		if (!PresentationOffer.isSessionToken(session)) {
			throw new IllegalArgumentException("Invalid presentation session");
		}
		if (state == null) {
			throw new IllegalArgumentException("Presentation state is required");
		}
		if (appliedFovMilliDegrees < 0 || appliedFovMilliDegrees > 360_000) {
			throw new IllegalArgumentException("Applied FOV is outside the representable range");
		}
		String message = PREFIX + " STATE " + session
				+ " " + Long.toUnsignedString(sequence)
				+ " " + Long.toUnsignedString(revision)
				+ " " + state.name()
				+ " " + appliedFovMilliDegrees;
		return message.getBytes(StandardCharsets.US_ASCII);
	}

	public static int toFovMilliDegrees(float degrees) {
		if (!Float.isFinite(degrees)) {
			return 0;
		}
		return Math.max(0, Math.min(360_000, Math.round(degrees * 1000.0F)));
	}

	private static boolean validSlice(byte[] bytes, int offset, int length) {
		return bytes != null && offset >= 0 && length >= 0 && offset <= bytes.length - length;
	}

	private static boolean isBoundedAsciiMessage(
			byte[] bytes, int offset, int length) {
		if (!validSlice(bytes, offset, length) || length <= 0 || length > MAX_OFFER_BYTES) {
			return false;
		}
		for (int index = 0; index < length; index++) {
			int value = bytes[offset + index] & 0xFF;
			if (value < 0x20 || value > 0x7E) {
				return false;
			}
		}
		return true;
	}

	private static boolean isUnsignedDecimal(String value) {
		if (value == null || value.isEmpty()) {
			return false;
		}
		for (int index = 0; index < value.length(); index++) {
			char character = value.charAt(index);
			if (character < '0' || character > '9') {
				return false;
			}
		}
		return true;
	}
}
