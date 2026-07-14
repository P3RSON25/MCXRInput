package dev.mcxrinput.presentation;

/**
 * Monotonic-time freshness and revision ordering for display calibration.
 * Endpoint ownership remains with the loopback receiver.
 */
public final class PresentationCalibrationTracker {
	private Snapshot latest;

	public boolean accept(PresentationCalibration calibration, long receivedAtNanos) {
		if (calibration == null) {
			return false;
		}
		if (latest != null
				&& calibration.session().equalsIgnoreCase(latest.calibration().session())) {
			int revisionOrder = Long.compareUnsigned(
					calibration.revision(), latest.calibration().revision());
			if (revisionOrder < 0) {
				return false;
			}
			if (revisionOrder == 0) {
				if (calibration.worldViewScalePermille()
						!= latest.calibration().worldViewScalePermille()) {
					return false;
				}
				latest = new Snapshot(latest.calibration(), receivedAtNanos);
				return true;
			}
		}
		latest = new Snapshot(calibration, receivedAtNanos);
		return true;
	}

	public Snapshot latest() {
		return latest;
	}

	public Snapshot latestFreshMatching(
			PresentationOffer offer,
			long nowNanos,
			long maximumAgeNanos) {
		if (latest == null || offer == null || maximumAgeNanos < 0
				|| !latest.calibration().matches(offer)) {
			return null;
		}
		long age = nowNanos - latest.receivedAtNanos();
		return age >= 0 && age <= maximumAgeNanos ? latest : null;
	}

	public record Snapshot(
			PresentationCalibration calibration,
			long receivedAtNanos) {
		public Snapshot {
			if (calibration == null) {
				throw new IllegalArgumentException("Presentation calibration is required");
			}
		}
	}
}
