package io.github.newmangarry323sketch.rustbuilding.client.screen;

import java.util.ArrayList;
import java.util.List;

import net.minecraft.client.gui.GuiGraphicsExtractor;
import net.minecraft.client.gui.components.Button;
import net.minecraft.client.gui.screens.Screen;
import net.minecraft.network.chat.Component;

/**
 * Buttons under a title and a few lines of text, in one centred column or two: all the mod's menus look
 * like this.
 */
abstract class MenuScreen extends Screen {
	protected static final int BUTTON_WIDTH = 200;
	protected static final int BUTTON_HEIGHT = 20;
	private static final int COLUMN_WIDTH = 150;
	private static final int GAP = 4;
	private static final int LINE_HEIGHT = 11;

	private final List<Component> lines = new ArrayList<>();
	private final List<Button.Builder> pendingButtons = new ArrayList<>();
	private int top;

	protected MenuScreen(Component title) {
		super(title);
	}

	/** Text shown between the title and the buttons. */
	protected List<Component> lines() {
		return this.lines;
	}

	/** Fills {@link #lines()} and adds the buttons with {@link #button}. Called on every (re)initialisation. */
	protected abstract void build();

	/** Lay the buttons out in two columns, for menus with many of them. */
	protected boolean twoColumns() {
		return false;
	}

	@Override
	protected final void init() {
		this.lines.clear();
		this.clearWidgets();
		this.pendingButtons.clear();
		this.build();

		int columns = this.twoColumns() ? 2 : 1;
		int buttonWidth = columns == 1 ? BUTTON_WIDTH : COLUMN_WIDTH;
		int rows = (this.pendingButtons.size() + columns - 1) / columns;
		int textHeight = 14 + this.lines.size() * LINE_HEIGHT + 8;
		int buttonsHeight = rows * (BUTTON_HEIGHT + GAP);
		this.top = Math.max(10, (this.height - textHeight - buttonsHeight) / 2);
		int y = this.top + textHeight;
		int x = (this.width - (columns * buttonWidth + (columns - 1) * GAP)) / 2;

		for (int i = 0; i < this.pendingButtons.size(); i++) {
			int column = i % columns;
			int buttonX = x + column * (buttonWidth + GAP);

			// A last button on its own goes in the middle.
			if (columns > 1 && i == this.pendingButtons.size() - 1 && column == 0) {
				buttonX = (this.width - buttonWidth) / 2;
			}

			int buttonY = y + (i / columns) * (BUTTON_HEIGHT + GAP);
			this.addRenderableWidget(this.pendingButtons.get(i).bounds(buttonX, buttonY, buttonWidth, BUTTON_HEIGHT).build());
		}

		this.afterLayout(x, this.top + textHeight - 4);
	}

	/** For screens that add their own widgets (the code box); {@code y} is just above the first button. */
	protected void afterLayout(int x, int y) {
	}

	protected Button.Builder button(Component label, Button.OnPress action) {
		Button.Builder builder = Button.builder(label, action);
		this.pendingButtons.add(builder);
		return builder;
	}

	/** Rebuilds the screen, e.g. after the server updated what it shows. */
	protected void refresh() {
		this.init();
	}

	@Override
	public void extractRenderState(GuiGraphicsExtractor graphics, int mouseX, int mouseY, float delta) {
		super.extractRenderState(graphics, mouseX, mouseY, delta);
		String title = this.title.getString();
		graphics.text(this.font, title, (this.width - this.font.width(title)) / 2, this.top, 0xFFFFFFFF, true);
		int y = this.top + 14;

		// Lines keep their own colours (a warning in red, say); plain ones are light grey.
		for (Component line : this.lines) {
			graphics.text(this.font, line, (this.width - this.font.width(line)) / 2, y, 0xFFC8C8C8, false);
			y += LINE_HEIGHT;
		}
	}

	@Override
	public boolean isPauseScreen() {
		return false;
	}
}
