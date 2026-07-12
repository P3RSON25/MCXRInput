package dev.mcxrinput.client;

import dev.mcxrinput.input.AnalogButtonPress;
import dev.mcxrinput.input.ControllerButton;
import dev.mcxrinput.input.ControllerStick;
import dev.mcxrinput.input.SlotNavigation;
import dev.mcxrinput.input.StickDpadGesture;
import dev.mcxrinput.input.StickDpadRepeater;
import dev.mcxrinput.mixin.client.AbstractContainerScreenAccessor;
import dev.mcxrinput.mixin.client.CreativeModeInventoryScreenAccessor;
import dev.mcxrinput.mixin.client.MouseHandlerAccessor;
import dev.mcxrinput.protocol.VrControllerState;
import dev.mcxrinput.protocol.VrInputFrame;
import net.minecraft.client.InputType;
import net.minecraft.client.Minecraft;
import net.minecraft.client.gui.components.Button;
import net.minecraft.client.gui.components.events.GuiEventListener;
import net.minecraft.client.gui.screens.Screen;
import net.minecraft.client.gui.screens.inventory.AbstractContainerScreen;
import net.minecraft.client.gui.screens.inventory.CreativeModeInventoryScreen;
import net.minecraft.client.gui.screens.inventory.MerchantScreen;
import net.minecraft.client.input.MouseButtonEvent;
import net.minecraft.client.input.MouseButtonInfo;
import net.minecraft.network.chat.Component;
import net.minecraft.world.inventory.ContainerInput;
import net.minecraft.world.inventory.Slot;
import net.minecraft.world.item.CreativeModeTab;
import org.lwjgl.glfw.GLFW;

import java.time.Duration;
import java.util.ArrayList;
import java.util.List;

/**
 * Provides snapped, D-pad-style navigation for container slots and Creative
 * tabs. Every action is a physical press routed through Minecraft's existing
 * screen methods; scrolling is delivered as an ordinary wheel event.
 */
final class VrInventoryInputController {
	private static final Duration MAXIMUM_FRAME_AGE = Duration.ofMillis(250);
	private static final float BUTTON_RELEASE_MARGIN = 0.10F;

	private final VrUdpReceiver receiver;
	private final MCXRInputConfig config;
	private final StickDpadGesture navigation = new StickDpadGesture();
	private final StickDpadGesture scrolling = new StickDpadGesture();
	private final AnalogButtonPress selectButton = new AnalogButtonPress(BUTTON_RELEASE_MARGIN);
	private final AnalogButtonPress quickMoveButton = new AnalogButtonPress(BUTTON_RELEASE_MARGIN);
	private final AnalogButtonPress takeHalfButton = new AnalogButtonPress(BUTTON_RELEASE_MARGIN);
	private final AnalogButtonPress dropButton = new AnalogButtonPress(BUTTON_RELEASE_MARGIN);
	private final AnalogButtonPress backButton = new AnalogButtonPress(BUTTON_RELEASE_MARGIN);
	private final AnalogButtonPress nextTabButton = new AnalogButtonPress(BUTTON_RELEASE_MARGIN);
	private final AnalogButtonPress previousTabButton = new AnalogButtonPress(BUTTON_RELEASE_MARGIN);
	private Screen lastScreen;
	private Screen warnedMultiplayerScreen;
	private Target selectedTarget;

	VrInventoryInputController(VrUdpReceiver receiver, MCXRInputConfig config) {
		this.receiver = receiver;
		this.config = config;
	}

	void tick(Minecraft client, boolean inputEnabled) {
		Screen currentScreen = client.gui.screen();
		if (inputEnabled && currentScreen instanceof AbstractContainerScreen<?>
				&& client.isMultiplayerServer()
				&& !config.allowInventoryInputInMultiplayer()) {
			if (currentScreen != warnedMultiplayerScreen && client.player != null) {
				client.player.sendOverlayMessage(Component.literal(
						"MCXRInput: inventory controller input is disabled on multiplayer by default"));
				warnedMultiplayerScreen = currentScreen;
			}
			releaseAll();
			return;
		}
		warnedMultiplayerScreen = null;
		if (!inputEnabled || client.gui.overlay() != null
				|| !(currentScreen instanceof AbstractContainerScreen<?> screen)) {
			releaseAll();
			return;
		}

		if (screen != lastScreen) {
			suppressInputs();
			selectedTarget = null;
			lastScreen = screen;
		}

		VrInputFrame frame = receiver.latestFreshFrame(
				MAXIMUM_FRAME_AGE, client.isMultiplayerServer());
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

		float threshold = (float) config.triggerThreshold();
		boolean backPressed = bindingPressed(
				config.menuBackBinding(), left, right, backButton, threshold);
		if (backPressed) {
			screen.onClose();
			return;
		}

		AbstractContainerScreenAccessor accessor = (AbstractContainerScreenAccessor) screen;
		List<Target> targets = collectTargets(screen, accessor);
		if (targets.isEmpty()) {
			selectedTarget = null;
			suppressTargetInputs();
			return;
		}

		if (selectedTarget == null || !targets.contains(selectedTarget)) {
			selectedTarget = nearestTarget(client, targets);
			snapCursor(client, selectedTarget);
		} else {
			Target hoveredTarget = hoveredTarget(targets, accessor.mcxrinput$hoveredSlot());
			if (hoveredTarget != null) {
				// Keep physical mouse use interoperable with the snapped controller cursor.
				selectedTarget = hoveredTarget;
			}
		}

		ControllerStick navigationStick = config.menuNavigationStick();
		VrControllerState stick = navigationStick.select(left, right);
		StickDpadRepeater.Direction direction = navigation.update(
				stick.active(), stick.stickX(), stick.stickY(), (float) config.controllerDeadzone());
		if (direction != null) {
			selectedTarget = nextTarget(targets, selectedTarget, direction);
			snapCursor(client, selectedTarget);
		}

		ControllerStick scrollStickSetting = config.inventoryScrollStick();
		VrControllerState scrollStick = scrollStickSetting.select(left, right);
		StickDpadRepeater.Direction scrollDirection = scrolling.update(
				scrollStick.active(), scrollStick.stickX(), scrollStick.stickY(),
				(float) config.controllerDeadzone());

		boolean selectPressed = bindingPressed(
				config.inventorySelectBinding(), left, right, selectButton, threshold);
		boolean quickMovePressed = bindingPressed(
				config.inventoryQuickMoveBinding(), left, right, quickMoveButton, threshold);
		boolean takeHalfPressed = bindingPressed(
				config.inventoryTakeHalfBinding(), left, right, takeHalfButton, threshold);
		boolean dropPressed = bindingPressed(
				config.inventoryDropBinding(), left, right, dropButton, threshold);
		boolean nextTabPressed = bindingPressed(
				config.creativeNextTabBinding(), left, right, nextTabButton, threshold);
		boolean previousTabPressed = bindingPressed(
				config.creativePreviousTabBinding(), left, right, previousTabButton, threshold);

		// The action values match vanilla mouse handling: button 0 pickup/place,
		// button 1 take/place half, QUICK_MOVE for shift-click, and slot -999 for
		// dropping the currently carried stack outside the container.
		if (nextTabPressed && screen instanceof CreativeModeInventoryScreen creative) {
			cycleCreativeTab(creative, 1);
			selectedTarget = null;
		} else if (previousTabPressed && screen instanceof CreativeModeInventoryScreen creative) {
			cycleCreativeTab(creative, -1);
			selectedTarget = null;
		} else if (selectPressed && selectedTarget.tab() != null
				&& screen instanceof CreativeModeInventoryScreen creative) {
			creative.setSelectedTab(selectedTarget.tab());
		} else if (selectPressed && selectedTarget.button() != null) {
			clickButton(client, screen, selectedTarget);
		} else if (selectPressed && selectedTarget.slot() != null) {
			Slot slot = selectedTarget.slot();
			accessor.mcxrinput$slotClicked(slot, slot.index, 0, ContainerInput.PICKUP);
		} else if (dropPressed && !screen.getMenu().getCarried().isEmpty()) {
			accessor.mcxrinput$slotClicked(null, -999, 0, ContainerInput.PICKUP);
		} else if (quickMovePressed && screen.getMenu().getCarried().isEmpty()
				&& selectedTarget.slot() != null) {
			Slot slot = selectedTarget.slot();
			accessor.mcxrinput$slotClicked(slot, slot.index, 0, ContainerInput.QUICK_MOVE);
		} else if (takeHalfPressed && selectedTarget.slot() != null) {
			Slot slot = selectedTarget.slot();
			accessor.mcxrinput$slotClicked(slot, slot.index, 1, ContainerInput.PICKUP);
		} else if (scrollDirection == StickDpadRepeater.Direction.UP) {
			dispatchScroll(client, 1.0);
		} else if (scrollDirection == StickDpadRepeater.Direction.DOWN) {
			dispatchScroll(client, -1.0);
		}
	}

	void releaseAll() {
		suppressInputs();
		lastScreen = null;
		selectedTarget = null;
	}

	private void suppressInputs() {
		suppressTargetInputs();
		backButton.suppress();
	}

	private void suppressTargetInputs() {
		navigation.suppress();
		scrolling.suppress();
		selectButton.suppress();
		quickMoveButton.suppress();
		takeHalfButton.suppress();
		dropButton.suppress();
		nextTabButton.suppress();
		previousTabButton.suppress();
	}

	private static List<Target> collectTargets(
			AbstractContainerScreen<?> screen,
			AbstractContainerScreenAccessor accessor
	) {
		int left = accessor.mcxrinput$leftPos();
		int top = accessor.mcxrinput$topPos();
		List<Target> targets = new ArrayList<>();
		for (Slot slot : screen.getMenu().slots) {
			if (slot.isActive() && slot.isHighlightable()) {
				targets.add(Target.slot(slot, left + slot.x + 8, top + slot.y + 8));
			}
		}

		if (screen instanceof CreativeModeInventoryScreen creative) {
			CreativeModeInventoryScreenAccessor creativeAccessor =
					(CreativeModeInventoryScreenAccessor) creative;
			for (CreativeModeTab tab : creative.getTabsOnPage(creative.getCurrentPage())) {
				if (tab.shouldDisplay()) {
					int x = left + creativeAccessor.mcxrinput$getTabX(tab) + 13;
					int y = top + creativeAccessor.mcxrinput$getTabY(tab) + 16;
					targets.add(Target.tab(tab, x, y));
				}
			}
		}

		if (screen instanceof MerchantScreen) {
			for (GuiEventListener child : screen.children()) {
				if (child instanceof Button button && button.visible && button.active) {
					targets.add(Target.button(
							button,
							button.getX() + button.getWidth() / 2,
							button.getY() + button.getHeight() / 2));
				}
			}
		}
		return List.copyOf(targets);
	}

	private static Target nearestTarget(Minecraft client, List<Target> targets) {
		int index = SlotNavigation.findNearest(
				targets.stream().map(Target::point).toList(),
				client.mouseHandler.getScaledXPos(client.getWindow()),
				client.mouseHandler.getScaledYPos(client.getWindow()));
		return targets.get(Math.max(0, index));
	}

	private static Target nextTarget(
			List<Target> targets,
			Target current,
			StickDpadRepeater.Direction direction
	) {
		int currentIndex = targets.indexOf(current);
		int nextIndex = SlotNavigation.findNext(
				targets.stream().map(Target::point).toList(), currentIndex, direction);
		return targets.get(Math.max(0, nextIndex));
	}

	private static Target hoveredTarget(List<Target> targets, Slot hoveredSlot) {
		for (Target target : targets) {
			if ((hoveredSlot != null && target.slot() == hoveredSlot)
					|| (target.button() != null && target.button().isHovered())) {
				return target;
			}
		}
		return null;
	}

	private static void clickButton(Minecraft client, Screen screen, Target target) {
		// Use the widget's ordinary mouse path. MerchantScreen owns selection and
		// networking, so one physical controller edge becomes exactly one vanilla click.
		double x = target.point().x();
		double y = target.point().y();
		MouseButtonEvent event = new MouseButtonEvent(
				x, y, new MouseButtonInfo(GLFW.GLFW_MOUSE_BUTTON_LEFT, 0));
		client.setLastInputType(InputType.MOUSE);
		screen.afterMouseAction();
		screen.mouseClicked(event, false);
	}

	private static void snapCursor(Minecraft client, Target target) {
		if (target == null) {
			return;
		}
		var window = client.getWindow();
		double rawX = target.point().x() * window.getScreenWidth() / window.getGuiScaledWidth();
		double rawY = target.point().y() * window.getScreenHeight() / window.getGuiScaledHeight();
		GLFW.glfwSetCursorPos(window.handle(), rawX, rawY);
		((MouseHandlerAccessor) client.mouseHandler).mcxrinput$onMove(window.handle(), rawX, rawY);
	}

	private static void dispatchScroll(Minecraft client, double amount) {
		var window = client.getWindow();
		((MouseHandlerAccessor) client.mouseHandler).mcxrinput$onScroll(
				window.handle(), 0.0, amount);
	}

	private static void cycleCreativeTab(CreativeModeInventoryScreen screen, int step) {
		int pageCount = Math.max(1, screen.getPageCount());
		int page = Math.max(0, Math.min(screen.getCurrentPage(), pageCount - 1));
		List<CreativeModeTab> tabs = visibleTabs(screen, page);
		CreativeModeTab selected = screen.getSelectedTab();
		int selectedIndex = tabs.indexOf(selected);

		if (selectedIndex >= 0) {
			int candidate = selectedIndex + step;
			if (candidate >= 0 && candidate < tabs.size()) {
				screen.setSelectedTab(tabs.get(candidate));
				return;
			}
		}

		for (int attempts = 0; attempts < pageCount; attempts++) {
			page = Math.floorMod(page + step, pageCount);
			tabs = visibleTabs(screen, page);
			if (!tabs.isEmpty()) {
				screen.switchToPage(page);
				screen.setSelectedTab(step > 0 ? tabs.getFirst() : tabs.getLast());
				return;
			}
		}
	}

	private static List<CreativeModeTab> visibleTabs(
			CreativeModeInventoryScreen screen,
			int page
	) {
		return screen.getTabsOnPage(page).stream()
				.filter(CreativeModeTab::shouldDisplay)
				.toList();
	}

	private record Target(
			Slot slot,
			CreativeModeTab tab,
			Button button,
			SlotNavigation.Point point
	) {
		static Target slot(Slot slot, int x, int y) {
			return new Target(slot, null, null, new SlotNavigation.Point(x, y));
		}

		static Target tab(CreativeModeTab tab, int x, int y) {
			return new Target(null, tab, null, new SlotNavigation.Point(x, y));
		}

		static Target button(Button button, int x, int y) {
			return new Target(null, null, button, new SlotNavigation.Point(x, y));
		}
	}

	private static boolean bindingPressed(
			ControllerButton binding,
			VrControllerState left,
			VrControllerState right,
			AnalogButtonPress press,
			float threshold
	) {
		ControllerButton.Sample sample = binding.sample(left, right);
		return press.update(sample.active(), sample.value(), threshold);
	}
}
