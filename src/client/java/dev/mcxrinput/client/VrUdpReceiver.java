package dev.mcxrinput.client;

import com.google.gson.Gson;
import dev.mcxrinput.protocol.BridgeProtocolPolicy;
import dev.mcxrinput.protocol.PoseMath;
import dev.mcxrinput.protocol.VrControllerState;
import dev.mcxrinput.protocol.VrInputFrame;
import dev.mcxrinput.protocol.VrPose;
import dev.mcxrinput.presentation.PresentationOffer;
import dev.mcxrinput.presentation.PresentationOfferTracker;
import dev.mcxrinput.presentation.PresentationProtocol;
import dev.mcxrinput.presentation.PresentationState;
import dev.mcxrinput.presentation.PresentationStateSequencer;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.io.IOException;
import java.net.DatagramPacket;
import java.net.DatagramSocket;
import java.net.InetAddress;
import java.net.InetSocketAddress;
import java.net.SocketException;
import java.nio.charset.StandardCharsets;
import java.time.Duration;
import java.util.Optional;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicReference;

final class VrUdpReceiver implements AutoCloseable {
	static final int DEFAULT_PORT = 28771;
	static final Duration PRESENTATION_OFFER_MAXIMUM_AGE = Duration.ofMillis(500L);
	private static final int MAX_DATAGRAM_BYTES = 4096;
	private static final Logger LOGGER = LoggerFactory.getLogger("MCXRInput/Bridge");
	private static final Gson GSON = new Gson();

	private final AtomicBoolean running = new AtomicBoolean();
	private final AtomicReference<VrInputFrame> latestFrame = new AtomicReference<>();
	private final Object presentationLock = new Object();
	private final PresentationOfferTracker presentationOffers = new PresentationOfferTracker();
	private final PresentationStateSequencer presentationSequences = new PresentationStateSequencer();
	private final int port;
	private final boolean allowV1TestPoses;
	private InetSocketAddress presentationSender;
	private DatagramSocket socket;
	private Thread receiverThread;

	VrUdpReceiver(int port) {
		if (port < 1024 || port > 65535) {
			throw new IllegalArgumentException("UDP port must be between 1024 and 65535");
		}
		this.port = port;
		allowV1TestPoses = BridgeProtocolPolicy.allowV1TestPosesFromSystemProperty();
	}

	void start() throws IOException {
		if (!running.compareAndSet(false, true)) {
			return;
		}

		try {
			// Intentionally bind to IPv4 loopback only. The bridge is not a network service.
			InetAddress loopback = InetAddress.getByName("127.0.0.1");
			socket = new DatagramSocket(new InetSocketAddress(loopback, port));
			receiverThread = Thread.ofPlatform()
					.name("MCXRInput local bridge receiver")
					.daemon(true)
					.start(this::receiveLoop);
			LOGGER.info("Listening for local VR bridge data on 127.0.0.1:{}", port);
			if (allowV1TestPoses) {
				LOGGER.warn(
						"DEVELOPMENT TEST MODE: accepting synthetic protocol-v1 poses. "
								+ "Use this option only for singleplayer testing."
				);
			}
		} catch (IOException exception) {
			running.set(false);
			throw exception;
		}
	}

	VrPose latestFreshPose(Duration maximumAge, boolean multiplayerWorld) {
		VrInputFrame frame = latestFreshFrame(maximumAge, multiplayerWorld);
		if (frame == null || !frame.hmd().active()) {
			return null;
		}

		return frame.hmd();
	}

	VrInputFrame latestFreshFrame(Duration maximumAge, boolean multiplayerWorld) {
		VrInputFrame frame = latestFrame.get();
		if (frame == null || !BridgeProtocolPolicy.allowsInWorld(
				frame.protocolVersion(), multiplayerWorld)) {
			return null;
		}

		long age = System.nanoTime() - frame.receivedAtNanos();
		return age >= 0 && age <= maximumAge.toNanos() ? frame : null;
	}

	PresentationOffer latestFreshPresentationOffer() {
		long nowNanos = System.nanoTime();
		synchronized (presentationLock) {
			PresentationOfferTracker.Snapshot snapshot = presentationOffers.latestFresh(
					nowNanos, PRESENTATION_OFFER_MAXIMUM_AGE.toNanos());
			return snapshot == null ? null : snapshot.offer();
		}
	}

	void sendPresentationState(PresentationState state, int appliedFovMilliDegrees) {
		DatagramSocket activeSocket = socket;
		if (!running.get() || activeSocket == null || activeSocket.isClosed()) {
			return;
		}

		PresentationOffer offer;
		InetSocketAddress sender;
		long sequence;
		synchronized (presentationLock) {
			PresentationOfferTracker.Snapshot snapshot = presentationOffers.latestFresh(
					System.nanoTime(), PRESENTATION_OFFER_MAXIMUM_AGE.toNanos());
			if (snapshot == null || presentationSender == null) {
				return;
			}
			offer = snapshot.offer();
			sender = presentationSender;
			sequence = presentationSequences.next(offer.session());
		}

		byte[] message;
		try {
			message = PresentationProtocol.formatState(
					offer.session(), sequence, offer.revision(), state, appliedFovMilliDegrees);
		} catch (IllegalArgumentException exception) {
			LOGGER.debug("Could not format local presentation state", exception);
			return;
		}

		try {
			activeSocket.send(new DatagramPacket(message, message.length, sender));
		} catch (IOException exception) {
			if (running.get()) {
				LOGGER.debug("Could not send local presentation state to the bridge", exception);
			}
		}
	}

	private void receiveLoop() {
		byte[] buffer = new byte[MAX_DATAGRAM_BYTES];
		while (running.get()) {
			DatagramPacket packet = new DatagramPacket(buffer, buffer.length);
			try {
				socket.receive(packet);
				parse(packet);
			} catch (SocketException exception) {
				if (running.get()) {
					LOGGER.warn("Local bridge socket stopped unexpectedly", exception);
				}
				return;
			} catch (IOException exception) {
				LOGGER.debug("Ignoring unreadable local bridge datagram", exception);
			}
		}
	}

	private void parse(DatagramPacket packet) {
		if (packet.getLength() == MAX_DATAGRAM_BYTES) {
			LOGGER.debug("Ignoring oversized local bridge datagram");
			return;
		}

		if (PresentationProtocol.hasPresentationPrefix(
				packet.getData(), packet.getOffset(), packet.getLength())) {
			parsePresentationOffer(packet);
			return;
		}

		try {
			String json = new String(packet.getData(), packet.getOffset(), packet.getLength(), StandardCharsets.UTF_8);
			BridgeMessage message = GSON.fromJson(json, BridgeMessage.class);
			if (message == null || !BridgeProtocolPolicy.accepts(message.version, allowV1TestPoses)
					|| message.hmd == null
					|| message.hmd.rotation == null || message.hmd.rotation.length != 4) {
				return;
			}

			double[] rotation = message.hmd.rotation;
			if (!PoseMath.isPlausibleQuaternion(rotation[0], rotation[1], rotation[2], rotation[3])) {
				return;
			}

			// Missing tracking state is never treated as permission to move or act.
			boolean active = message.hmd.active != null && message.hmd.active;
			long receivedAtNanos = System.nanoTime();
			VrPose pose = new VrPose(
					rotation[0], rotation[1], rotation[2], rotation[3],
					message.timestamp, receivedAtNanos, active
			);
			VrControllerState left = VrControllerState.INACTIVE;
			VrControllerState right = VrControllerState.INACTIVE;
			if (message.version == 2 && message.controllers != null) {
				left = parseController(message.controllers.left);
				right = parseController(message.controllers.right);
			}
			latestFrame.set(new VrInputFrame(
					message.version, pose, left, right, receivedAtNanos));
		} catch (RuntimeException exception) {
			LOGGER.debug("Ignoring malformed local bridge datagram", exception);
		}
	}

	private void parsePresentationOffer(DatagramPacket packet) {
		if (!isIpv4Loopback(packet.getAddress())) {
			return;
		}
		Optional<PresentationOffer> parsed = PresentationProtocol.parseOffer(
				packet.getData(), packet.getOffset(), packet.getLength());
		if (parsed.isEmpty()) {
			LOGGER.debug("Ignoring malformed local presentation offer");
			return;
		}

		InetSocketAddress sender = new InetSocketAddress(packet.getAddress(), packet.getPort());
		synchronized (presentationLock) {
			PresentationOfferTracker.Snapshot existing = presentationOffers.latest();
			if (existing != null && parsed.get().sameSession(existing.offer())
					&& presentationSender != null && !presentationSender.equals(sender)) {
				return;
			}
			if (presentationOffers.accept(parsed.get(), System.nanoTime())) {
				// Replies go only to the exact loopback endpoint that supplied the
				// newest accepted offer; controls-only bridges send no offer at all.
				presentationSender = sender;
			}
		}
	}

	private static boolean isIpv4Loopback(InetAddress address) {
		return address != null && "127.0.0.1".equals(address.getHostAddress());
	}

	private static VrControllerState parseController(ControllerMessage controller) {
		if (controller == null || controller.active == null || !controller.active) {
			return VrControllerState.INACTIVE;
		}
		if (controller.stick == null || controller.stick.length != 2
				|| !Float.isFinite(controller.stick[0]) || !Float.isFinite(controller.stick[1])) {
			return VrControllerState.INACTIVE;
		}

		float trigger = finiteClamped(controller.trigger, 0.0F, 0.0F, 1.0F);
		float squeeze = finiteClamped(controller.squeeze, 0.0F, 0.0F, 1.0F);
		return new VrControllerState(
				true,
				finiteClamped(controller.stick[0], 0.0F, -1.0F, 1.0F),
				finiteClamped(controller.stick[1], 0.0F, -1.0F, 1.0F),
				trigger,
				squeeze,
				controller.stickClick != null && controller.stickClick,
				controller.a != null && controller.a,
				controller.b != null && controller.b,
				controller.x != null && controller.x,
				controller.y != null && controller.y,
				controller.menu != null && controller.menu
		);
	}

	private static float finiteClamped(Float value, float fallback, float minimum, float maximum) {
		if (value == null || !Float.isFinite(value)) {
			return fallback;
		}
		return Math.max(minimum, Math.min(maximum, value));
	}

	@Override
	public void close() {
		if (!running.compareAndSet(true, false)) {
			return;
		}

		if (socket != null) {
			socket.close();
		}
		if (receiverThread != null) {
			try {
				receiverThread.join(250L);
			} catch (InterruptedException exception) {
				Thread.currentThread().interrupt();
			}
		}
	}

	private static final class BridgeMessage {
		int version;
		long timestamp;
		HmdMessage hmd;
		ControllersMessage controllers;
	}

	private static final class HmdMessage {
		double[] rotation;
		Boolean active;
	}

	private static final class ControllersMessage {
		ControllerMessage left;
		ControllerMessage right;
	}

	private static final class ControllerMessage {
		Boolean active;
		float[] stick;
		Float trigger;
		Float squeeze;
		Boolean stickClick;
		Boolean a;
		Boolean b;
		Boolean x;
		Boolean y;
		Boolean menu;
	}
}
