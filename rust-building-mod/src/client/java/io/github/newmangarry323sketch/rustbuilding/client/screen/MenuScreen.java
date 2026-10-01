package io.github.newmangarry323sketch.rustbuilding.client.screen;

import java.util.ArrayList;
import java.util.List;

import net.minecraft.client.gui.GuiGraphicsExtractor;
import net.minecraft.client.gui.components.Button;
import net.minecraft.client.gui.screens.Screen;
import net.minecraft.network.chat.Component;

/** A centred column of buttons under a title and a few lines of text: all the mod's menus look like this. */
abstract class MenuScreen extends Screen {
	protected static final int BUTTON_WIDTH = 200;
	protected static final int BUTTON_HEIGHT = 20;
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

	@Override
	protected final void init() {
		this.lines.clear();
		this.clearWidgets();
		this.pendingButtons.clear();
		this.build();

		int textHeight = 14 + this.lines.size() * LINE_HEIGHT + 8;
		int buttonsHeight = this.pendingButtons.size() * (BUTTON_HEIGHT + GAP);
		this.top = Math.max(10, (this.height - textHeight - buttonsHeight) / 2);
		int y = this.top + textHeight;
		int x = (this.width - BUTTON_WIDTH) / 2;

		for (Button.Builder builder : this.pendingButtons) {
			this.addRenderableWidget(builder.bounds(x, y, BUTTON_WIDTH, BUTTON_HEIGHT).build());
			y += BUTTON_HEIGHT + GAP;
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

		for (Component line : this.lines) {
			String text = line.getString();
			graphics.text(this.font, text, (this.width - this.font.width(text)) / 2, y, 0xFFC8C8C8, false);
			y += LINE_HEIGHT;
		}
	}

	@Override
	public boolean isPauseScreen() {
		return false;
	}
}
