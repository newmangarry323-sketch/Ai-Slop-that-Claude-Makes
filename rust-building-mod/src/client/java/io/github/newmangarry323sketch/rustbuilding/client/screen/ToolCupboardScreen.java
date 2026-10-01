package io.github.newmangarry323sketch.rustbuilding.client.screen;

import java.util.List;

import net.minecraft.client.Minecraft;
import net.minecraft.core.BlockPos;
import net.minecraft.network.chat.Component;

import net.fabricmc.fabric.api.client.networking.v1.ClientPlayNetworking;

import io.github.newmangarry323sketch.rustbuilding.LockScreenMode;
import io.github.newmangarry323sketch.rustbuilding.block.entity.ToolCupboardBlockEntity;
import io.github.newmangarry323sketch.rustbuilding.lock.Member;
import io.github.newmangarry323sketch.rustbuilding.network.Payloads;
import io.github.newmangarry323sketch.rustbuilding.privilege.BuildingPrivilege;

/** The cupboard's privilege list, with Rust's three actions: authorise, deauthorise, clear the list. */
public class ToolCupboardScreen extends MenuScreen {
	private static final int SHOWN_NAMES = 8;
	private final BlockPos pos;
	private List<Member> shown = List.of();

	public ToolCupboardScreen(BlockPos pos) {
		super(Component.translatable("block.rustbuilding.tool_cupboard"));
		this.pos = pos;
	}

	private ToolCupboardBlockEntity cupboard() {
		Minecraft minecraft = Minecraft.getInstance();
		return minecraft.level != null && minecraft.level.getBlockEntity(this.pos) instanceof ToolCupboardBlockEntity cupboard ? cupboard : null;
	}

	@Override
	protected void build() {
		ToolCupboardBlockEntity cupboard = this.cupboard();
		Minecraft minecraft = Minecraft.getInstance();

		if (cupboard == null || minecraft.player == null) {
			this.button(Component.translatable("gui.done"), button -> this.onClose());
			return;
		}

		this.shown = List.copyOf(cupboard.authorized());
		boolean authorized = cupboard.isAuthorized(minecraft.player.getUUID());
		this.lines().add(Component.translatable("screen.rustbuilding.cupboard.range", BuildingPrivilege.RANGE));
		this.lines().add(Component.translatable(authorized ? "screen.rustbuilding.cupboard.you_are" : "screen.rustbuilding.cupboard.you_are_not"));
		this.lines().add(Component.translatable("screen.rustbuilding.cupboard.list", this.shown.size()));

		for (int i = 0; i < Math.min(SHOWN_NAMES, this.shown.size()); i++) {
			this.lines().add(Component.literal(this.shown.get(i).name()));
		}

		if (this.shown.size() > SHOWN_NAMES) {
			this.lines().add(Component.translatable("screen.rustbuilding.cupboard.more", this.shown.size() - SHOWN_NAMES));
		}

		if (authorized) {
			this.button(Component.translatable("screen.rustbuilding.cupboard.deauthorize"), button -> this.send(Payloads.CupboardAction.DEAUTHORIZE));
		} else {
			this.button(Component.translatable("screen.rustbuilding.cupboard.authorize"), button -> this.send(Payloads.CupboardAction.AUTHORIZE));
		}

		this.button(Component.translatable("screen.rustbuilding.cupboard.clear"), button -> this.send(Payloads.CupboardAction.CLEAR));

		if (cupboard.lock().isLocked() && cupboard.lock().isOwner(minecraft.player)) {
			this.button(Component.translatable("screen.rustbuilding.lock.settings"),
					button -> minecraft.gui.setScreen(new CodeLockScreen(this.pos, LockScreenMode.OWNER)));
		}

		this.button(Component.translatable("gui.done"), button -> this.onClose());
	}

	private void send(int action) {
		ClientPlayNetworking.send(new Payloads.CupboardAction(this.pos, action));
	}

	@Override
	public void tick() {
		super.tick();
		ToolCupboardBlockEntity cupboard = this.cupboard();

		if (cupboard == null) {
			this.onClose();
			return;
		}

		// The server's answer arrives as a block entity update; redraw when the list changes.
		if (!cupboard.authorized().equals(this.shown)) {
			this.refresh();
		}
	}
}
