package dev.mcxrinput.client;

import dev.mcxrinput.input.ControllerButton;
import dev.mcxrinput.input.ControllerStick;
import net.minecraft.client.gui.components.Button;
import net.minecraft.client.gui.components.StringWidget;
import net.minecraft.client.gui.screens.Screen;
import net.minecraft.network.chat.Component;

import java.util.ArrayList;
import java.util.List;
import java.util.function.Consumer;
import java.util.function.Supplier;

final class MCXRInputBindingsScreen extends Screen {
	private final Screen parent;
	private final MCXRInputConfig.Values values;
	private final Page page;
	private final List<Runnable> refreshers = new ArrayList<>();

	MCXRInputBindingsScreen(Screen parent, MCXRInputConfig.Values values, Page page) {
		super(Component.translatable(page.titleKey));
		this.parent = parent;
		this.values = values;
		this.page = page;
	}

	@Override
	protected void init() {
		refreshers.clear();
		int centerX = width / 2;
		int left = centerX - 150;
		int y = 38;

		addRenderableOnly(new StringWidget(
				centerX - 125, 16, 250, 20,
				Component.translatable(page.titleKey), font));

		if (page == Page.OVERVIEW) {
			addCategoryButton(left, 60, Page.GAMEPLAY);
			addCategoryButton(left, 86, Page.MENU);
			addCategoryButton(left, 112, Page.INVENTORY);
		} else if (page == Page.GAMEPLAY) {
			addStickRow(left, y, "option.mcxrinput.movement_stick",
					() -> values.movementStick, value -> values.movementStick = value,
					MCXRInputConfig.DEFAULT_MOVEMENT_STICK);
			y += 24;
			addButtonRow(left, y, "option.mcxrinput.binding.jump",
					() -> values.jumpBinding, value -> values.jumpBinding = value,
					MCXRInputConfig.DEFAULT_JUMP_BINDING);
			y += 24;
			addButtonRow(left, y, "option.mcxrinput.binding.sneak",
					() -> values.sneakBinding, value -> values.sneakBinding = value,
					MCXRInputConfig.DEFAULT_SNEAK_BINDING);
			y += 24;
			addButtonRow(left, y, "option.mcxrinput.binding.sprint",
					() -> values.sprintBinding, value -> values.sprintBinding = value,
					MCXRInputConfig.DEFAULT_SPRINT_BINDING);
			y += 24;
			addButtonRow(left, y, "option.mcxrinput.binding.attack",
					() -> values.attackBinding, value -> values.attackBinding = value,
					MCXRInputConfig.DEFAULT_ATTACK_BINDING);
			y += 24;
			addButtonRow(left, y, "option.mcxrinput.binding.use",
					() -> values.useBinding, value -> values.useBinding = value,
					MCXRInputConfig.DEFAULT_USE_BINDING);
			y += 24;
			addButtonRow(left, y, "option.mcxrinput.binding.inventory",
					() -> values.inventoryBinding, value -> values.inventoryBinding = value,
					MCXRInputConfig.DEFAULT_INVENTORY_BINDING);
		} else if (page == Page.MENU) {
			addStickRow(left, y, "option.mcxrinput.menu_navigation_stick",
					() -> values.menuNavigationStick, value -> values.menuNavigationStick = value,
					MCXRInputConfig.DEFAULT_MENU_NAVIGATION_STICK);
			y += 24;
			addButtonRow(left, y, "option.mcxrinput.binding.menu_confirm",
					() -> values.menuConfirmBinding, value -> values.menuConfirmBinding = value,
					MCXRInputConfig.DEFAULT_MENU_CONFIRM_BINDING);
			y += 24;
			addButtonRow(left, y, "option.mcxrinput.binding.menu_back",
					() -> values.menuBackBinding, value -> values.menuBackBinding = value,
					MCXRInputConfig.DEFAULT_MENU_BACK_BINDING);
		} else {
			addButtonRow(left, y, "option.mcxrinput.binding.inventory_select",
					() -> values.inventorySelectBinding, value -> values.inventorySelectBinding = value,
					MCXRInputConfig.DEFAULT_INVENTORY_SELECT_BINDING);
			y += 24;
			addButtonRow(left, y, "option.mcxrinput.binding.inventory_quick_move",
					() -> values.inventoryQuickMoveBinding, value -> values.inventoryQuickMoveBinding = value,
					MCXRInputConfig.DEFAULT_INVENTORY_QUICK_MOVE_BINDING);
			y += 24;
			addButtonRow(left, y, "option.mcxrinput.binding.inventory_take_half",
					() -> values.inventoryTakeHalfBinding, value -> values.inventoryTakeHalfBinding = value,
					MCXRInputConfig.DEFAULT_INVENTORY_TAKE_HALF_BINDING);
			y += 24;
			addButtonRow(left, y, "option.mcxrinput.binding.inventory_drop",
					() -> values.inventoryDropBinding, value -> values.inventoryDropBinding = value,
					MCXRInputConfig.DEFAULT_INVENTORY_DROP_BINDING);
		}

		if (page == Page.OVERVIEW) {
			addRenderableWidget(Button.builder(Component.translatable("gui.done"), button -> returnToParent())
					.bounds(centerX - 75, height - 32, 150, 20).build());
		} else {
			addRenderableWidget(Button.builder(Component.translatable("controls.reset"), button -> {
				resetPage();
				refreshValues();
			}).bounds(centerX - 155, height - 32, 150, 20).build());
			addRenderableWidget(Button.builder(Component.translatable("gui.done"), button -> returnToParent())
					.bounds(centerX + 5, height - 32, 150, 20).build());
		}
		refreshValues();
	}

	@Override
	public void onClose() {
		returnToParent();
	}

	private void addButtonRow(
			int left,
			int y,
			String labelKey,
			Supplier<String> getter,
			Consumer<String> setter,
			ControllerButton fallback
	) {
		addRenderableOnly(new StringWidget(left, y, 126, 20, Component.translatable(labelKey), font));
		Button valueButton = addRenderableWidget(Button.builder(Component.empty(), button -> {
			ControllerButton current = ControllerButton.fromId(getter.get(), fallback);
			setter.accept(current.next().id());
			refreshValues();
		}).bounds(left + 132, y, 168, 20).build());
		refreshers.add(() -> {
			ControllerButton current = ControllerButton.fromId(getter.get(), fallback);
			valueButton.setMessage(Component.translatable(current.translationKey()));
		});
	}

	private void addCategoryButton(int left, int y, Page destination) {
		addRenderableWidget(Button.builder(Component.translatable(destination.titleKey), button -> {
			if (minecraft != null) {
				minecraft.gui.setScreen(new MCXRInputBindingsScreen(this, values, destination));
			}
		}).bounds(left, y, 300, 20).build());
	}

	private void addStickRow(
			int left,
			int y,
			String labelKey,
			Supplier<String> getter,
			Consumer<String> setter,
			ControllerStick fallback
	) {
		addRenderableOnly(new StringWidget(left, y, 126, 20, Component.translatable(labelKey), font));
		Button valueButton = addRenderableWidget(Button.builder(Component.empty(), button -> {
			ControllerStick current = ControllerStick.fromId(getter.get(), fallback);
			setter.accept(current.next().id());
			refreshValues();
		}).bounds(left + 132, y, 168, 20).build());
		refreshers.add(() -> {
			ControllerStick current = ControllerStick.fromId(getter.get(), fallback);
			valueButton.setMessage(Component.translatable(current.translationKey()));
		});
	}

	private void resetPage() {
		if (page == Page.GAMEPLAY) {
			values.movementStick = MCXRInputConfig.DEFAULT_MOVEMENT_STICK.id();
			values.jumpBinding = MCXRInputConfig.DEFAULT_JUMP_BINDING.id();
			values.sneakBinding = MCXRInputConfig.DEFAULT_SNEAK_BINDING.id();
			values.sprintBinding = MCXRInputConfig.DEFAULT_SPRINT_BINDING.id();
			values.attackBinding = MCXRInputConfig.DEFAULT_ATTACK_BINDING.id();
			values.useBinding = MCXRInputConfig.DEFAULT_USE_BINDING.id();
			values.inventoryBinding = MCXRInputConfig.DEFAULT_INVENTORY_BINDING.id();
		} else if (page == Page.MENU) {
			values.menuNavigationStick = MCXRInputConfig.DEFAULT_MENU_NAVIGATION_STICK.id();
			values.menuConfirmBinding = MCXRInputConfig.DEFAULT_MENU_CONFIRM_BINDING.id();
			values.menuBackBinding = MCXRInputConfig.DEFAULT_MENU_BACK_BINDING.id();
		} else if (page == Page.INVENTORY) {
			values.inventorySelectBinding = MCXRInputConfig.DEFAULT_INVENTORY_SELECT_BINDING.id();
			values.inventoryQuickMoveBinding = MCXRInputConfig.DEFAULT_INVENTORY_QUICK_MOVE_BINDING.id();
			values.inventoryTakeHalfBinding = MCXRInputConfig.DEFAULT_INVENTORY_TAKE_HALF_BINDING.id();
			values.inventoryDropBinding = MCXRInputConfig.DEFAULT_INVENTORY_DROP_BINDING.id();
		}
	}

	private void refreshValues() {
		for (Runnable refresher : refreshers) {
			refresher.run();
		}
	}

	private void returnToParent() {
		if (minecraft != null) {
			minecraft.gui.setScreen(parent);
		}
	}

	enum Page {
		OVERVIEW("screen.mcxrinput.controller_bindings"),
		GAMEPLAY("screen.mcxrinput.gameplay_bindings"),
		MENU("screen.mcxrinput.menu_bindings"),
		INVENTORY("screen.mcxrinput.inventory_bindings");

		final String titleKey;

		Page(String titleKey) {
			this.titleKey = titleKey;
		}
	}
}
