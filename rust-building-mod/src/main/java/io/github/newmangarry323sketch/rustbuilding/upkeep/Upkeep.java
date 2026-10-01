package io.github.newmangarry323sketch.rustbuilding.upkeep;

import java.util.ArrayList;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Set;

import net.minecraft.core.BlockPos;
import net.minecraft.world.level.Level;

import io.github.newmangarry323sketch.rustbuilding.building.BuildingTier;
import io.github.newmangarry323sketch.rustbuilding.building.Edge;
import io.github.newmangarry323sketch.rustbuilding.building.Grid;
import io.github.newmangarry323sketch.rustbuilding.building.PieceRef;
import io.github.newmangarry323sketch.rustbuilding.building.Structure;
import io.github.newmangarry323sketch.rustbuilding.privilege.BuildingPrivilege;

/**
 * Rust's upkeep. A tool cupboard keeps the pieces in its zone from decaying by paying, out of its
 * storage, a share of what each piece cost to build - in that piece's own material - every upkeep
 * period. The share grows with the size of the base, using Rust's default brackets: the first 15 pieces
 * at 10%, the next 50 at 15%, the next 125 at 20% and the rest at 33.3%, averaged over all of them.
 *
 * <p>Time: Rust charges every 24 hours and its grades decay in hours. Here one hour of Rust is one
 * Minecraft day (20 minutes), so upkeep is charged every 24 Minecraft days and every ratio between
 * upkeep and decay stays as it is in Rust.
 */
public final class Upkeep {
	/** One Minecraft day, which stands in for one hour of Rust. */
	public static final long RUST_HOUR = 24_000L;
	/** Rust's upkeep period, 24 hours. */
	public static final long PERIOD = 24 * RUST_HOUR;

	private static final int[] BRACKET_SIZE = {15, 50, 125};
	private static final double[] BRACKET_RATE = {0.1, 0.15, 0.2, 0.333};

	private Upkeep() {
	}

	/** The share of each piece's cost charged per period for a base of this many pieces. */
	public static double rate(int pieces) {
		if (pieces <= 0) {
			return BRACKET_RATE[0];
		}

		int remaining = pieces;
		double charged = 0.0;

		for (int i = 0; i < BRACKET_SIZE.length && remaining > 0; i++) {
			int inBracket = Math.min(remaining, BRACKET_SIZE[i]);
			charged += inBracket * BRACKET_RATE[i];
			remaining -= inBracket;
		}

		charged += remaining * BRACKET_RATE[BRACKET_RATE.length - 1];
		return charged / pieces;
	}

	/**
	 * What a cupboard's pieces cost to keep up.
	 *
	 * @param perPeriod material per upkeep period for each grade, by {@link BuildingTier#ordinal()}
	 */
	public record Bill(int pieces, double[] perPeriod) {
		public static final Bill NONE = new Bill(0, new double[BuildingTier.values().length]);

		public double perPeriod(BuildingTier tier) {
			return this.perPeriod[tier.ordinal()];
		}

		/** How long one item of a grade's material keeps that grade's pieces up. */
		public long ticksPerItem(BuildingTier tier) {
			double cost = this.perPeriod(tier);
			return cost <= 0.0 ? Long.MAX_VALUE : Math.max(1L, Math.round(PERIOD / cost));
		}

		/** Whole items per period, rounded up, for display. */
		public int shownCost(BuildingTier tier) {
			return (int) Math.ceil(this.perPeriod(tier) - 1.0E-9);
		}
	}

	/** One piece's part of a bill: its grade and what it cost to build at that grade. */
	public record Charge(BuildingTier tier, int cost) {
	}

	public static Bill bill(List<Charge> charges) {
		double[] perPeriod = new double[BuildingTier.values().length];
		double rate = rate(charges.size());

		for (Charge charge : charges) {
			perPeriod[charge.tier().ordinal()] += charge.cost() * rate;
		}

		return new Bill(charges.size(), perPeriod);
	}

	/** The bill for the pieces in a cupboard's zone, as they stand now. */
	public static Bill scan(Level level, BlockPos cupboard) {
		List<Charge> charges = new ArrayList<>();

		for (PieceRef piece : piecesCoveredBy(level, cupboard)) {
			BuildingTier tier = piece.tier(level);

			if (tier != null) {
				charges.add(new Charge(tier, tier.cost(piece.units(level))));
			}
		}

		return bill(charges);
	}

	/**
	 * Every piece whose anchor lies in a cupboard's zone. Pieces are found from their slabs: every wall
	 * stands on a slab's edge and every staircase on a slab, so looking at each cell's slab levels finds
	 * them all. Cells in chunks that are not loaded are skipped rather than loaded.
	 */
	public static List<PieceRef> piecesCoveredBy(Level level, BlockPos cupboard) {
		Set<PieceRef> found = new LinkedHashSet<>();
		int reach = BuildingPrivilege.RANGE + Grid.SPAN;
		int minY = Math.max(level.getMinY(), cupboard.getY() - reach);
		int maxY = Math.min(level.getMaxY(), cupboard.getY() + reach);

		for (int cellX = Grid.cell(cupboard.getX() - reach); cellX <= Grid.cell(cupboard.getX() + reach); cellX++) {
			for (int cellZ = Grid.cell(cupboard.getZ() - reach); cellZ <= Grid.cell(cupboard.getZ() + reach); cellZ++) {
				if (!cellLoaded(level, cellX, cellZ)) {
					continue;
				}

				for (int y = minY; y <= maxY; y++) {
					if (!Structure.slabExists(level, cellX, cellZ, y)) {
						continue;
					}

					add(found, cupboard, new PieceRef.Slab(cellX, cellZ, y));
					PieceRef.Stairs stairs = new PieceRef.Stairs(cellX, cellZ, y);

					if (!stairs.ownBlocks(level).isEmpty()) {
						add(found, cupboard, stairs);
					}

					for (Edge edge : Grid.edgesOf(cellX, cellZ, y)) {
						if (Structure.hasWall(level, edge)) {
							add(found, cupboard, new PieceRef.Wall(edge));
						}
					}
				}
			}
		}

		return new ArrayList<>(found);
	}

	private static boolean cellLoaded(Level level, int cellX, int cellZ) {
		int x0 = Grid.lineOf(cellX);
		int z0 = Grid.lineOf(cellZ);
		return level.hasChunkAt(new BlockPos(x0, 0, z0))
				&& level.hasChunkAt(new BlockPos(x0 + Grid.SPAN, 0, z0))
				&& level.hasChunkAt(new BlockPos(x0, 0, z0 + Grid.SPAN))
				&& level.hasChunkAt(new BlockPos(x0 + Grid.SPAN, 0, z0 + Grid.SPAN));
	}

	private static void add(Set<PieceRef> found, BlockPos cupboard, PieceRef piece) {
		if (BuildingPrivilege.covers(cupboard, piece.anchor())) {
			found.add(piece);
		}
	}
}
