package io.github.newmangarry323sketch.rustbuilding.block;

import net.minecraft.core.BlockPos;
import net.minecraft.server.level.ServerLevel;
import net.minecraft.world.item.ItemStack;
import net.minecraft.world.level.Level;
import net.minecraft.world.level.block.Block;
import net.minecraft.world.level.block.Blocks;
import net.minecraft.world.level.block.state.BlockState;

import io.github.newmangarry323sketch.rustbuilding.building.Edge;
import io.github.newmangarry323sketch.rustbuilding.building.Grid;
import io.github.newmangarry323sketch.rustbuilding.raid.PieceDamage;
import io.github.newmangarry323sketch.rustbuilding.registry.ModBlocks;
import io.github.newmangarry323sketch.rustbuilding.registry.ModItems;

/** Doors and garage doors fitted into a wall's openings, which go when the wall goes. */
public final class Inserts {
	private Inserts() {
	}

	/**
	 * Takes every door and garage door out of a wall's panel.
	 *
	 * @param drop hand the items back (an owner demolishing the wall), rather than destroying them
	 *             (a raid, decay, or a collapse)
	 */
	public static void removeAll(Level level, Edge edge, boolean drop) {
		for (int u = 0; u < 3; u++) {
			for (int h = 1; h <= Grid.WALL_HEIGHT; h++) {
				BlockPos pos = edge.pos(u, h);
				BlockState state = level.getBlockState(pos);

				if (state.getBlock() instanceof RustDoorBlock door) {
					removeDoor(level, pos, state, door, drop);
				} else if (state.getBlock() instanceof GarageDoorBlock garageDoor) {
					garageDoor.removeAll(level, pos, state, drop);
				}
			}
		}
	}

	private static void removeDoor(Level level, BlockPos pos, BlockState state, RustDoorBlock door, boolean drop) {
		BlockPos lower = RustDoorBlock.lowerHalf(pos, state);

		if (drop) {
			Block.popResource(level, lower, new ItemStack(door == ModBlocks.ARMORED_DOOR ? ModItems.ARMORED_DOOR : ModItems.SHEET_METAL_DOOR));
		}

		// The bottom half first: the top half then removes itself, and only the bottom half has loot.
		level.setBlock(lower, Blocks.AIR.defaultBlockState(), Block.UPDATE_ALL);
		level.setBlock(lower.above(), Blocks.AIR.defaultBlockState(), Block.UPDATE_ALL);

		if (level instanceof ServerLevel serverLevel) {
			PieceDamage.get(serverLevel).clear(lower);
		}
	}
}
