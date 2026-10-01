package io.github.newmangarry323sketch.rustbuilding;

import net.minecraft.core.BlockPos;

/**
 * Lets shared code open screens without referring to client-only classes, which do not exist on a
 * dedicated server. The client entrypoint installs the real implementation; on a server every call is
 * a no-op. Only call these when {@code level.isClientSide()}.
 */
public interface ClientBridge {
	ClientBridge NONE = new ClientBridge() {
	};

	static ClientBridge get() {
		return Holder.instance;
	}

	static void install(ClientBridge bridge) {
		Holder.instance = bridge;
	}

	default void openPieceMenu() {
	}

	default void openHammerMenu(BlockPos pos) {
	}

	default void openToolCupboard(BlockPos pos) {
	}

	default void openCodeLock(BlockPos pos, LockScreenMode mode) {
	}

	final class Holder {
		private static ClientBridge instance = NONE;

		private Holder() {
		}
	}
}
