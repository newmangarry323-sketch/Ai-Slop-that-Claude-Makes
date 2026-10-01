package io.github.newmangarry323sketch.rustbuilding.client.screen;

import net.minecraft.client.gui.components.EditBox;
import net.minecraft.core.BlockPos;
import net.minecraft.network.chat.Component;

import net.fabricmc.fabric.api.client.networking.v1.ClientPlayNetworking;

import io.github.newmangarry323sketch.rustbuilding.LockScreenMode;
import io.github.newmangarry323sketch.rustbuilding.lock.CodeLock;
import io.github.newmangarry323sketch.rustbuilding.network.Payloads;

/** Four digits: to set a new lock's code, to get past someone's lock, or (for the owner) to change it. */
public class CodeLockScreen extends MenuScreen {
	private final BlockPos pos;
	private final LockScreenMode mode;
	private EditBox codeBox;
	private String typed = "";

	public CodeLockScreen(BlockPos pos, LockScreenMode mode) {
		super(Component.translatable("item.rustbuilding.code_lock"));
		this.pos = pos;
		this.mode = mode;
	}

	@Override
	protected void build() {
		switch (this.mode) {
			case SET -> {
				this.lines().add(Component.translatable("screen.rustbuilding.lock.set"));
				this.button(Component.translatable("screen.rustbuilding.lock.confirm"), button -> this.submit(Payloads.CodeLockAction.INSTALL));
			}
			case ENTER -> {
				this.lines().add(Component.translatable("screen.rustbuilding.lock.enter"));
				this.button(Component.translatable("screen.rustbuilding.lock.unlock"), button -> this.submit(Payloads.CodeLockAction.ENTER));
			}
			case OWNER -> {
				this.lines().add(Component.translatable("screen.rustbuilding.lock.owner"));
				this.button(Component.translatable("screen.rustbuilding.lock.change"), button -> this.submit(Payloads.CodeLockAction.CHANGE));
				this.button(Component.translatable("screen.rustbuilding.lock.remove"), button -> {
					ClientPlayNetworking.send(new Payloads.CodeLockAction(this.pos, Payloads.CodeLockAction.REMOVE, ""));
					this.onClose();
				});
			}
		}

		this.button(Component.translatable("gui.cancel"), button -> this.onClose());
		// Leave room for the code box above the buttons.
		this.lines().add(Component.empty());
		this.lines().add(Component.empty());
	}

	@Override
	protected void afterLayout(int x, int y) {
		this.codeBox = new EditBox(this.font, x + (BUTTON_WIDTH - 60) / 2, y - 22, 60, 18, Component.translatable("screen.rustbuilding.lock.code"));
		this.codeBox.setMaxLength(4);
		this.codeBox.setFilter(text -> text.chars().allMatch(c -> c >= '0' && c <= '9'));
		this.codeBox.setValue(this.typed);
		this.codeBox.setResponder(text -> this.typed = text);
		this.addRenderableWidget(this.codeBox);
		this.setInitialFocus(this.codeBox);
	}

	private void submit(int action) {
		String code = this.codeBox == null ? "" : this.codeBox.getValue();

		if (!CodeLock.isValidCode(code)) {
			return;
		}

		ClientPlayNetworking.send(new Payloads.CodeLockAction(this.pos, action, code));
		this.onClose();
	}
}
