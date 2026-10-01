package io.github.newmangarry323sketch.rustbuilding.building;

import org.jspecify.annotations.Nullable;

import net.minecraft.core.BlockPos;
import net.minecraft.world.level.BlockGetter;
import net.minecraft.world.level.block.state.BlockState;

import io.github.newmangarry323sketch.rustbuilding.block.BuildingBlock;
import io.github.newmangarry323sketch.rustbuilding.block.BuildingStairsBlock;

/** Read-only questions about building blocks in the world. */
public final class Structure {
	/** Returned by {@link #storeyOf} when no slab can be found below a block. */
	public static final int UNKNOWN = Integer.MIN_VALUE;

	private Structure() {
	}

	public static boolean isBuilding(BlockState state) {
		return state.getBlock() instanceof BuildingBlock;
	}

	public static boolean isStairs(BlockState state) {
		return state.getBlock() instanceof BuildingStairsBlock;
	}

	/** Any block that is part of a building piece. */
	public static boolean isPiece(BlockState state) {
		return isBuilding(state) || isStairs(state);
	}

	@Nullable
	public static BuildingKind kindOf(BlockState state) {
		return state.getBlock() instanceof BuildingBlock ? state.getValue(BuildingBlock.KIND) : null;
	}

	@Nullable
	public static BuildingTier tierOf(BlockState state) {
		if (state.getBlock() instanceof BuildingBlock building) {
			return building.tier();
		}

		if (state.getBlock() instanceof BuildingStairsBlock stairs) {
			return stairs.tier();
		}

		return null;
	}

	public static boolean isKind(BlockGetter level, BlockPos pos, BuildingKind kind) {
		return kindOf(level.getBlockState(pos)) == kind;
	}

	/** Empty space a piece may be built into: air, grass, snow layers, water and so on. */
	public static boolean isFree(BlockGetter level, BlockPos pos) {
		return level.getBlockState(pos).canBeReplaced();
	}

	public static boolean slabExists(BlockGetter level, int cellX, int cellZ, int y) {
		return isKind(level, Grid.slabAnchor(cellX, cellZ, y), BuildingKind.SLAB);
	}

	@Nullable
	public static BuildingTier slabTier(BlockGetter level, int cellX, int cellZ, int y) {
		BlockState anchor = level.getBlockState(Grid.slabAnchor(cellX, cellZ, y));
		return kindOf(anchor) == BuildingKind.SLAB ? tierOf(anchor) : null;
	}

	/** A slab counts as a foundation when something solid, or a support leg, is under its centre. */
	public static boolean isFoundation(BlockGetter level, int cellX, int cellZ, int y) {
		BlockPos below = Grid.slabAnchor(cellX, cellZ, y).below();
		return isKind(level, below, BuildingKind.SUPPORT) || (!isPiece(level.getBlockState(below)) && !isFree(level, below));
	}

	/** Whether any block of a wall-like piece stands on this edge. */
	public static boolean hasWall(BlockGetter level, Edge edge) {
		for (int u = 0; u < 3; u++) {
			for (int h = 1; h <= Grid.WALL_HEIGHT; h++) {
				if (isKind(level, edge.pos(u, h), BuildingKind.WALL)) {
					return true;
				}
			}
		}

		return false;
	}

	/** Whether the slab blocks a wall on this edge would stand on are all present. */
	public static boolean edgeSupported(BlockGetter level, Edge edge) {
		for (int u = 0; u < 3; u++) {
			if (!isKind(level, edge.pos(u, 0), BuildingKind.SLAB)) {
				return false;
			}
		}

		return true;
	}

	/**
	 * The slab level of the storey a building block belongs to, found by walking to the slab it stands
	 * on (walls, stairs) or hangs from (support legs).
	 */
	public static int storeyOf(BlockGetter level, BlockPos pos, BlockState state) {
		if (isStairs(state)) {
			for (int down = 1; down <= Grid.STOREY + 1; down++) {
				if (isKind(level, pos.below(down), BuildingKind.SLAB)) {
					return pos.getY() - down;
				}
			}

			return UNKNOWN;
		}

		BuildingKind kind = kindOf(state);

		if (kind == null) {
			return UNKNOWN;
		}

		return switch (kind) {
			case SLAB -> pos.getY();
			case SUPPORT -> {
				for (int up = 1; up <= Grid.STOREY; up++) {
					BuildingKind above = kindOf(level.getBlockState(pos.above(up)));

					if (above == BuildingKind.SLAB) {
						yield pos.getY() + up;
					}

					if (above != BuildingKind.SUPPORT) {
						break;
					}
				}

				yield UNKNOWN;
			}
			case WALL -> {
				for (int down = 1; down <= Grid.WALL_HEIGHT; down++) {
					BuildingKind below = kindOf(level.getBlockState(pos.below(down)));

					if (below == BuildingKind.SLAB) {
						yield pos.getY() - down;
					}

					if (below != BuildingKind.WALL) {
						break;
					}
				}

				yield UNKNOWN;
			}
		};
	}
}
