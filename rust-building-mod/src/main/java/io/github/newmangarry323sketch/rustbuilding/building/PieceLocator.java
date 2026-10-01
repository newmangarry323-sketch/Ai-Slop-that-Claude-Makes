package io.github.newmangarry323sketch.rustbuilding.building;

import org.jspecify.annotations.Nullable;

import net.minecraft.core.BlockPos;
import net.minecraft.world.level.BlockGetter;
import net.minecraft.world.level.block.state.BlockState;
import net.minecraft.world.phys.Vec3;

/** Works out which piece a building block belongs to. */
public final class PieceLocator {
	private PieceLocator() {
	}

	/**
	 * @param viewer where the question is asked from - the player's eyes, or an explosion's centre. It
	 *               decides between the two foundations sharing a line block, or the walls meeting at a pillar.
	 */
	@Nullable
	public static PieceRef locate(BlockGetter level, BlockPos pos, BlockState state, Vec3 viewer) {
		if (Structure.isStairs(state)) {
			int y0 = Structure.storeyOf(level, pos, state);
			return y0 == Structure.UNKNOWN ? null : new PieceRef.Stairs(Grid.cell(pos.getX()), Grid.cell(pos.getZ()), y0);
		}

		BuildingKind kind = Structure.kindOf(state);

		if (kind == null) {
			return null;
		}

		return switch (kind) {
			case SUPPORT -> {
				int y0 = Structure.storeyOf(level, pos, state);

				if (y0 == Structure.UNKNOWN) {
					yield null;
				}

				BlockPos slab = new BlockPos(pos.getX(), y0, pos.getZ());
				yield locateSlab(level, slab, viewer);
			}
			case SLAB -> locateSlab(level, pos, viewer);
			case WALL -> {
				int y0 = Structure.storeyOf(level, pos, state);
				yield y0 == Structure.UNKNOWN ? null : locateWall(level, pos, y0, viewer);
			}
		};
	}

	@Nullable
	private static PieceRef locateSlab(BlockGetter level, BlockPos pos, Vec3 viewer) {
		int y0 = pos.getY();

		if (Grid.isInterior(pos)) {
			return new PieceRef.Slab(Grid.cell(pos.getX()), Grid.cell(pos.getZ()), y0);
		}

		// A line block is shared: prefer the slab on the viewer's side, otherwise any neighbour that exists.
		int[] preferred = Grid.cellOnViewerSide(pos, viewer);

		if (Structure.slabExists(level, preferred[0], preferred[1], y0)) {
			return new PieceRef.Slab(preferred[0], preferred[1], y0);
		}

		for (int[] cell : Grid.cellsSharing(pos)) {
			if (Structure.slabExists(level, cell[0], cell[1], y0)) {
				return new PieceRef.Slab(cell[0], cell[1], y0);
			}
		}

		return null;
	}

	@Nullable
	private static PieceRef locateWall(BlockGetter level, BlockPos pos, int y0, Vec3 viewer) {
		boolean onX = Grid.onLine(pos.getX());
		boolean onZ = Grid.onLine(pos.getZ());

		if (onX && !onZ) {
			return new PieceRef.Wall(Edge.alongZ(pos.getX(), Grid.cell(pos.getZ()), y0));
		}

		if (onZ && !onX) {
			return new PieceRef.Wall(Edge.alongX(pos.getZ(), Grid.cell(pos.getX()), y0));
		}

		if (!onX) {
			return null;
		}

		// A corner pillar: pick the wall it joins that points most towards the viewer.
		int cornerCellX = Grid.cell(pos.getX());
		int cornerCellZ = Grid.cell(pos.getZ());
		Edge[] candidates = {
				Edge.alongX(pos.getZ(), cornerCellX, y0),
				Edge.alongX(pos.getZ(), cornerCellX - 1, y0),
				Edge.alongZ(pos.getX(), cornerCellZ, y0),
				Edge.alongZ(pos.getX(), cornerCellZ - 1, y0)
		};
		double[][] directions = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
		double dx = viewer.x - (pos.getX() + 0.5);
		double dz = viewer.z - (pos.getZ() + 0.5);
		Edge best = null;
		double bestScore = Double.NEGATIVE_INFINITY;

		for (int i = 0; i < candidates.length; i++) {
			if (!Structure.hasWall(level, candidates[i])) {
				continue;
			}

			double score = directions[i][0] * dx + directions[i][1] * dz;

			if (score > bestScore) {
				bestScore = score;
				best = candidates[i];
			}
		}

		return best == null ? null : new PieceRef.Wall(best);
	}
}
