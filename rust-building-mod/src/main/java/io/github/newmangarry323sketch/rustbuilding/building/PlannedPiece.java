package io.github.newmangarry323sketch.rustbuilding.building;

import java.util.List;

import org.jspecify.annotations.Nullable;

import net.minecraft.core.BlockPos;
import net.minecraft.network.chat.Component;
import net.minecraft.world.level.block.state.BlockState;

/**
 * A piece the building plan would place, with every block it would set (its own blocks plus any shared
 * line blocks, pillars or support legs that do not exist yet).
 *
 * @param units cost units, paid in twig material on placement
 * @param problem why it cannot be placed, or {@code null} if it can
 */
public record PlannedPiece(
		PieceType type,
		PieceRef ref,
		List<BlockPos> positions,
		List<BlockState> states,
		int units,
		@Nullable Component problem
) {
	public boolean valid() {
		return this.problem == null;
	}
}
