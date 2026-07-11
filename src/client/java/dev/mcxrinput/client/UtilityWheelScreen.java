package dev.mcxrinput.client;

import dev.mcxrinput.input.UtilityWheelSelection;
import net.minecraft.client.gui.GuiGraphicsExtractor;
import net.minecraft.client.gui.screens.Screen;
import net.minecraft.network.chat.Component;

import java.util.Objects;

/**
 * Non-pausing, client-only overlay for the four fixed utility actions.
 * Controller state and action dispatch remain in the utility-wheel controller;
 * this screen is deliberately only a visual representation of its selection.
 */
final class UtilityWheelScreen extends Screen {
	private static final int CARD_WIDTH = 96;
	private static final int CARD_HEIGHT = 28;
	private static final int HUB_WIDTH = 96;
	private static final int HUB_HEIGHT = 40;
	private static final int HORIZONTAL_OFFSET = 104;
	private static final int VERTICAL_OFFSET = 58;

	private static final int SCREEN_SHADE = 0x58000000;
	private static final int CARD_BACKGROUND = 0xD0202228;
	private static final int SELECTED_BACKGROUND = 0xE0446FA3;
	private static final int HUB_BACKGROUND = 0xD0101115;
	private static final int CARD_BORDER = 0xA0AEB8C4;
	private static final int SELECTED_BORDER = 0xFFFFFFFF;
	private static final int TEXT_COLOR = 0xFFFFFFFF;
	private static final int HINT_COLOR = 0xFFB8C0CC;

	private UtilityWheelSelection.Action selectedAction;

	UtilityWheelScreen() {
		super(Component.translatable("screen.mcxrinput.utility_wheel.title"));
	}

	void setSelectedAction(UtilityWheelSelection.Action selectedAction) {
		this.selectedAction = Objects.requireNonNull(selectedAction, "selectedAction");
	}

	void clearSelectedAction() {
		selectedAction = null;
	}

	@Override
	public void extractRenderState(GuiGraphicsExtractor graphics, int mouseX, int mouseY, float partialTick) {
		super.extractRenderState(graphics, mouseX, mouseY, partialTick);

		int centerX = width / 2;
		int centerY = height / 2;
		drawAction(graphics, UtilityWheelSelection.Action.PAUSE,
				centerX - CARD_WIDTH / 2, centerY - VERTICAL_OFFSET - CARD_HEIGHT / 2);
		drawAction(graphics, UtilityWheelSelection.Action.CHAT,
				centerX - HORIZONTAL_OFFSET - CARD_WIDTH / 2, centerY - CARD_HEIGHT / 2);
		drawAction(graphics, UtilityWheelSelection.Action.PLAYER_LIST,
				centerX - CARD_WIDTH / 2, centerY + VERTICAL_OFFSET - CARD_HEIGHT / 2);
		drawAction(graphics, UtilityWheelSelection.Action.PERSPECTIVE,
				centerX + HORIZONTAL_OFFSET - CARD_WIDTH / 2, centerY - CARD_HEIGHT / 2);
		drawHub(graphics, centerX, centerY);
	}

	@Override
	public void extractTransparentBackground(GuiGraphicsExtractor graphics) {
		graphics.fill(0, 0, width, height, SCREEN_SHADE);
	}

	@Override
	public boolean isPauseScreen() {
		return false;
	}

	@Override
	public boolean isInGameUi() {
		return true;
	}

	private void drawAction(
			GuiGraphicsExtractor graphics,
			UtilityWheelSelection.Action action,
			int x,
			int y
	) {
		boolean selected = selectedAction == action;
		graphics.fill(x, y, x + CARD_WIDTH, y + CARD_HEIGHT,
				selected ? SELECTED_BACKGROUND : CARD_BACKGROUND);
		graphics.outline(x, y, CARD_WIDTH, CARD_HEIGHT,
				selected ? SELECTED_BORDER : CARD_BORDER);
		graphics.centeredText(font, Component.translatable(actionTranslationKey(action)),
				x + CARD_WIDTH / 2, y + (CARD_HEIGHT - font.lineHeight) / 2, TEXT_COLOR);
	}

	private void drawHub(GuiGraphicsExtractor graphics, int centerX, int centerY) {
		int x = centerX - HUB_WIDTH / 2;
		int y = centerY - HUB_HEIGHT / 2;
		graphics.fill(x, y, x + HUB_WIDTH, y + HUB_HEIGHT, HUB_BACKGROUND);
		graphics.outline(x, y, HUB_WIDTH, HUB_HEIGHT, CARD_BORDER);
		graphics.centeredText(font, title, centerX, y + 7, TEXT_COLOR);
		graphics.centeredText(font, Component.translatable("screen.mcxrinput.utility_wheel.hint"),
				centerX, y + 22, HINT_COLOR);
	}

	private static String actionTranslationKey(UtilityWheelSelection.Action action) {
		return switch (action) {
			case PAUSE -> "screen.mcxrinput.utility_wheel.pause";
			case CHAT -> "screen.mcxrinput.utility_wheel.chat";
			case PLAYER_LIST -> "screen.mcxrinput.utility_wheel.player_list";
			case PERSPECTIVE -> "screen.mcxrinput.utility_wheel.perspective";
		};
	}
}
