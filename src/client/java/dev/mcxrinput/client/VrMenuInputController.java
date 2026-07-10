package dev.mcxrinput.client;

import dev.mcxrinput.input.AnalogButtonLatch;
import dev.mcxrinput.input.ControllerButton;
import dev.mcxrinput.input.ControllerStick;
import dev.mcxrinput.input.StickDpadRepeater;
import dev.mcxrinput.protocol.VrControllerState;
import dev.mcxrinput.protocol.VrInputFrame;
import net.minecraft.client.InputType;
import net.minecraft.client.Minecraft;
import net.minecraft.client.gui.screens.Screen;
import net.minecraft.client.gui.screens.inventory.AbstractContainerScreen;
import net.minecraft.client.input.KeyEvent;
import org.lwjgl.glfw.GLFW;

import java.time.Duration;

/** Uses Minecraft's native keyboard-focus navigation instead of a virtual mouse. */
final class VrMenuInputController {
	private static final Duration MAXIMUM_FRAME_AGE = Duration.ofMillis(250);
	private static final float BUTTON_RELEASE_MARGIN = 0.10F;
	private static final int INITIAL_REPEAT_DELAY_TICKS = 8;
	private static final int REPEAT_INTERVAL_TICKS = 3;

	private final VrUdpReceiver receiver;
	private final MCXRInputConfig config;
	private final StickDpadRepeater navigation = new StickDpadRepeater(
			INITIAL_REPEAT_DELAY_TICKS, REPEAT_INTERVAL_TICKS);
	private final AnalogButtonLatch confirmButton = new AnalogButtonLatch(BUTTON_RELEASE_MARGIN);
	private final AnalogButtonLatch backButton = new AnalogButtonLatch(BUTTON_RELEASE_MARGIN);
	private Screen lastScreen;
	private boolean confirmWasDown;
	private boolean backWasDown;

	VrMenuInputController(VrUdpReceiver receiver, MCXRInputConfig config) {
		this.receiver = receiver;
		this.config = config;
	}

	void tick(Minecraft client, boolean inputEnabled) {
		Screen screen = client.gui.screen();
		if (!inputEnabled || screen == null || client.gui.overlay() != null
				|| screen instanceof AbstractContainerScreen<?>) {
			suppressInputs();
			lastScreen = screen;
			return;
		}

		if (screen != lastScreen) {
			suppressInputs();
			lastScreen = screen;
		}

		VrInputFrame frame = receiver.latestFreshFrame(MAXIMUM_FRAME_AGE);
		if (frame == null || !frame.hmd().active()) {
			suppressInputs();
			return;
		}

		VrControllerState left = frame.leftController();
		VrControllerState right = frame.rightController();
		if (!left.active() && !right.active()) {
			suppressInputs();
			return;
		}

		ControllerStick navigationStick = config.menuNavigationStick();
		VrControllerState stick = navigationStick.select(left, right);
		StickDpadRepeater.Direction direction = navigation.update(
				stick.active(), stick.stickX(), stick.stickY(), (float) config.controllerDeadzone());

		float buttonThreshold = (float) config.triggerThreshold();
		boolean confirmDown = bindingDown(
				config.menuConfirmBinding(), left, right, confirmButton, buttonThreshold);
		boolean backDown = bindingDown(
				config.menuBackBinding(), left, right, backButton, buttonThreshold);
		boolean confirmPressed = confirmDown && !confirmWasDown;
		boolean backPressed = backDown && !backWasDown;
		confirmWasDown = confirmDown;
		backWasDown = backDown;

		// Process at most one discrete GUI action per tick. This mirrors ordinary
		// keyboard navigation and cannot create gameplay actions or packets.
		if (confirmPressed) {
			dispatchKey(screen, GLFW.GLFW_KEY_ENTER);
		} else if (backPressed) {
			dispatchKey(screen, GLFW.GLFW_KEY_ESCAPE);
		} else if (direction != null) {
			client.setLastInputType(InputType.KEYBOARD_ARROW);
			dispatchKey(screen, keyFor(direction));
		}
	}

	void releaseAll() {
		suppressInputs();
		lastScreen = null;
	}

	private void suppressInputs() {
		navigation.suppress();
		confirmButton.suppress();
		backButton.suppress();
		confirmWasDown = false;
		backWasDown = false;
	}

	private static boolean bindingDown(
			ControllerButton binding,
			VrControllerState left,
			VrControllerState right,
			AnalogButtonLatch latch,
			float threshold
	) {
		ControllerButton.Sample sample = binding.sample(left, right);
		return latch.update(sample.active(), sample.value(), threshold);
	}

	private static void dispatchKey(Screen screen, int key) {
		screen.afterKeyboardAction();
		screen.keyPressed(new KeyEvent(key, 0, 0));
	}

	private static int keyFor(StickDpadRepeater.Direction direction) {
		return switch (direction) {
			case UP -> GLFW.GLFW_KEY_UP;
			case DOWN -> GLFW.GLFW_KEY_DOWN;
			case LEFT -> GLFW.GLFW_KEY_LEFT;
			case RIGHT -> GLFW.GLFW_KEY_RIGHT;
		};
	}
}
