package dev.mcxrinput.client;

import dev.mcxrinput.input.AnalogButtonLatch;
import dev.mcxrinput.input.ControllerButton;
import dev.mcxrinput.input.ControllerStick;
import dev.mcxrinput.input.SlotNavigation;
import dev.mcxrinput.input.StickDpadRepeater;
import dev.mcxrinput.mixin.client.AbstractContainerScreenAccessor;
import dev.mcxrinput.mixin.client.MouseHandlerAccessor;
import dev.mcxrinput.protocol.VrControllerState;
import dev.mcxrinput.protocol.VrInputFrame;
import net.minecraft.client.Minecraft;
import net.minecraft.client.gui.screens.Screen;
import net.minecraft.client.gui.screens.inventory.AbstractContainerScreen;
import net.minecraft.world.inventory.ContainerInput;
import net.minecraft.world.inventory.Slot;
import org.lwjgl.glfw.GLFW;

import java.time.Duration;
import java.util.List;

/**
 * Provides a snapped, D-pad-style cursor for vanilla container slots.
 * Every inventory action is a single physical press routed through the
 * container screen's normal slot-click implementation.
 */
final class VrInventoryInputController {
	private static final Duration MAXIMUM_FRAME_AGE = Duration.ofMillis(250);
	private static final float BUTTON_RELEASE_MARGIN = 0.10F;
	private static final int INITIAL_REPEAT_DELAY_TICKS = 8;
	private static final int REPEAT_INTERVAL_TICKS = 3;

	private final VrUdpReceiver receiver;
	private final MCXRInputConfig config;
	private final StickDpadRepeater navigation = new StickDpadRepeater(
			INITIAL_REPEAT_DELAY_TICKS, REPEAT_INTERVAL_TICKS);
	private final ButtonEdge selectButton = new ButtonEdge();
	private final ButtonEdge quickMoveButton = new ButtonEdge();
	private final ButtonEdge takeHalfButton = new ButtonEdge();
	private final ButtonEdge dropButton = new ButtonEdge();
	private final ButtonEdge backButton = new ButtonEdge();
	private Screen lastScreen;
	private Slot selectedSlot;

	VrInventoryInputController(VrUdpReceiver receiver, MCXRInputConfig config) {
		this.receiver = receiver;
		this.config = config;
	}

	void tick(Minecraft client, boolean inputEnabled) {
		Screen currentScreen = client.gui.screen();
		if (!inputEnabled || client.gui.overlay() != null
				|| !(currentScreen instanceof AbstractContainerScreen<?> screen)) {
			releaseAll();
			return;
		}

		if (screen != lastScreen) {
			suppressInputs();
			selectedSlot = null;
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

		List<Slot> slots = screen.getMenu().slots.stream()
				.filter(Slot::isActive)
				.filter(Slot::isHighlightable)
				.toList();
		if (slots.isEmpty()) {
			selectedSlot = null;
			return;
		}

		AbstractContainerScreenAccessor accessor = (AbstractContainerScreenAccessor) screen;
		if (selectedSlot == null || !slots.contains(selectedSlot)) {
			selectedSlot = nearestSlot(client, accessor, slots);
			snapCursor(client, accessor, selectedSlot);
		} else if (accessor.mcxrinput$hoveredSlot() != null
				&& slots.contains(accessor.mcxrinput$hoveredSlot())) {
			// Keep physical mouse use interoperable with the snapped controller cursor.
			selectedSlot = accessor.mcxrinput$hoveredSlot();
		}

		ControllerStick navigationStick = config.menuNavigationStick();
		VrControllerState stick = navigationStick.select(left, right);
		StickDpadRepeater.Direction direction = navigation.update(
				stick.active(), stick.stickX(), stick.stickY(), (float) config.controllerDeadzone());
		if (direction != null) {
			selectedSlot = nextSlot(accessor, slots, selectedSlot, direction);
			snapCursor(client, accessor, selectedSlot);
		}

		float threshold = (float) config.triggerThreshold();
		boolean selectPressed = selectButton.update(
				config.inventorySelectBinding(), left, right, threshold);
		boolean quickMovePressed = quickMoveButton.update(
				config.inventoryQuickMoveBinding(), left, right, threshold);
		boolean takeHalfPressed = takeHalfButton.update(
				config.inventoryTakeHalfBinding(), left, right, threshold);
		boolean dropPressed = dropButton.update(
				config.inventoryDropBinding(), left, right, threshold);
		boolean backPressed = backButton.update(
				config.menuBackBinding(), left, right, threshold);

		// The action values match vanilla mouse handling: button 0 pickup/place,
		// button 1 take/place half, QUICK_MOVE for shift-click, and slot -999 for
		// dropping the currently carried stack outside the container.
		if (backPressed) {
			screen.onClose();
		} else if (selectPressed) {
			accessor.mcxrinput$slotClicked(
					selectedSlot, selectedSlot.index, 0, ContainerInput.PICKUP);
		} else if (dropPressed && !screen.getMenu().getCarried().isEmpty()) {
			accessor.mcxrinput$slotClicked(null, -999, 0, ContainerInput.PICKUP);
		} else if (quickMovePressed && screen.getMenu().getCarried().isEmpty()) {
			accessor.mcxrinput$slotClicked(
					selectedSlot, selectedSlot.index, 0, ContainerInput.QUICK_MOVE);
		} else if (takeHalfPressed) {
			accessor.mcxrinput$slotClicked(
					selectedSlot, selectedSlot.index, 1, ContainerInput.PICKUP);
		}
	}

	void releaseAll() {
		suppressInputs();
		lastScreen = null;
		selectedSlot = null;
	}

	private void suppressInputs() {
		navigation.suppress();
		selectButton.suppress();
		quickMoveButton.suppress();
		takeHalfButton.suppress();
		dropButton.suppress();
		backButton.suppress();
	}

	private static Slot nearestSlot(
			Minecraft client,
			AbstractContainerScreenAccessor accessor,
			List<Slot> slots
	) {
		List<SlotNavigation.Point> points = slotPoints(accessor, slots);
		int index = SlotNavigation.findNearest(
				points,
				client.mouseHandler.getScaledXPos(client.getWindow()),
				client.mouseHandler.getScaledYPos(client.getWindow()));
		return slots.get(Math.max(0, index));
	}

	private static Slot nextSlot(
			AbstractContainerScreenAccessor accessor,
			List<Slot> slots,
			Slot current,
			StickDpadRepeater.Direction direction
	) {
		int currentIndex = slots.indexOf(current);
		int nextIndex = SlotNavigation.findNext(slotPoints(accessor, slots), currentIndex, direction);
		return slots.get(Math.max(0, nextIndex));
	}

	private static List<SlotNavigation.Point> slotPoints(
			AbstractContainerScreenAccessor accessor,
			List<Slot> slots
	) {
		int left = accessor.mcxrinput$leftPos();
		int top = accessor.mcxrinput$topPos();
		return slots.stream()
				.map(slot -> new SlotNavigation.Point(left + slot.x + 8, top + slot.y + 8))
				.toList();
	}

	private static void snapCursor(
			Minecraft client,
			AbstractContainerScreenAccessor accessor,
			Slot slot
	) {
		if (slot == null) {
			return;
		}
		double guiX = accessor.mcxrinput$leftPos() + slot.x + 8.0;
		double guiY = accessor.mcxrinput$topPos() + slot.y + 8.0;
		var window = client.getWindow();
		double rawX = guiX * window.getScreenWidth() / window.getGuiScaledWidth();
		double rawY = guiY * window.getScreenHeight() / window.getGuiScaledHeight();
		GLFW.glfwSetCursorPos(window.handle(), rawX, rawY);
		((MouseHandlerAccessor) client.mouseHandler).mcxrinput$onMove(window.handle(), rawX, rawY);
	}

	private static final class ButtonEdge {
		private final AnalogButtonLatch latch = new AnalogButtonLatch(BUTTON_RELEASE_MARGIN);
		private boolean wasDown;

		boolean update(
				ControllerButton binding,
				VrControllerState left,
				VrControllerState right,
				float threshold
		) {
			ControllerButton.Sample sample = binding.sample(left, right);
			boolean down = latch.update(sample.active(), sample.value(), threshold);
			boolean pressed = down && !wasDown;
			wasDown = down;
			return pressed;
		}

		void suppress() {
			latch.suppress();
			wasDown = false;
		}
	}
}
