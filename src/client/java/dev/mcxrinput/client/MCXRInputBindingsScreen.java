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
		int y = 42;

		addRenderableOnly(new StringWidget(
				centerX - 125, 16, 250, 20,
				Component.translatable(page.titleKey), font));

		if (page == Page.GAMEPLAY) {
			addStickRow(left, y, "option.mcxrinput.movement_stick",
					() -> values.movementStick, value -> values.movementStick = value,
					MCXRInputConfig.DEFAULT_MOVEMENT_STICK);
			y += 26;
			addButtonRow(left, y, "option.mcxrinput.binding.jump",
					() -> values.jumpBinding, value -> values.jumpBinding = value,
					MCXRInputConfig.DEFAULT_JUMP_BINDING);
			y += 26;
			addButtonRow(left, y, "option.mcxrinput.binding.sneak",
					() -> values.sneakBinding, value -> values.sneakBinding = value,
					MCXRInputConfig.DEFAULT_SNEAK_BINDING);
			y += 26;
			addButtonRow(left, y, "option.mcxrinput.binding.sprint",
					() -> values.sprintBinding, value -> values.sprintBinding = value,
					MCXRInputConfig.DEFAULT_SPRINT_BINDING);
			y += 26;
			addButtonRow(left, y, "option.mcxrinput.binding.attack",
					() -> values.attackBinding, value -> values.attackBinding = value,
					MCXRInputConfig.DEFAULT_ATTACK_BINDING);
			y += 26;
			addButtonRow(left, y, "option.mcxrinput.binding.use",
					() -> values.useBinding, value -> values.useBinding = value,
					MCXRInputConfig.DEFAULT_USE_BINDING);
		} else {
			addStickRow(left, y, "option.mcxrinput.menu_navigation_stick",
					() -> values.menuNavigationStick, value -> values.menuNavigationStick = value,
					MCXRInputConfig.DEFAULT_MENU_NAVIGATION_STICK);
			y += 26;
			addButtonRow(left, y, "option.mcxrinput.binding.menu_confirm",
					() -> values.menuConfirmBinding, value -> values.menuConfirmBinding = value,
					MCXRInputConfig.DEFAULT_MENU_CONFIRM_BINDING);
			y += 26;
			addButtonRow(left, y, "option.mcxrinput.binding.menu_back",
					() -> values.menuBackBinding, value -> values.menuBackBinding = value,
					MCXRInputConfig.DEFAULT_MENU_BACK_BINDING);
		}

		addRenderableWidget(Button.builder(Component.translatable("controls.reset"), button -> {
			resetPage();
			refreshValues();
		}).bounds(centerX - 155, height - 32, 150, 20).build());
		addRenderableWidget(Button.builder(Component.translatable("gui.done"), button -> returnToParent())
				.bounds(centerX + 5, height - 32, 150, 20).build());
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
		} else {
			values.menuNavigationStick = MCXRInputConfig.DEFAULT_MENU_NAVIGATION_STICK.id();
			values.menuConfirmBinding = MCXRInputConfig.DEFAULT_MENU_CONFIRM_BINDING.id();
			values.menuBackBinding = MCXRInputConfig.DEFAULT_MENU_BACK_BINDING.id();
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
		GAMEPLAY("screen.mcxrinput.gameplay_bindings"),
		MENU("screen.mcxrinput.menu_bindings");

		final String titleKey;

		Page(String titleKey) {
			this.titleKey = titleKey;
		}
	}
}
