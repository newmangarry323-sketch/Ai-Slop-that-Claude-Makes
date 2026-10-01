package io.github.newmangarry323sketch.rustbuilding.privilege;

import java.util.ArrayList;
import java.util.Collection;
import java.util.List;

import net.minecraft.core.BlockPos;
import net.minecraft.world.entity.player.Player;
import net.minecraft.world.level.Level;
import net.minecraft.world.level.block.entity.BlockEntity;
import net.minecraft.world.level.chunk.LevelChunk;

import io.github.newmangarry323sketch.rustbuilding.block.entity.ToolCupboardBlockEntity;

/**
 * Building privilege, Rust style. A tool cupboard covers a cube {@value #RANGE} blocks out from it in
 * every direction. Inside that cube only players on the cupboard's list may build, upgrade or demolish.
 *
 * <p>Rust measures the zone from the building itself rather than a fixed cube; a cube is a
 * simplification. To stop one group's zone covering another's, a cupboard cannot be placed where its
 * zone would overlap a cupboard that does not list you.
 */
public final class BuildingPrivilege {
	public static final int RANGE = 16;

	private BuildingPrivilege() {
	}

	/** Creative players are not limited, in the same way Rust admins are not. */
	public static boolean bypasses(Player player) {
		return player.isCreative();
	}

	public static boolean covers(BlockPos cupboard, BlockPos pos) {
		return Math.abs(cupboard.getX() - pos.getX()) <= RANGE
				&& Math.abs(cupboard.getY() - pos.getY()) <= RANGE
				&& Math.abs(cupboard.getZ() - pos.getZ()) <= RANGE;
	}

	/** Loaded tool cupboards within {@code radius} blocks (on every axis) of a position. */
	public static List<ToolCupboardBlockEntity> cupboardsNear(Level level, BlockPos centre, int radius) {
		List<ToolCupboardBlockEntity> found = new ArrayList<>();
		int minChunkX = (centre.getX() - radius) >> 4;
		int maxChunkX = (centre.getX() + radius) >> 4;
		int minChunkZ = (centre.getZ() - radius) >> 4;
		int maxChunkZ = (centre.getZ() + radius) >> 4;

		for (int chunkX = minChunkX; chunkX <= maxChunkX; chunkX++) {
			for (int chunkZ = minChunkZ; chunkZ <= maxChunkZ; chunkZ++) {
				if (!level.hasChunk(chunkX, chunkZ)) {
					continue;
				}

				LevelChunk chunk = level.getChunk(chunkX, chunkZ);

				for (BlockEntity blockEntity : chunk.getBlockEntities().values()) {
					if (blockEntity instanceof ToolCupboardBlockEntity cupboard && !cupboard.isRemoved()) {
						BlockPos pos = cupboard.getBlockPos();

						if (Math.abs(pos.getX() - centre.getX()) <= radius
								&& Math.abs(pos.getY() - centre.getY()) <= radius
								&& Math.abs(pos.getZ() - centre.getZ()) <= radius) {
							found.add(cupboard);
						}
					}
				}
			}
		}

		return found;
	}

	/** Whether every chunk within {@code radius} blocks (on X and Z) of a position is loaded. */
	public static boolean areaLoaded(Level level, BlockPos centre, int radius) {
		for (int chunkX = (centre.getX() - radius) >> 4; chunkX <= (centre.getX() + radius) >> 4; chunkX++) {
			for (int chunkZ = (centre.getZ() - radius) >> 4; chunkZ <= (centre.getZ() + radius) >> 4; chunkZ++) {
				if (!level.hasChunk(chunkX, chunkZ)) {
					return false;
				}
			}
		}

		return true;
	}

	public static boolean canBuild(Level level, Player player, BlockPos pos) {
		return canBuildAll(level, player, List.of(pos));
	}

	/**
	 * A position is buildable when no cupboard covers it, or the player is on the list of one that does.
	 */
	public static boolean canBuildAll(Level level, Player player, Collection<BlockPos> positions) {
		if (positions.isEmpty() || bypasses(player)) {
			return true;
		}

		int minX = Integer.MAX_VALUE;
		int minY = Integer.MAX_VALUE;
		int minZ = Integer.MAX_VALUE;
		int maxX = Integer.MIN_VALUE;
		int maxY = Integer.MIN_VALUE;
		int maxZ = Integer.MIN_VALUE;

		for (BlockPos pos : positions) {
			minX = Math.min(minX, pos.getX());
			minY = Math.min(minY, pos.getY());
			minZ = Math.min(minZ, pos.getZ());
			maxX = Math.max(maxX, pos.getX());
			maxY = Math.max(maxY, pos.getY());
			maxZ = Math.max(maxZ, pos.getZ());
		}

		BlockPos centre = new BlockPos((minX + maxX) / 2, (minY + maxY) / 2, (minZ + maxZ) / 2);
		int extent = Math.max(maxX - minX, Math.max(maxY - minY, maxZ - minZ)) / 2 + 1;
		List<ToolCupboardBlockEntity> cupboards = cupboardsNear(level, centre, RANGE + extent);

		if (cupboards.isEmpty()) {
			return true;
		}

		for (BlockPos pos : positions) {
			boolean covered = false;
			boolean authorized = false;

			for (ToolCupboardBlockEntity cupboard : cupboards) {
				if (covers(cupboard.getBlockPos(), pos)) {
					covered = true;

					if (cupboard.isAuthorized(player.getUUID())) {
						authorized = true;
						break;
					}
				}
			}

			if (covered && !authorized) {
				return false;
			}
		}

		return true;
	}

	/** A new cupboard's zone may not overlap the zone of a cupboard that does not list this player. */
	public static boolean canPlaceCupboard(Level level, Player player, BlockPos pos) {
		if (bypasses(player)) {
			return true;
		}

		for (ToolCupboardBlockEntity cupboard : cupboardsNear(level, pos, RANGE * 2)) {
			if (!cupboard.isAuthorized(player.getUUID())) {
				return false;
			}
		}

		return true;
	}
}
