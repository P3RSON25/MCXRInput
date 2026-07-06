package dev.mcxrinput.client;

import com.google.gson.Gson;
import dev.mcxrinput.protocol.PoseMath;
import dev.mcxrinput.protocol.VrPose;
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
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicReference;

final class VrUdpReceiver implements AutoCloseable {
	static final int DEFAULT_PORT = 28771;
	private static final int MAX_DATAGRAM_BYTES = 4096;
	private static final Logger LOGGER = LoggerFactory.getLogger("MCXRInput/Bridge");
	private static final Gson GSON = new Gson();

	private final AtomicBoolean running = new AtomicBoolean();
	private final AtomicReference<VrPose> latestPose = new AtomicReference<>();
	private final int port;
	private DatagramSocket socket;
	private Thread receiverThread;

	VrUdpReceiver(int port) {
		if (port < 1024 || port > 65535) {
			throw new IllegalArgumentException("UDP port must be between 1024 and 65535");
		}
		this.port = port;
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
		} catch (IOException exception) {
			running.set(false);
			throw exception;
		}
	}

	VrPose latestFreshPose(Duration maximumAge) {
		VrPose pose = latestPose.get();
		if (pose == null || !pose.active()) {
			return null;
		}

		long age = System.nanoTime() - pose.receivedAtNanos();
		return age >= 0 && age <= maximumAge.toNanos() ? pose : null;
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

		try {
			String json = new String(packet.getData(), packet.getOffset(), packet.getLength(), StandardCharsets.UTF_8);
			BridgeMessage message = GSON.fromJson(json, BridgeMessage.class);
			if (message == null || message.version != 1 || message.hmd == null
					|| message.hmd.rotation == null || message.hmd.rotation.length != 4) {
				return;
			}

			double[] rotation = message.hmd.rotation;
			if (!PoseMath.isPlausibleQuaternion(rotation[0], rotation[1], rotation[2], rotation[3])) {
				return;
			}

			boolean active = message.hmd.active == null || message.hmd.active;
			latestPose.set(new VrPose(
					rotation[0], rotation[1], rotation[2], rotation[3],
					message.timestamp, System.nanoTime(), active
			));
		} catch (RuntimeException exception) {
			LOGGER.debug("Ignoring malformed local bridge datagram", exception);
		}
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
	}

	private static final class HmdMessage {
		double[] rotation;
		Boolean active;
	}
}
