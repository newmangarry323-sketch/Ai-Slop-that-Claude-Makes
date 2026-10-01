package io.github.newmangarry323sketch.rustbuilding.building;

import java.util.ArrayDeque;
import java.util.Deque;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

import org.jspecify.annotations.Nullable;

import net.minecraft.core.BlockPos;
import net.minecraft.core.Direction;
import net.minecraft.server.level.ServerLevel;
import net.minecraft.world.level.Level;
import net.minecraft.world.level.block.Block;
import net.minecraft.world.level.block.Blocks;
import net.minecraft.world.level.block.state.BlockState;

import io.github.newmangarry323sketch.rustbuilding.raid.PieceDamage;
import io.github.newmangarry323sketch.rustbuilding.registry.ModBlocks;

/** Changes to the world: placing, upgrading and destroying pieces, and keeping shared blocks right. */
public final class BuildingOps {
	/** Upper bound on pieces one collapse may take down, as a guard against runaway cascades. */
	private static final int MAX_CASCADE = 512;

	private BuildingOps() {
	}

	public static void place(Level level, PlannedPiece plan) {
		for (int i = 0; i < plan.positions().size(); i++) {
			level.setBlock(plan.positions().get(i), plan.states().get(i), Block.UPDATE_ALL);
		}

		clearDamage(level, plan.ref());
	}

	/** Brings every block of a piece to {@code tier}; shared line blocks and pillars follow the best neighbour. */
	public static void upgrade(Level level, PieceRef ref, BuildingTier tier) {
		List<BlockPos> own = ref.ownBlocks(level);

		for (BlockPos pos : own) {
			setTier(level, pos, tier);

			if (ref instanceof PieceRef.Slab) {
				retierLegs(level, pos, tier);
			}
		}

		refreshShared(level, ref);
		clearDamage(level, ref);
	}

	/**
	 * Removes a piece and anything that no longer stands without it: walls on a foundation edge that
	 * nothing supports any more, stairs on a removed floor, floors that lost their last wall.
	 *
	 * @return how many pieces came down
	 */
	public static int destroy(Level level, PieceRef first) {
		Deque<PieceRef> queue = new ArrayDeque<>();
		Set<PieceRef> seen = new HashSet<>();
		queue.add(first);
		int destroyed = 0;

		while (!queue.isEmpty() && destroyed < MAX_CASCADE) {
			PieceRef ref = queue.poll();

			if (!seen.add(ref)) {
				continue;
			}

			List<BlockPos> own = ref.ownBlocks(level);

			if (own.isEmpty()) {
				continue;
			}

			for (BlockPos pos : own) {
				level.setBlock(pos, Blocks.AIR.defaultBlockState(), Block.UPDATE_ALL);

				if (ref instanceof PieceRef.Slab) {
					retierLegs(level, pos, null);
				}
			}

			destroyed++;
			refreshShared(level, ref);
			clearDamage(level, ref);
			queueDependents(level, ref, queue);
		}

		return destroyed;
	}

	private static void queueDependents(Level level, PieceRef ref, Deque<PieceRef> queue) {
		switch (ref) {
			case PieceRef.Slab slab -> {
				queue.add(new PieceRef.Stairs(slab.cellX(), slab.cellZ(), slab.y0()));

				for (Edge edge : Grid.edgesOf(slab.cellX(), slab.cellZ(), slab.y0())) {
					if (Structure.hasWall(level, edge) && !Structure.edgeSupported(level, edge)) {
						queue.add(new PieceRef.Wall(edge));
					}
				}
			}
			case PieceRef.Wall wall -> {
				int above = wall.edge().y0() + Grid.STOREY;

				for (int[] cell : wall.edge().cells()) {
					if (Structure.slabExists(level, cell[0], cell[1], above)
							&& !Structure.isFoundation(level, cell[0], cell[1], above)
							&& !PiecePlanner.floorSupported(level, cell[0], cell[1], above)) {
						queue.add(new PieceRef.Slab(cell[0], cell[1], above));
					}
				}
			}
			case PieceRef.Stairs stairs -> {
				// Nothing rests on stairs.
			}
		}
	}

	/** Re-derives the shared blocks around a piece after it changed. */
	public static void refreshShared(Level level, PieceRef ref) {
		switch (ref) {
			case PieceRef.Slab slab -> {
				for (BlockPos pos : Grid.slabBorder(slab.cellX(), slab.cellZ(), slab.y0())) {
					refreshSlabLine(level, pos);
				}
			}
			case PieceRef.Wall wall -> {
				for (int end = 0; end <= 1; end++) {
					for (int h = 1; h <= Grid.WALL_HEIGHT; h++) {
						refreshPillar(level, wall.edge().corner(end, h));
					}
				}
			}
			case PieceRef.Stairs stairs -> {
				// Stairs share nothing.
			}
		}
	}

	/** A line block at slab level exists while any cell beside it has a slab, at that slab's best grade. */
	private static void refreshSlabLine(Level level, BlockPos pos) {
		if (!Structure.isKind(level, pos, BuildingKind.SLAB)) {
			return;
		}

		BuildingTier best = null;

		for (int[] cell : Grid.cellsSharing(pos)) {
			BuildingTier tier = Structure.slabTier(level, cell[0], cell[1], pos.getY());

			if (tier != null && (best == null || tier.ordinal() > best.ordinal())) {
				best = tier;
			}
		}

		if (best == null) {
			level.setBlock(pos, Blocks.AIR.defaultBlockState(), Block.UPDATE_ALL);
		} else {
			setTier(level, pos, best);
		}

		retierLegs(level, pos, best);
	}

	/** A corner pillar exists while a wall block touches it at that height, at that wall's best grade. */
	private static void refreshPillar(Level level, BlockPos corner) {
		if (!Structure.isKind(level, corner, BuildingKind.WALL)) {
			return;
		}

		BuildingTier best = null;

		for (Direction direction : Direction.Plane.HORIZONTAL) {
			BlockState neighbour = level.getBlockState(corner.relative(direction));

			if (Structure.kindOf(neighbour) == BuildingKind.WALL) {
				BuildingTier tier = Structure.tierOf(neighbour);

				if (tier != null && (best == null || tier.ordinal() > best.ordinal())) {
					best = tier;
				}
			}
		}

		if (best == null) {
			level.setBlock(corner, Blocks.AIR.defaultBlockState(), Block.UPDATE_ALL);
		} else {
			setTier(level, corner, best);
		}
	}

	/** Sets the support legs under a slab block to a grade, or removes them when {@code tier} is null. */
	private static void retierLegs(Level level, BlockPos slabPos, @Nullable BuildingTier tier) {
		for (int down = 1; down <= PiecePlanner.MAX_LEG + 1; down++) {
			BlockPos pos = slabPos.below(down);

			if (!Structure.isKind(level, pos, BuildingKind.SUPPORT)) {
				return;
			}

			if (tier == null) {
				level.setBlock(pos, Blocks.AIR.defaultBlockState(), Block.UPDATE_ALL);
			} else {
				setTier(level, pos, tier);
			}
		}
	}

	private static void setTier(Level level, BlockPos pos, BuildingTier tier) {
		BlockState old = level.getBlockState(pos);

		if (!Structure.isPiece(old) || Structure.tierOf(old) == tier) {
			return;
		}

		Block target = Structure.isStairs(old) ? ModBlocks.stairs(tier) : ModBlocks.building(tier);
		level.setBlock(pos, target.withPropertiesOf(old), Block.UPDATE_ALL);
	}

	private static void clearDamage(Level level, PieceRef ref) {
		if (level instanceof ServerLevel serverLevel) {
			PieceDamage.get(serverLevel).clear(ref.anchor());
		}
	}
}
