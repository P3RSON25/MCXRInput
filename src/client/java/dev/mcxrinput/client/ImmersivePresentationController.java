package dev.mcxrinput.client;

import dev.mcxrinput.presentation.PresentationOffer;
import dev.mcxrinput.presentation.PresentationProtocol;
import dev.mcxrinput.presentation.PresentationState;
import net.minecraft.client.Minecraft;

import java.util.concurrent.atomic.AtomicInteger;

/**
 * Main-thread display handshake for the optional unified bridge. It changes no
 * gameplay state: the only render change is an exact, short-lived FOV lock that
 * keeps the captured source and the native projection calibration in agreement.
 */
final class ImmersivePresentationController {
	private final VrUdpReceiver receiver;
	private final AtomicInteger lastAppliedFovMilliDegrees = new AtomicInteger();

	ImmersivePresentationController(VrUdpReceiver receiver) {
		this.receiver = receiver;
	}

	void tick(Minecraft client) {
		PresentationState state = classify(client);
		int appliedFov = lastAppliedFovMilliDegrees.get();
		if (appliedFov <= 0) {
			// Before the first camera calculation, report Minecraft's configured
			// baseline rather than falsely acknowledging the bridge-requested value.
			appliedFov = PresentationProtocol.toFovMilliDegrees(client.options.fov().get());
		}
		receiver.sendPresentationState(state, appliedFov);
	}

	float applyFov(Minecraft client, float vanillaFov) {
		float appliedFov = vanillaFov;
		PresentationOffer offer = receiver.latestFreshPresentationOffer();
		if (offer != null && classify(client) == PresentationState.WORLD) {
			// Returning the exact offered value also disables sprint/fluid/death FOV
			// effects while immersive projection is active. Frame-varying FOV would
			// invalidate the bridge's frozen source-to-eye mapping.
			appliedFov = offer.sourceFovDegrees();
		}
		lastAppliedFovMilliDegrees.set(PresentationProtocol.toFovMilliDegrees(appliedFov));
		return appliedFov;
	}

	static PresentationState classify(Minecraft client) {
		return PresentationState.classify(
				client.level != null,
				client.player != null,
				client.gui.screen() != null,
				client.gui.overlay() != null);
	}
}
