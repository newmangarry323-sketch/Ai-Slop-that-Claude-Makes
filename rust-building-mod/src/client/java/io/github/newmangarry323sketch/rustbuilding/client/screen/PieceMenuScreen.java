package io.github.newmangarry323sketch.rustbuilding.client.screen;

import net.minecraft.network.chat.Component;

import net.fabricmc.fabric.api.client.networking.v1.ClientPlayNetworking;

import io.github.newmangarry323sketch.rustbuilding.building.PieceType;
import io.github.newmangarry323sketch.rustbuilding.network.Payloads;

/** The building plan's piece picker (Rust uses a radial menu; this is a list). */
public class PieceMenuScreen extends MenuScreen {
	private final PieceType current;

	public PieceMenuScreen(PieceType current) {
		super(Component.translatable("screen.rustbuilding.pieces"));
		this.current = current;
	}

	@Override
	protected boolean twoColumns() {
		return true;
	}

	@Override
	protected void build() {
		this.lines().add(Component.translatable("screen.rustbuilding.pieces.hint"));

		for (PieceType type : PieceType.MENU_ORDER) {
			Component label = type == this.current
					? Component.literal("> ").append(type.displayName()).append(" <")
					: type.displayName();
			this.button(label, button -> {
				ClientPlayNetworking.send(new Payloads.SelectPiece(type.ordinal()));
				this.onClose();
			});
		}
	}
}
