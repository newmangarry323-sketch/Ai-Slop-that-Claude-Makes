package io.github.newmangarry323sketch.rustbuilding.network;

import net.minecraft.core.BlockPos;
import net.minecraft.network.RegistryFriendlyByteBuf;
import net.minecraft.network.codec.ByteBufCodecs;
import net.minecraft.network.codec.StreamCodec;
import net.minecraft.network.protocol.common.custom.CustomPacketPayload;

import io.github.newmangarry323sketch.rustbuilding.RustBuilding;

/** Client-to-server requests from the mod's screens. The server validates every one. */
public final class Payloads {
	private Payloads() {
	}

	/** Pick the piece the held building plan places. */
	public record SelectPiece(int piece) implements CustomPacketPayload {
		public static final Type<SelectPiece> TYPE = new Type<>(RustBuilding.id("select_piece"));
		public static final StreamCodec<RegistryFriendlyByteBuf, SelectPiece> CODEC = StreamCodec.composite(
				ByteBufCodecs.VAR_INT, SelectPiece::piece,
				SelectPiece::new
		);

		@Override
		public Type<? extends CustomPacketPayload> type() {
			return TYPE;
		}
	}

	/** A choice from the hammer menu. */
	public record HammerAction(BlockPos pos, int action, int tier) implements CustomPacketPayload {
		public static final int UPGRADE = 0;
		public static final int DEMOLISH = 1;
		public static final int REPAIR = 2;

		public static final Type<HammerAction> TYPE = new Type<>(RustBuilding.id("hammer_action"));
		public static final StreamCodec<RegistryFriendlyByteBuf, HammerAction> CODEC = StreamCodec.composite(
				BlockPos.STREAM_CODEC, HammerAction::pos,
				ByteBufCodecs.VAR_INT, HammerAction::action,
				ByteBufCodecs.VAR_INT, HammerAction::tier,
				HammerAction::new
		);

		@Override
		public Type<? extends CustomPacketPayload> type() {
			return TYPE;
		}
	}

	/** A button on the tool cupboard screen. */
	public record CupboardAction(BlockPos pos, int action) implements CustomPacketPayload {
		public static final int AUTHORIZE = 0;
		public static final int DEAUTHORIZE = 1;
		public static final int CLEAR = 2;

		public static final Type<CupboardAction> TYPE = new Type<>(RustBuilding.id("cupboard_action"));
		public static final StreamCodec<RegistryFriendlyByteBuf, CupboardAction> CODEC = StreamCodec.composite(
				BlockPos.STREAM_CODEC, CupboardAction::pos,
				ByteBufCodecs.VAR_INT, CupboardAction::action,
				CupboardAction::new
		);

		@Override
		public Type<? extends CustomPacketPayload> type() {
			return TYPE;
		}
	}

	/** A button on the code lock screen. */
	public record CodeLockAction(BlockPos pos, int action, String code) implements CustomPacketPayload {
		public static final int INSTALL = 0;
		public static final int ENTER = 1;
		public static final int CHANGE = 2;
		public static final int REMOVE = 3;

		public static final Type<CodeLockAction> TYPE = new Type<>(RustBuilding.id("code_lock_action"));
		public static final StreamCodec<RegistryFriendlyByteBuf, CodeLockAction> CODEC = StreamCodec.composite(
				BlockPos.STREAM_CODEC, CodeLockAction::pos,
				ByteBufCodecs.VAR_INT, CodeLockAction::action,
				ByteBufCodecs.stringUtf8(8), CodeLockAction::code,
				CodeLockAction::new
		);

		@Override
		public Type<? extends CustomPacketPayload> type() {
			return TYPE;
		}
	}
}
